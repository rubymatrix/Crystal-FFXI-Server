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

#pragma once

#include "common/cbasetypes.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Id remapping for one older client build. The server's ids come from today's DATs; an older client
// indexes its own, where most tables have shifted. Each profile has its own set, generated on a machine
// with both installs (FFXI-PS2 repo: tools/dialog_map.py, entity_map.py, item_map.py, event_map.py) and
// read from res/compat/<profile>/:
//
//     dialog_map.bin  "PS2DLG01"  zone dialog line, today -> old
//     entity_map.bin  "PS2ENT01"  static entity index, today -> old (and back)
//     items.bin       "PS2ITM01"  bitset of the item ids the old install has
//     events.bin      "PS2EVT01"  event numbers each zone's old event files carry
//
// A missing file turns its remapping off (ids pass unchanged, nothing is filtered) with a warning.

namespace compat
{

class IdMaps
{
public:
    explicit IdMaps(std::string dir);

    // Zone dialog line: today's index -> the old index, or nullopt if the line did not exist then
    // (or the zone is not in the old install, or the map is missing).
    auto dialog(uint16 zoneId, uint16 messageId) const -> std::optional<uint16>;

    // Does the old install have this zone's dialog at all?
    auto hasZone(uint16 zoneId) const -> bool;

    // Does the old install have this zone at all (its dialog or its NPC list)? Without either map, every zone.
    auto hasZoneData(uint16 zoneId) const -> bool;

    // MesNum in 0x027/0x02A/0x036: bit 15 is a display flag, the low 15 bits the dialog line.
    auto mesNum(uint16 zoneId, uint16 mesNum) const -> std::optional<uint16>;

    // Does the old install have this item? (Item 0 and 0xFFFF always; everything without the map.)
    auto hasItem(uint16 itemId) const -> bool;

    // Does any of the zone's old event files carry this event number? (Everything without the map.)
    auto hasEvent(uint16 zoneId, uint16 eventId) const -> bool;

    // Static entities (UniqueNo 0x01000000 | zone << 12 | index, ActIndex = index < 0x400). Anything
    // else passes unchanged; an entity the old install lacks has no counterpart (nullopt).
    static auto isStatic(uint32 uniqueNo) -> bool;
    auto        uniqueNoToOld(uint32 uniqueNo) const -> std::optional<uint32>;
    auto        uniqueNoFromOld(uint32 uniqueNo) const -> std::optional<uint32>;
    auto        actIndexToOld(uint16 zoneId, uint16 actIndex) const -> std::optional<uint16>;
    auto        actIndexFromOld(uint16 zoneId, uint16 actIndex) const -> std::optional<uint16>;

private:
    using ZoneTable = std::unordered_map<uint16, std::vector<uint16>>;

    static auto lookup(const ZoneTable& maps, uint16 zoneId, uint16 id) -> std::optional<uint16>;
    auto        remapUniqueNo(uint32 uniqueNo, bool toOld) const -> std::optional<uint32>;

    std::string        dir_;
    ZoneTable          dialog_;
    ZoneTable          entity_;
    ZoneTable          entityReverse_;
    ZoneTable          events_; // sorted per zone
    std::vector<uint8> items_;  // 0x10000 bits, empty without the map
};

} // namespace compat
