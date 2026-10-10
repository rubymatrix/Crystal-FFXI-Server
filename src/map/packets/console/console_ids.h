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

#include "common/cbasetypes.h"

#include <optional>

// Id remapping for the PS2 client's data. The server's ids come from today's DATs; the PS2 client
// indexes its own, where most tables have shifted. Only translators call these, and they answer from
// the id maps of the profile being translated for (compat::active().maps(), compat/id_maps.h), so each
// PS2 build gets its own numbering and PC clients are never affected.
namespace console::ids
{

// Zone dialog line: today's index -> the 2010 index, or nullopt if the line did not exist in 2010
// (or the zone is not in the PS2 install, or the map is missing).
auto dialog(uint16 zoneId, uint16 messageId) -> std::optional<uint16>;

// Does the PS2 install have this zone's dialog at all? (A zone missing here is missing from the
// install: the client cannot load it.)
auto hasZone(uint16 zoneId) -> bool;

// MesNum in 0x027/0x02A/0x036: bit 15 is a display flag, the low 15 bits the dialog line.
auto mesNum(uint16 zoneId, uint16 mesNum) -> std::optional<uint16>;

// Does the 2010 install have this item? Its item DATs hold a record for every id of the ranges they
// cover, and items added since are "." placeholders there, so an id counts only with a real name
// (tools/item_map.py -> items.bin). Item 0 (no item) and gil are always known. Without the
// map every item is known (nothing is filtered).
auto hasItem(uint16 itemId) -> bool;

// Does any of the zone's 2010 event files carry this event number? An event it does not have runs on
// no entity of the client, which then never sends its end (the player stays locked). Event numbers
// themselves did not shift between versions. (tools/event_map.py -> events.bin.) Without the
// map every event is known.
auto hasEvent(uint16 zoneId, uint16 eventId) -> bool;

// Home Point #1 (8700): 2010 zone-specific Yes/No event (result 0 Yes, 1 No), or nullopt if 8700 exists or no HP.
constexpr uint16 kHomePointEvent = 8700;
auto homePointEvent(uint16 zoneId) -> std::optional<uint16>;

// Static entities (NPCs, mobs, doors: UniqueNo 0x01000000 | zone << 12 | index, ActIndex = index).
// The zone's name list in the 2010 DATs has entities inserted since, so indices shift. The map is
// aligned on the names (tools/entity_map.py -> entity_map.bin). Anything that is not a static
// entity (players, pets, trusts: ActIndex >= 0x400) passes unchanged. An entity added after 2010 has
// no 2010 index: nullopt, and the packet naming it should be dropped.
auto isStatic(uint32 uniqueNo) -> bool;

// today -> 2010 (packets to the PS2 client)
auto uniqueNoToClient(uint32 uniqueNo) -> std::optional<uint32>;
auto actIndexToClient(uint16 zoneId, uint16 actIndex) -> std::optional<uint16>;

// 2010 -> today (packets from the PS2 client)
auto uniqueNoFromClient(uint32 uniqueNo) -> std::optional<uint32>;
auto actIndexFromClient(uint16 zoneId, uint16 actIndex) -> std::optional<uint16>;

} // namespace console::ids
