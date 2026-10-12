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

// Maps server ids (from today's DATs) to the ids in the console client's own DATs, and back.
// Answers come from the id maps of the active profile, so each console build gets its own numbering.
// Only translators call these. PC clients are never affected.
// The maps are built by FFXI-PS2 tools/item_map.py, event_map.py and entity_map.py.
namespace console::ids
{

// Maps today's zone dialog line to the client's.
// Returns nullopt if the line, the zone or the map is missing.
auto dialog(uint16 zoneId, uint16 messageId) -> std::optional<uint16>;

// Returns true if the client's install has this zone's dialog.
// A zone missing here is missing from the install, and the client can't load it.
auto hasZone(uint16 zoneId) -> bool;

// Maps the MesNum of 0x027, 0x02A and 0x036.
// Bit 15 is a display flag. The low 15 bits are the dialog line.
auto mesNum(uint16 zoneId, uint16 mesNum) -> std::optional<uint16>;

// Returns true if the client's install has this item.
// Items added since show up as "." placeholders in the client's item DATs, so only ids with a real name count.
// Item 0 and gil are always known. Without an item map every item is known.
auto hasItem(uint16 itemId) -> bool;

// Returns true if one of the zone's event files in the client's install has this event.
// A missing event runs on nothing, so the client never ends it and the player stays locked.
// Event numbers did not shift between versions. Without an event map every event is known.
auto hasEvent(uint16 zoneId, uint16 eventId) -> bool;

// Today's Home Point #1 event.
constexpr uint16 kHomePointEvent = 8700;

// Returns the zone's 2010 Home Point Yes/No event (result 0 is Yes, 1 is No).
// Returns nullopt if the client has event 8700 or the zone has no Home Point.
auto homePointEvent(uint16 zoneId) -> std::optional<uint16>;

// Static entities are NPCs, mobs and doors: UniqueNo is 0x01000000 | zone << 12 | index, and ActIndex is the index.
// Entities were added to the zones' name lists since 2010, so the indexes shifted.
// Players, pets and trusts (ActIndex >= 0x400) are not static and pass unchanged.
// An entity added after 2010 has no client index. These return nullopt and the packet should be dropped.
auto isStatic(uint32 uniqueNo) -> bool;

// Today's ids to the client's, for packets sent to it.
auto uniqueNoToClient(uint32 uniqueNo) -> std::optional<uint32>;
auto actIndexToClient(uint16 zoneId, uint16 actIndex) -> std::optional<uint16>;

// The client's ids to today's, for packets it sends.
auto uniqueNoFromClient(uint32 uniqueNo) -> std::optional<uint32>;
auto actIndexFromClient(uint16 zoneId, uint16 actIndex) -> std::optional<uint16>;

} // namespace console::ids
