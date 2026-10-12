/*
===========================================================================

  Copyright (c) 2026 LandSandBoat Dev Teams

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

#include "profile.h"

#include "packets/console/console_profile.h"

#include "common/logging.h"

#include "map_session.h"
#include "packets/basic.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

namespace compat
{

namespace
{

// Every build that needs translation. Add a factory here for each new one.
auto buildProfiles() -> std::vector<std::unique_ptr<Profile>>
{
    std::vector<std::unique_ptr<Profile>> out;
    out.push_back(console::makeProfile20100904());

    // res/compat/<profile>/versions.txt: the 0x00A Ver values of that build, one per line (hex with 0x, or
    // decimal; # starts a comment). The login logs every client's Ver, so a new build is pinned without a
    // rebuild.
    for (auto& p : out)
    {
        std::ifstream f("res/compat/" + p->name() + "/versions.txt");
        std::string   line;
        while (std::getline(f, line))
        {
            if (const auto hash = line.find('#'); hash != std::string::npos)
            {
                line.resize(hash);
            }

            const auto b = line.find_first_not_of(" \t\r");
            if (b == std::string::npos)
            {
                continue;
            }
            auto       entry = line.substr(b);
            const auto e     = entry.find_last_not_of(" \t\r");
            entry.resize(e + 1);

            // a whole number (0x... or decimal) is a Ver; anything else ("20100904_2") a lobby version string
            size_t used = 0;
            try
            {
                const auto value = std::stoul(entry, &used, 0);
                if (used == entry.size())
                {
                    p->addVersion(static_cast<uint32>(value));
                    continue;
                }
            }
            catch (const std::exception&)
            {
            }
            p->addBuild(entry);
        }
        if (!p->versions().empty() || !p->builds().empty())
        {
            ShowInfoFmt("compat: profile {} serves {} version string(s), {} Ver value(s)", p->name(), p->builds().size(), p->versions().size());
        }
    }
    return out;
}

thread_local const Profile* tActive = nullptr;

// Sets the active profile for the duration of one translation
struct ActiveScope
{
    const Profile* previous;

    explicit ActiveScope(const Profile* profile)
    : previous(std::exchange(tActive, profile))
    {
    }

    ~ActiveScope()
    {
        tActive = previous;
    }

    ActiveScope(const ActiveScope&)                    = delete;
    ActiveScope(ActiveScope&&)                         = delete;
    auto operator=(const ActiveScope&) -> ActiveScope& = delete;
    auto operator=(ActiveScope&&) -> ActiveScope&      = delete;
};

// send_parse runs on the map's network thread; translators and the send loop share it.
thread_local std::vector<std::vector<uint8>> tEmitted;

auto resultName(const Result r) -> const char*
{
    return r == Result::Drop ? "drop" : r == Result::Rewritten ? "rewritten"
                                                               : "pass";
}

} // namespace

auto combine(const Result a, const Result b) -> Result
{
    if (a == Result::Drop || b == Result::Drop)
    {
        return Result::Drop;
    }
    return (a == Result::Rewritten || b == Result::Rewritten) ? Result::Rewritten : Result::Pass;
}

Profile::Profile(std::string name, const Platform platform, std::string description)
: name_(std::move(name))
, platform_(platform)
, description_(std::move(description))
{
}

void Profile::addVersion(const uint32 version)
{
    versions_.push_back(version);
}

void Profile::addBuild(std::string build)
{
    builds_.push_back(std::move(build));
}

void Profile::s2c(const uint16 id, const Translator fn)
{
    s2c_[id & 0x1FF] = fn;
}

void Profile::c2s(const uint16 id, const Translator fn)
{
    c2s_[id & 0x1FF] = fn;
}

void Profile::handles(const uint16* ids, const size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        handled_[ids[i] & 0x1FF] = true;
    }
}

void Profile::s2cPass(const Translator fn)
{
    s2cPasses_.push_back(fn);
}

void Profile::c2sPass(const Translator fn)
{
    c2sPasses_.push_back(fn);
}

auto Profile::clientHandles(const uint16 id) const -> bool
{
    return id < kIdCount && handled_[id];
}

auto Profile::maps() const -> const IdMaps&
{
    if (!maps_)
    {
        maps_ = std::make_unique<IdMaps>("res/compat/" + name_);
    }
    return *maps_;
}

auto Profile::translateS2C(MapSession* PSession, CBasicPacket& packet) const -> Result
{
    auto result = Result::Pass;

    // A translator may re-id a packet the client knows under another id (0x068 pet sync is the 2010
    // PS2 client's 0x067 sub-type 4), so it runs before the unhandled-id drop, checked on the result.
    if (const auto fn = s2c_[packet.getType() & 0x1FF])
    {
        result = fn(PSession, packet);
    }

    if (result == Result::Drop || !clientHandles(packet.getType()))
    {
        return Result::Drop;
    }

    for (const auto pass : s2cPasses_)
    {
        result = combine(result, pass(PSession, packet));
        if (result == Result::Drop)
        {
            return Result::Drop;
        }
    }
    return result;
}

auto Profile::translateC2S(MapSession* PSession, CBasicPacket& packet) const -> Result
{
    auto result = Result::Pass;

    if (const auto fn = c2s_[packet.getType() & 0x1FF])
    {
        result = fn(PSession, packet);
    }

    for (const auto pass : c2sPasses_)
    {
        if (result == Result::Drop)
        {
            return Result::Drop;
        }
        result = combine(result, pass(PSession, packet));
    }
    return result;
}

auto profiles() -> const std::vector<std::unique_ptr<Profile>>&
{
    static const auto all = buildProfiles();
    return all;
}

auto findProfile(const Platform platform, const uint32 version, const std::string& build) -> const Profile*
{
    const Profile* byVersion = nullptr;
    const Profile* fallback  = nullptr;
    for (const auto& p : profiles())
    {
        if (p->platform() != platform)
        {
            continue;
        }
        const auto& bs = p->builds();
        if (!build.empty() && std::find(bs.begin(), bs.end(), build) != bs.end())
        {
            return p.get();
        }
        const auto& vs = p->versions();
        if (byVersion == nullptr && std::find(vs.begin(), vs.end(), version) != vs.end())
        {
            byVersion = p.get();
        }
        if (fallback == nullptr)
        {
            fallback = p.get();
        }
    }
    return byVersion ? byVersion : fallback;
}

auto zoneAvailable(const MapSession* PSession, const uint16 zoneId) -> bool
{
    if (PSession == nullptr || PSession->client.profile == nullptr || zoneId >= 1000)
    {
        return true;
    }
    return PSession->client.profile->maps().hasZoneData(zoneId);
}

auto active() -> const Profile&
{
    // Translators only run inside translateS2C / translateC2S, which set it
    return *tActive;
}

auto tracing() -> bool
{
    static const bool on = []
    {
        for (const auto* name : { "XI_COMPAT_TRACE", "XI_PS2_TRACE" })
        {
            const char* v = std::getenv(name);
            if (v != nullptr && *v != '\0' && *v != '0')
            {
                return true;
            }
        }
        return false;
    }();
    return on;
}

auto translateS2C(MapSession* PSession, CBasicPacket& packet) -> Result
{
    const auto* profile = PSession->client.profile;
    if (profile == nullptr)
    {
        return Result::Pass;
    }

    ActiveScope scope(profile);
    const auto  id     = packet.getType();
    const auto  size   = packet.getSize();
    const auto  result = profile->translateS2C(PSession, packet);
    if (tracing())
    {
        ShowInfoFmt("{} trace: s2c {:03X} size {:03X} seq {} -> {} {:03X} size {:03X}", profile->name(), id, size, packet.getSequence(), resultName(result), packet.getType(), result == Result::Drop ? 0 : packet.getSize());
    }
    return result;
}

auto translateC2S(MapSession* PSession, CBasicPacket& packet) -> Result
{
    const auto* profile = PSession->client.profile;
    if (profile == nullptr)
    {
        return Result::Pass;
    }

    ActiveScope scope(profile);
    const auto  id     = packet.getType();
    const auto  size   = packet.getSize();
    const auto  result = profile->translateC2S(PSession, packet);
    if (tracing() && id != 0x015) // position: several a second
    {
        ShowInfoFmt("{} trace: c2s {:03X} size {:03X} -> {}", profile->name(), id, size, resultName(result));
    }
    return result;
}

void emitS2C(const uint8* packet, const size_t size)
{
    tEmitted.emplace_back(packet, packet + size);
}

auto takeEmittedS2C() -> std::vector<std::vector<uint8>>
{
    return std::exchange(tEmitted, {});
}

} // namespace compat
