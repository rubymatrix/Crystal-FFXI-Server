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

#include "id_maps.h"

#include "common/logging.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

namespace compat
{

namespace
{

constexpr uint16 kNone              = 0xFFFF;
constexpr uint16 kFirstDynamicIndex = 0x400; // players from here, then pets/trusts

// A per-zone table: 8-byte magic, u32 zones, then per zone u16 zone, u32 n, u16 values[n].
// Returns false (with a warning) when the file is missing or not what it should be.
auto readZoneTable(const std::string& path, const char* magicExpected, std::unordered_map<uint16, std::vector<uint16>>& out) -> bool
{
    std::ifstream f(path, std::ios::binary);
    char          magic[8] = {};
    uint32        count    = 0;
    if (!f.read(magic, sizeof(magic)) || std::memcmp(magic, magicExpected, 8) != 0 || !f.read(reinterpret_cast<char*>(&count), 4))
    {
        ShowWarningFmt("compat: {} missing or invalid; its ids are not remapped", path);
        return false;
    }
    for (uint32 i = 0; i < count; ++i)
    {
        uint16 zone = 0;
        uint32 n    = 0;
        if (!f.read(reinterpret_cast<char*>(&zone), 2) || !f.read(reinterpret_cast<char*>(&n), 4) || n > 0x10000)
        {
            ShowWarningFmt("compat: {} truncated after {} zones", path, i);
            return true;
        }
        auto& values = out[zone];
        values.resize(n);
        f.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(n) * 2);
    }
    ShowInfoFmt("compat: {}: {} zones", path, count);
    return true;
}

} // namespace

IdMaps::IdMaps(std::string dir)
: dir_(std::move(dir))
{
    readZoneTable(dir_ + "/dialog_map.bin", "PS2DLG01", dialog_);

    if (readZoneTable(dir_ + "/entity_map.bin", "PS2ENT01", entity_))
    {
        for (const auto& [zone, map] : entity_)
        {
            auto& rev = entityReverse_[zone];
            for (uint32 k = 0; k < map.size(); ++k)
            {
                if (map[k] == kNone)
                {
                    continue;
                }
                if (rev.size() <= map[k])
                {
                    rev.resize(map[k] + 1, kNone);
                }
                if (rev[map[k]] == kNone)
                {
                    rev[map[k]] = static_cast<uint16>(k);
                }
            }
        }
    }

    readZoneTable(dir_ + "/events.bin", "PS2EVT01", events_);

    const auto    itemsPath = dir_ + "/items.bin";
    std::ifstream f(itemsPath, std::ios::binary);
    char          magic[8] = {};
    items_.resize(0x2000);
    if (!f.read(magic, sizeof(magic)) || std::memcmp(magic, "PS2ITM01", 8) != 0 ||
        !f.read(reinterpret_cast<char*>(items_.data()), static_cast<std::streamsize>(items_.size())))
    {
        ShowWarningFmt("compat: {} missing or invalid; items are not filtered", itemsPath);
        items_.clear();
    }
    else
    {
        ShowInfoFmt("compat: {} loaded", itemsPath);
    }
}

auto IdMaps::lookup(const ZoneTable& maps, const uint16 zoneId, const uint16 id) -> std::optional<uint16>
{
    const auto it = maps.find(zoneId);
    if (it == maps.end() || id >= it->second.size() || it->second[id] == kNone)
    {
        return std::nullopt;
    }
    return it->second[id];
}

auto IdMaps::dialog(const uint16 zoneId, const uint16 messageId) const -> std::optional<uint16>
{
    return lookup(dialog_, zoneId, messageId);
}

auto IdMaps::hasZone(const uint16 zoneId) const -> bool
{
    return dialog_.contains(zoneId);
}

auto IdMaps::hasZoneData(const uint16 zoneId) const -> bool
{
    if (dialog_.empty() && entity_.empty())
    {
        return true;
    }
    return dialog_.contains(zoneId) || entity_.contains(zoneId);
}

auto IdMaps::mesNum(const uint16 zoneId, const uint16 mesNum) const -> std::optional<uint16>
{
    const auto line = dialog(zoneId, mesNum & 0x7FFF);
    if (!line || *line > 0x7FFF)
    {
        return std::nullopt;
    }
    return static_cast<uint16>((mesNum & 0x8000) | *line);
}

auto IdMaps::hasItem(const uint16 itemId) const -> bool
{
    if (itemId == 0 || itemId == 0xFFFF || items_.empty())
    {
        return true;
    }
    return (items_[itemId >> 3] & (1u << (itemId & 7))) != 0;
}

auto IdMaps::hasEvent(const uint16 zoneId, const uint16 eventId) const -> bool
{
    if (events_.empty())
    {
        return true;
    }
    const auto it = events_.find(zoneId);
    return it != events_.end() && std::binary_search(it->second.begin(), it->second.end(), eventId);
}

auto IdMaps::isStatic(const uint32 uniqueNo) -> bool
{
    return (uniqueNo >> 24) == 0x01 && (uniqueNo & 0xFFF) < kFirstDynamicIndex;
}

auto IdMaps::remapUniqueNo(const uint32 uniqueNo, const bool toOld) const -> std::optional<uint32>
{
    if (!isStatic(uniqueNo))
    {
        return uniqueNo;
    }
    const auto zone  = static_cast<uint16>((uniqueNo >> 12) & 0xFFF);
    const auto index = static_cast<uint16>(uniqueNo & 0xFFF);
    const auto other = toOld ? actIndexToOld(zone, index) : actIndexFromOld(zone, index);
    if (!other)
    {
        return std::nullopt;
    }
    return (uniqueNo & ~0xFFFu) | *other;
}

auto IdMaps::uniqueNoToOld(const uint32 uniqueNo) const -> std::optional<uint32>
{
    return remapUniqueNo(uniqueNo, true);
}

auto IdMaps::uniqueNoFromOld(const uint32 uniqueNo) const -> std::optional<uint32>
{
    return remapUniqueNo(uniqueNo, false);
}

auto IdMaps::actIndexToOld(const uint16 zoneId, const uint16 actIndex) const -> std::optional<uint16>
{
    if (actIndex >= kFirstDynamicIndex)
    {
        return actIndex;
    }
    return lookup(entity_, zoneId, actIndex);
}

auto IdMaps::actIndexFromOld(const uint16 zoneId, const uint16 actIndex) const -> std::optional<uint16>
{
    if (actIndex >= kFirstDynamicIndex)
    {
        return actIndex;
    }
    return lookup(entityReverse_, zoneId, actIndex);
}

} // namespace compat
