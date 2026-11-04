/*
===========================================================================

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses/

===========================================================================
*/

#include "lobby_api.h"

#include "common/database.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/utils.h"
#include "common/xi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace lobby_api
{

namespace
{

// Lobby error codes the 2010 client shows (FFXI-3NNN)
constexpr uint32 ErrNameUnavailable = 313; // "The character name you entered is unavailable."
constexpr uint32 ErrNameServer      = 314; // "Failed to register with the name server."
constexpr uint32 ErrBanned          = 332; // "Could not connect to lobby server. Please check this title's news for announcements."

// The charid is the low 16 bits of the sub id the lobby keeps for a character
constexpr uint32 MaxCharId = 0xFFFF;

constexpr size_t MaxBodyBytes = 4096;

// Creating, renaming and deleting are one at a time, so two characters cannot take the same name or charid
std::mutex mutationMutex;

// What a handler answers instead of 200
struct Refusal
{
    int         status;
    uint32      error;
    std::string message;
};

[[noreturn]] void refuse(int status, std::string message, uint32 error = 0)
{
    throw Refusal{ status, error, std::move(message) };
}

void reply(httplib::Response& res, int status, const json& body)
{
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

// Both strings, in time that does not depend on where they differ
auto sameKey(const std::string& given, const std::string& expected) -> bool
{
    if (given.size() != expected.size())
    {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < given.size(); ++i)
    {
        diff |= static_cast<unsigned char>(given[i]) ^ static_cast<unsigned char>(expected[i]);
    }
    return diff == 0;
}

auto authorized(const httplib::Request& req) -> bool
{
    const auto key = settings::get<std::string>("network.LOBBY_API_KEY");
    if (key.empty())
    {
        return false;
    }
    constexpr std::string_view prefix = "Bearer ";
    const auto                 header = req.get_header_value("Authorization");
    return header.starts_with(prefix) && sameKey(header.substr(prefix.size()), key);
}

// Runs a handler for an authorized caller: a refusal becomes its status and JSON, a bad body a 400, anything else a 500
template <typename Fn>
auto handle(Fn fn)
{
    return [fn](const httplib::Request& req, httplib::Response& res)
    {
        if (settings::get<std::string>("network.LOBBY_API_KEY").empty())
        {
            res.status = 404;
            return;
        }
        if (!authorized(req))
        {
            ShowWarningFmt("lobby_api: {} sent no valid lobby key to {}", req.remote_addr, req.path);
            reply(res, 401, { { "error", 0 }, { "message", "unauthorized" } });
            return;
        }
        try
        {
            if (req.body.size() > MaxBodyBytes)
            {
                refuse(413, "body too large");
            }
            fn(req, res);
        }
        catch (const Refusal& r)
        {
            reply(res, r.status, { { "error", r.error }, { "message", r.message } });
        }
        catch (const json::exception& e)
        {
            reply(res, 400, { { "error", 0 }, { "message", fmt::format("bad request: {}", e.what()) } });
        }
        catch (const std::exception& e)
        {
            ShowErrorFmt("lobby_api: {} failed: {}", req.path, e.what());
            reply(res, 500, { { "error", ErrNameServer }, { "message", "database error" } });
        }
    };
}

auto body(const httplib::Request& req) -> json
{
    auto j = json::parse(req.body);
    if (!j.is_object())
    {
        refuse(400, "body is not an object");
    }
    return j;
}

// A statement that must succeed; the database logs why not
template <typename... Args>
auto run(const std::string& query, Args&&... args) -> std::unique_ptr<db::ResultSet>
{
    auto rset = db::preparedStmt(query, std::forward<Args>(args)...);
    if (!rset)
    {
        throw std::runtime_error("query failed: " + query);
    }
    return rset;
}

// 3 to 15 letters
auto validName(const std::string& name) -> bool
{
    return name.size() >= 3 && name.size() <= 15 && std::ranges::all_of(name, [](unsigned char c)
                                                                        {
                                                                            return std::isalpha(c) != 0 && c < 0x80;
                                                                        });
}

// 0 if the name can be given to a character of this world, else the lobby error to show. Deleted characters keep
// their rows (accid 0) but not their names: a new character can take one.
auto nameError(const std::string& name) -> uint32
{
    if (!validName(name))
    {
        ShowWarningFmt("lobby_api: name <{}> refused: not 3 to 15 letters", name);
        return ErrNameUnavailable;
    }
    auto rset = run("SELECT COUNT(*) AS `n` FROM chars WHERE LOWER(charname) = LOWER(?) AND accid <> 0", name);
    if (rset->next() && rset->get<uint32>("n") != 0)
    {
        ShowWarningFmt("lobby_api: name <{}> refused: in use", name);
        return ErrNameUnavailable;
    }
    return 0;
}

constexpr std::array<const char*, 22> JobColumns = {
    "war", "mnk", "whm", "blm", "rdm", "thf", "pld", "drk", "bst", "brd", "rng", "sam", "nin", "drg", "smn", "blu", "cor", "pup", "dnc", "sch", "geo", "run"
};

// The character as the lobby shows it in the list, if it is on this content id. Charid 0 asks for whichever
// character the content id holds, which is how the lobby finishes a create it lost the answer to.
auto characterInfo(uint32 contentId, uint32 charId) -> json
{
    if (contentId == 0)
    {
        return { { "found", false } };
    }

    auto rset = run("SELECT chars.charid, charname, doRename, pos_zone, mjob, sjob, race, face, head, body, hands, legs, feet, main, sub, "
                    "nation, size, war, mnk, whm, blm, rdm, thf, pld, drk, bst, brd, rng, sam, nin, drg, smn, blu, cor, pup, dnc, sch, geo, run "
                    "FROM chars "
                    "INNER JOIN char_stats ON char_stats.charid = chars.charid "
                    "INNER JOIN char_look ON char_look.charid = chars.charid "
                    "INNER JOIN char_jobs ON char_jobs.charid = chars.charid "
                    "WHERE (chars.charid = ? OR ? = 0) AND chars.accid = ? LIMIT 1",
                    charId,
                    charId,
                    contentId);
    if (!rset->next())
    {
        return { { "found", false } };
    }

    const auto mainJob = rset->get<uint8>("mjob");
    return {
        { "found", true },
        { "contentId", contentId },
        { "charId", rset->get<uint32>("charid") },
        { "name", rset->get<std::string>("charname") },
        { "rename", rset->get<uint8>("doRename") != 0 },
        { "zone", rset->get<uint16>("pos_zone") },
        { "mainJob", mainJob },
        { "mainJobLevel", mainJob >= 1 && mainJob <= JobColumns.size() ? rset->get<uint8>(JobColumns[mainJob - 1]) : 0 },
        { "subJob", rset->get<uint8>("sjob") },
        { "race", rset->get<uint16>("race") },
        { "face", rset->get<uint16>("face") },
        { "head", rset->get<uint16>("head") },
        { "body", rset->get<uint16>("body") },
        { "hands", rset->get<uint16>("hands") },
        { "legs", rset->get<uint16>("legs") },
        { "feet", rset->get<uint16>("feet") },
        { "main", rset->get<uint16>("main") },
        { "sub", rset->get<uint16>("sub") },
        { "nation", rset->get<uint8>("nation") },
        { "size", rset->get<uint8>("size") },
    };
}

auto sameName(const std::string& a, const std::string& b) -> bool
{
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y)
                              {
                                  return std::tolower(x) == std::tolower(y);
                              });
}

// Whether the PlayOnline account is banned from this world (pol_bans, set with !banaccount)
auto isBanned(uint64 polId) -> bool
{
    auto rset = run("SELECT COUNT(*) AS `n` FROM pol_bans WHERE pol_id = ? AND (banned_until = 0 OR banned_until > UNIX_TIMESTAMP())", polId);
    return rset->next() && rset->get<uint32>("n") != 0;
}

auto owns(uint32 contentId, uint32 charId) -> bool
{
    auto rset = run("SELECT COUNT(*) AS `n` FROM chars WHERE charid = ? AND accid = ?", charId, contentId);
    return rset->next() && rset->get<uint32>("n") != 0;
}

// The next free charid that fits 16 bits; past 0xFFFF, the first gap. 0: none.
auto nextCharId() -> uint32
{
    auto   rset   = run("SELECT COALESCE(MAX(charid), 0) + 1 AS `id` FROM chars WHERE charid <= ?", MaxCharId);
    uint32 charId = rset->next() ? rset->get<uint32>("id") : 1;
    if (charId <= MaxCharId)
    {
        return charId;
    }

    auto gap = run("SELECT MIN(c.charid) + 1 AS `id` FROM chars c "
                   "WHERE c.charid < ? AND NOT EXISTS (SELECT 1 FROM chars n WHERE n.charid = c.charid + 1)",
                   MaxCharId);
    if (gap->next() && !gap->isNull("id"))
    {
        return gap->get<uint32>("id");
    }
    return 0;
}

auto startZoneAllowed(uint8 nation, uint32 zone) -> bool
{
    static constexpr std::array<std::array<uint32, 3>, 3> zones = { {
        { 0xE6, 0xE7, 0xE8 }, // San d'Oria
        { 0xEA, 0xEB, 0xEC }, // Bastok
        { 0xEE, 0xF0, 0xF1 }, // Windurst
    } };
    return nation < zones.size() && std::ranges::find(zones[nation], zone) != zones[nation].end();
}

auto fromHex(const std::string& hex) -> std::optional<std::array<uint8, 20>>
{
    if (hex.size() != 40)
    {
        return std::nullopt;
    }
    std::array<uint8, 20> out{};
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto pair = hex.substr(i * 2, 2);
        if (!std::isxdigit(static_cast<unsigned char>(pair[0])) || !std::isxdigit(static_cast<unsigned char>(pair[1])))
        {
            return std::nullopt;
        }
        out[i] = static_cast<uint8>(std::stoul(pair, nullptr, 16));
    }
    return out;
}

