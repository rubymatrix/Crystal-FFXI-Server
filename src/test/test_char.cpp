/*
===========================================================================

  Copyright (c) 2025 LandSandBoat Dev Teams

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

#include "test_char.h"

#include "common/cbasetypes.h"
#include "common/settings.h"
#include "map/entities/base_entity.h"
#include "map/entities/char_entity.h"
#include "test_common.h"
#include "zone.h"

#include <array>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{

constexpr uint32 MinTestCharId = 20000000;
constexpr uint32 MinTestAccId  = 20000000;

std::vector<std::string> charIdTables = {
    "chars",
    "char_chocobos",
    "char_effects",
    "char_equip",
    "char_equip_saved",
    "char_exp",
    "char_flags",
    "char_history",
    "char_inventory",
    "char_job_points",
    "char_jobs",
    "char_look",
    "char_merit",
    "char_monstrosity",
    "char_pet",
    "char_points",
    "char_profile",
    "char_recast",
    "char_skills",
    "char_spells",
    "char_stats",
    "char_storage",
    "char_style",
    "char_unlocks",
    "char_vars",

    "accounts_parties",
    "accounts_sessions",
};

} // namespace

// Cleans the given character ID or all characters with IDs >= MinTestCharId.
void TestChar::clean(uint32 charId /* = 0 */)
{
    const auto ids = [&]() -> std::pair<uint32, uint32>
    {
        if (charId > 0)
        {
            return { charId, charId };
        }

        return { MinTestCharId, std::numeric_limits<uint32>::max() };
    }();

    constexpr auto oneIdColumn = std::array{
        "DELETE FROM auction_house WHERE seller BETWEEN ? AND ?",
        "DELETE FROM audit_vendor WHERE seller BETWEEN ? AND ?",
    };

    constexpr auto twoIdColumns = std::array{
        "DELETE FROM delivery_box WHERE charid BETWEEN ? AND ? OR senderid BETWEEN ? AND ?",
        "DELETE FROM audit_bazaar WHERE seller BETWEEN ? AND ? OR purchaser BETWEEN ? AND ?",
        "DELETE FROM audit_trade WHERE sender BETWEEN ? AND ? OR receiver BETWEEN ? AND ?",
    };

    for (const auto* query : oneIdColumn)
    {
        if (!db::preparedStmt(query, ids.first, ids.second))
        {
            ShowErrorFmt("Failed to execute cleanup query: {}", query);
        }
    }

    for (const auto* query : twoIdColumns)
    {
        if (!db::preparedStmt(query, ids.first, ids.second, ids.first, ids.second))
        {
            ShowErrorFmt("Failed to execute cleanup query: {}", query);
        }
    }

    for (const auto& tableName : charIdTables)
    {
        const auto query = std::format("DELETE FROM {} WHERE charid BETWEEN ? AND ?", tableName);
        if (!db::preparedStmt(query, ids.first, ids.second))
        {
            ShowErrorFmt("Failed to execute cleanup query: {}", query);
        }
    }
}

auto TestChar::create(const xi::ZoneId zoneId, const CharRace race) -> std::unique_ptr<TestChar>
{
    uint32_t accId  = 0;
    uint32_t charId = 0;

    // Each test character gets a content id (chars.accid) of its own, above any the lobby hands out
    auto rset = db::preparedStmt("SELECT max(accid) FROM chars");
    FOR_DB_SINGLE_RESULT(rset)
    {
        accId = std::max(MinTestAccId, rset->get<uint32>("max(accid)") + 1);
    }
    else
    {
        TestError("Unable to get max content ID");
        return nullptr;
    }

    rset = db::preparedStmt("SELECT max(charid) FROM chars");
    FOR_DB_SINGLE_RESULT(rset)
    {
        charId = std::max(MinTestCharId, rset->get<uint32>("max(charid)") + 1);
    }
    else
    {
        TestError("Unable to get max character ID");
        return nullptr;
    }

    const auto charName = std::format("T{}", charId);

    // The rows the lobby API makes for a new character (lobby_api.cpp, createCharacter). The content id doubles as
    // the test character's POL ID, so each one is an account of its own.
    const auto created = db::transaction(
        [&]()
        {
            const auto run = [](const char* query, auto&&... args)
            {
                if (!db::preparedStmt(query, std::forward<decltype(args)>(args)...))
                {
                    throw std::runtime_error(std::format("query failed: {}", query));
                }
            };

            run("INSERT INTO chars(charid,accid,pol_id,charname,pos_zone,nation) VALUES(?, ?, ?, ?, ?, ?)",
                charId,
                accId,
                static_cast<uint64>(accId),
                charName,
                static_cast<uint16>(zoneId),
                static_cast<uint8>(NATION_SANDORIA));
            run("INSERT INTO char_look(charid,face,race,size) VALUES(?, ?, ?, ?)",
                charId,
                static_cast<uint8>(CharFace::Face1A),
                static_cast<uint8>(race),
                static_cast<uint16>(CharSize::Small));
            run("INSERT INTO char_stats(charid,mjob) VALUES(?, ?)", charId, static_cast<uint8>(xi::Job::WAR));
            run("INSERT INTO char_exp(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_flags(charid) VALUES(?) ON DUPLICATE KEY UPDATE disconnecting = disconnecting", charId);
            run("INSERT INTO char_jobs(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_points(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_unlocks(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_profile(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("INSERT INTO char_storage(charid) VALUES(?) ON DUPLICATE KEY UPDATE charid = charid", charId);
            run("DELETE FROM char_inventory WHERE charid = ?", charId);
            run("INSERT INTO char_inventory(charid) VALUES(?)", charId);
            if (settings::get<bool>("main.NEW_CHARACTER_CUTSCENE"))
            {
                run("INSERT INTO char_vars(charid, varname, value) VALUES(?, ?, ?)", charId, "HQuest[newCharacterCS]notSeen", 1);
            }
        });
    if (!created)
    {
        TestError("Unable to create test character");
        return nullptr;
    }

    auto testChar        = std::make_unique<TestChar>();
    testChar->accountId_ = accId;
    testChar->charId_    = charId;
    testChar->charName_  = charName;

    return testChar;
}

TestChar::~TestChar()
{
    TestChar::clean(this->charId_);
}

void TestChar::clearPackets() const
{
    if (session_)
    {
        session_->PChar->clearPacketList();
    }
}

void TestChar::setSession(MapSession* session)
{
    session_ = session;
}

auto TestChar::session() const -> MapSession*
{
    return session_;
}

auto TestChar::entity() const -> CCharEntity*
{
    if (session_)
    {
        return session_->PChar.get();
    }

    return nullptr;
}

void TestChar::setEntity(std::unique_ptr<CCharEntity> entity) const
{
    if (session_)
    {
        session_->charID          = entity->id;
        session_->PChar           = std::move(entity);
        session_->PChar->PSession = session();
    }
}

void TestChar::setBlowfish(const BLOWFISH b) const
{
    session_->blowfish.status = b;
}

auto TestChar::charId() const -> uint32_t
{
    return charId_;
}

auto TestChar::accountId() const -> uint32_t
{
    return accountId_;
}

void TestChar::setIpp(const IPP ipp)
{
    ipp_ = ipp;
}

auto TestChar::ipp() const -> IPP
{
    return ipp_;
}
