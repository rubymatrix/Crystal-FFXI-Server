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

#pragma once

#include "client.h"
#include "id_maps.h"

#include "common/cbasetypes.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// Translation profiles: one per client build whose packets or data differ from today's.
//
// A profile holds everything that build needs: a translator per packet id in each direction, the ids
// its client has a handler for (anything else is dropped), passes that run on every packet after the
// per-id translators, and its id maps (compat/id_maps.h) from res/compat/<name>/.
//
// Profiles are built once, by the factories listed in profile.cpp. A build is matched on platform and
// on the version string the lobby stored for the session, then on the Ver of its 0x00A (both listed in
// res/compat/<profile>/versions.txt, or addBuild / addVersion in its factory); failing both, the first
// profile registered for the platform serves it. For a session with a profile, send_parse calls translateS2C on every outgoing packet
// and parse calls translateC2S on every incoming one before it is dispatched.

class CBasicPacket;
struct MapSession;

namespace compat
{

enum class Result : uint8
{
    Pass,      // send / dispatch as is
    Rewritten, // the translator changed the packet in place (size included)
    Drop,      // do not send / do not dispatch
};

using Translator = Result (*)(MapSession* PSession, CBasicPacket& packet);

auto combine(Result a, Result b) -> Result;

class Profile
{
public:
    static constexpr uint16 kIdCount = 0x200;

    Profile(std::string name, Platform platform, std::string description);

    auto name() const -> const std::string&
    {
        return name_;
    }

    auto platform() const -> Platform
    {
        return platform_;
    }

    auto description() const -> const std::string&
    {
        return description_;
    }

    // 0x00A Ver values this profile serves; none: every build of its platform no other profile lists
    auto versions() const -> const std::vector<uint32>&
    {
        return versions_;
    }

    void addVersion(uint32 version);

    // Lobby version strings (the client's patch.ver, e.g. "20100904_2") this profile serves; checked before Ver
    auto builds() const -> const std::vector<std::string>&
    {
        return builds_;
    }

    void addBuild(std::string build);

    // Registration (profile factories only)
    void s2c(uint16 id, Translator fn);
    void c2s(uint16 id, Translator fn);
    void handles(const uint16* ids, size_t count); // ids the client registers a handler for
    void s2cPass(Translator fn);                   // after the per-id s2c translator, on every packet it kept
    void c2sPass(Translator fn);                   // after the per-id c2s translator

    // Does the client have a handler for this s2c id? Unhandled ids are dropped.
    auto clientHandles(uint16 id) const -> bool;

    // The build's id maps (res/compat/<name>/), loaded on first use
    auto maps() const -> const IdMaps&;

    auto translateS2C(MapSession* PSession, CBasicPacket& packet) const -> Result;
    auto translateC2S(MapSession* PSession, CBasicPacket& packet) const -> Result;

private:
    std::string                      name_;
    Platform                         platform_;
    std::string                      description_;
    std::vector<uint32>              versions_;
    std::vector<std::string>         builds_;
    std::array<Translator, kIdCount> s2c_{};
    std::array<Translator, kIdCount> c2s_{};
    std::array<bool, kIdCount>       handled_{};
    std::vector<Translator>          s2cPasses_;
    std::vector<Translator>          c2sPasses_;
    mutable std::unique_ptr<IdMaps>  maps_;
};

// Every registered profile
auto profiles() -> const std::vector<std::unique_ptr<Profile>>&;

// The profile for a platform, lobby version string and 0x00A Ver, or nullptr (today's layout)
auto findProfile(Platform platform, uint32 version, const std::string& build = {}) -> const Profile*;

// The profile whose translator is running on this thread (set by translateS2C / translateC2S), for
// helpers deep in a translator that need its id maps. Only valid inside a translation.
auto active() -> const Profile&;

// Can this session's client enter the zone? A console build whose install has no files for it (the 2010 PS2
// client and every zone past 2010; zones a later build lacks) would stand in an empty zone. Clients without a
// profile, and instanced zones (1000+), always can.
auto zoneAvailable(const MapSession* PSession, uint16 zoneId) -> bool;

// Translate one packet of a session that has a profile (PSession->client.profile)
auto translateS2C(MapSession* PSession, CBasicPacket& packet) -> Result;
auto translateC2S(MapSession* PSession, CBasicPacket& packet) -> Result;

// XI_COMPAT_TRACE=1 (or the older XI_PS2_TRACE=1) logs every translated packet (and resend)
auto tracing() -> bool;

// A translator that has to turn one of today's packets into two for its client queues the second one
// here. send_parse sends it right after the translated packet, in the same datagram and with the same
// sequence number. The packet must already be in the client's layout (it is not translated again).
void emitS2C(const uint8* packet, size_t size);
auto takeEmittedS2C() -> std::vector<std::vector<uint8>>;

} // namespace compat