void listCharacters(const httplib::Request& req, httplib::Response& res)
{
    const auto  j         = body(req);
    const auto& requested = j.at("characters");
    if (!requested.is_array() || requested.size() > 64)
    {
        refuse(400, "characters must be a list of at most 64");
    }

    json characters = json::array();
    for (const auto& entry : requested)
    {
        characters.push_back(characterInfo(entry.at("contentId").get<uint32>(), entry.at("charId").get<uint32>()));
    }
    reply(res, 200, { { "characters", characters } });
}

void checkName(const httplib::Request& req, httplib::Response& res)
{
    const auto j = body(req);
    reply(res, 200, { { "error", nameError(j.at("name").get<std::string>()) } });
}

void createCharacter(const httplib::Request& req, httplib::Response& res)
{
    const auto j         = body(req);
    const auto contentId = j.at("contentId").get<uint32>();
    const auto polId     = j.at("polId").get<uint64>();
    const auto name      = j.at("name").get<std::string>();
    const auto race      = j.at("race").get<uint8>();
    const auto face      = j.at("face").get<uint8>();
    const auto size      = j.at("size").get<uint8>();
    const auto job       = j.at("job").get<uint8>();
    const auto nation    = j.at("nation").get<uint8>();
    const auto zone      = j.at("startZone").get<uint32>();

    if (contentId == 0 || polId == 0 || race < 1 || race > 8 || size > 2 || face > 15 || job < 1 || job > 6 || !startZoneAllowed(nation, zone))
    {
        refuse(400, fmt::format("invalid character for content id {}", contentId));
    }

    const std::lock_guard lock(mutationMutex);

    if (isBanned(polId))
    {
        ShowWarningFmt("lobby_api: refused a character for content id {}: POL ID {} is banned", contentId, polId);
        refuse(403, "PlayOnline account is banned from this world", ErrBanned);
    }

    // A content id holds one character. The lobby sends a create again when it never got the answer, and gets the
    // character it already made back.
    {
        auto rset = run("SELECT charid, charname, pol_id FROM chars WHERE accid = ? LIMIT 1", contentId);
        if (rset->next())
        {
            if (rset->get<uint64>("pol_id") == polId && sameName(rset->get<std::string>("charname"), name))
            {
                ShowInfoFmt("lobby_api: create of {} on content id {} repeated; it is character {}", name, contentId, rset->get<uint32>("charid"));
                reply(res, 200, { { "charId", rset->get<uint32>("charid") } });
                return;
            }
            refuse(409, "content id already has a character");
        }
    }

    if (const auto error = nameError(name))
    {
        refuse(409, "name unavailable", error);
    }

    const auto charId = nextCharId();
    if (charId == 0)
    {
        ShowErrorFmt("lobby_api: content id {}: no free character id below 0x10000", contentId);
        refuse(507, "no free character id", ErrNameServer);
    }

    const auto cutscene = settings::get<bool>("main.NEW_CHARACTER_CUTSCENE");
    const auto created  = db::transaction(
        [&]()
        {
            run("INSERT INTO chars(charid,accid,pol_id,charname,pos_zone,nation) VALUES(?, ?, ?, ?, ?, ?)", charId, contentId, polId, name, zone, nation);
            run("INSERT INTO char_look(charid,face,race,size) VALUES(?, ?, ?, ?)", charId, face, race, size);
            run("INSERT INTO char_stats(charid,mjob) VALUES(?, ?)", charId, job);
            run("INSERT INTO char_exp(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_flags(charid) VALUES(?) ON DUPLICATE KEY UPDATE disconnecting = disconnecting", charId);
            run("INSERT INTO char_jobs(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_points(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_unlocks(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_profile(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_storage(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("DELETE FROM char_inventory WHERE charid = ?", charId);
            run("INSERT INTO char_inventory(charid) VALUES(?)", charId);
            if (cutscene)
            {
                run("INSERT INTO char_vars(charid, varname, value) VALUES(?, ?, ?)", charId, "HQuest[newCharacterCS]notSeen", 1);
            }
        });
    if (!created)
    {
        refuse(500, "could not create the character", ErrNameServer);
    }

    ShowInfoFmt("lobby_api: created {} ({}) on content id {}", name, charId, contentId);
    reply(res, 200, { { "charId", charId } });
}

void renameCharacter(const httplib::Request& req, httplib::Response& res)
{
    const auto j         = body(req);
    const auto contentId = j.at("contentId").get<uint32>();
    const auto charId    = j.at("charId").get<uint32>();
    const auto name      = j.at("name").get<std::string>();

    const std::lock_guard lock(mutationMutex);

    if (const auto error = nameError(name))
    {
        refuse(409, "name unavailable", error);
    }

    // Only a character flagged for a rename can take a new name
    auto rset = run("UPDATE chars SET charname = ?, doRename = 0 WHERE charid = ? AND accid = ? AND doRename <> 0", name, charId, contentId);
    if (rset->rowsAffected() == 0)
    {
        refuse(409, "character is not flagged for a rename", ErrNameServer);
    }
    ShowInfoFmt("lobby_api: renamed character {} to {}", charId, name);
    reply(res, 200, json::object());
}

void deleteCharacter(const httplib::Request& req, httplib::Response& res)
{
    const auto j         = body(req);
    const auto contentId = j.at("contentId").get<uint32>();
    const auto charId    = j.at("charId").get<uint32>();

    const std::lock_guard lock(mutationMutex);

    if (!owns(contentId, charId))
    {
        refuse(404, "no such character on that content id");
    }

    // The row, and every char_* row, stay: accid 0 marks a deleted character, original_accid the one it was on
    run("UPDATE chars SET original_accid = accid, accid = 0 WHERE charid = ? AND accid = ?", charId, contentId);
    run("DELETE FROM accounts_sessions WHERE charid = ?", charId);
    ShowInfoFmt("lobby_api: deleted character {} of content id {}", charId, contentId);
    reply(res, 200, json::object());
}

// A session row means the character is in the world, once a zone-out the other map server never saw (client_port 0
// for over 2 minutes) has been cleared away.
auto isOnline(uint32 charId) -> bool
{
    run("DELETE FROM accounts_sessions WHERE charid = ? AND client_port = 0 AND last_zoneout_time <= SUBTIME(NOW(), '00:02:00')", charId);
    auto rset = run("SELECT COUNT(*) AS `n` FROM accounts_sessions WHERE charid = ?", charId);
    return !rset->next() || rset->get<uint32>("n") != 0;
}

void online(const httplib::Request& req, httplib::Response& res)
{
    const auto  j       = body(req);
    const auto& charIds = j.at("charIds");
    if (!charIds.is_array() || charIds.size() > 64)
    {
        refuse(400, "charIds must be a list of at most 64");
    }

    bool any = false;
    for (const auto& charId : charIds)
    {
        any = isOnline(charId.get<uint32>()) || any;
    }
    reply(res, 200, { { "online", any } });
}

void enterWorld(const httplib::Request& req, httplib::Response& res)
{
    const auto j          = body(req);
    const auto contentId  = j.at("contentId").get<uint32>();
    const auto charId     = j.at("charId").get<uint32>();
    const auto key        = fromHex(j.at("key").get<std::string>());
    const auto serverAddr = j.at("serverAddr").get<uint32>();
    const auto serverPort = j.at("serverPort").get<uint16>();
    const auto clientAddr = j.at("clientAddr").get<uint32>();
    const auto version    = j.at("clientVersion").get<std::string>();
    const auto expansions = j.at("clientExpansions").get<uint32>();
    const auto polId      = j.at("account").at("polId").get<uint64>();

    if (!key || contentId == 0 || polId == 0 || version.size() > 16)
    {
        refuse(400, "invalid session");
    }

    const std::lock_guard lock(mutationMutex);

    if (!owns(contentId, charId))
    {
        refuse(404, "character is not on that content id (deleted?)");
    }
    if (isBanned(polId))
    {
        ShowWarningFmt("lobby_api: refused character {}: POL ID {} is banned", charId, polId);
        refuse(403, "PlayOnline account is banned from this world", ErrBanned);
    }
    if (isOnline(charId))
    {
        refuse(409, "character is still logged in");
    }

    // The lobby is the authority on which PlayOnline account owns a content id
    {
        auto rset = run("SELECT pol_id FROM chars WHERE charid = ?", charId);
        if (rset->next() && rset->get<uint64>("pol_id") != polId)
        {
            if (rset->get<uint64>("pol_id") != 0)
            {
                ShowWarningFmt("lobby_api: character {} moves from POL ID {} to {}", charId, rset->get<uint64>("pol_id"), polId);
            }
            run("UPDATE chars SET pol_id = ? WHERE charid = ?", polId, charId);
        }
    }

    // One character in this world per PlayOnline account. The lobby checks its other worlds itself.
    {
        auto others = run("SELECT charid FROM chars WHERE pol_id = ? AND charid <> ? AND accid <> 0", polId, charId);
        while (others->next())
        {
            if (isOnline(others->get<uint32>("charid")))
            {
                refuse(409, fmt::format("character {} of the same PlayOnline account is still logged in", others->get<uint32>("charid")));
            }
        }
    }

    uint8 sessionKey[20] = {};
    std::copy(key->begin(), key->end(), sessionKey);
    run("INSERT INTO accounts_sessions(accid, charid, session_key, server_addr, server_port, client_addr, version_mismatch, client_version, client_expansions) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
        contentId,
        charId,
        sessionKey,
        serverAddr,
        serverPort,
        clientAddr,
        0,
        version,
        expansions);
    reply(res, 200, json::object());
}

} // namespace

void registerRoutes(httplib::Server& server)
{
    server.Post("/api/lobby/characters/list", handle(listCharacters));
    server.Post("/api/lobby/characters/check-name", handle(checkName));
    server.Post("/api/lobby/characters/create", handle(createCharacter));
    server.Post("/api/lobby/characters/rename", handle(renameCharacter));
    server.Post("/api/lobby/characters/delete", handle(deleteCharacter));
    server.Post("/api/lobby/characters/online", handle(online));
    server.Post("/api/lobby/sessions/enter", handle(enterWorld));
}

} // namespace lobby_api
