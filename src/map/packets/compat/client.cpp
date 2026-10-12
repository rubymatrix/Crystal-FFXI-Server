/*
===========================================================================

  Copyright (c) 2026 Project Crystal Dev Team

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

#include "client.h"
#include "profile.h"

#include <fmt/format.h>

#include <cstring>

namespace compat
{

namespace
{

auto startsWith(const char* tag, const char* prefix) -> bool
{
    return std::strncmp(tag, prefix, std::strlen(prefix)) == 0;
}

auto platformOf(const char* tag) -> Platform
{
    // The PC client names itself "WIN"; everything else is a console
    if (std::strcmp(tag, "WIN") == 0)
    {
        return Platform::PC;
    }
    if (std::strcmp(tag, "PS2") == 0)
    {
        return Platform::PS2;
    }
    if (startsWith(tag, "X"))
    {
        return Platform::Xbox360;
    }
    return Platform::Unknown;
}

} // namespace

auto platformName(const Platform platform) -> const char*
{
    switch (platform)
    {
        case Platform::PC:
            return "PC";
        case Platform::PS2:
            return "PS2";
        case Platform::Xbox360:
            return "Xbox 360";
        case Platform::Unknown:
        default:
            return "unknown";
    }
}

auto ClientInfo::describe() const -> std::string
{
    const auto base = platform == Platform::Unknown ? fmt::format("unknown ({:?})", tag) : std::string(platformName(platform));
    return fmt::format("{} {}Ver {:08X} expansions {:X} ({})", base, build.empty() ? "" : build + " ", version, expansions,
                       profile ? profile->name() : "today's layout");
}

auto identify(const uint8* sPlatform, const uint32 version, const std::string& build, const uint32 expansions) -> ClientInfo
{
    ClientInfo info;
    std::memcpy(info.tag, sPlatform, 4);
    info.tag[4] = '\0';

    // "PS2" is padded with a NUL or a space
    for (int i = 3; i >= 0 && (info.tag[i] == ' ' || info.tag[i] == '\0'); --i)
    {
        info.tag[i] = '\0';
    }

    info.platform = platformOf(info.tag);
    info.version    = version;
    info.build      = build;
    info.expansions = expansions;
    info.profile    = findProfile(info.platform, version, build);
    return info;
}

} // namespace compat
