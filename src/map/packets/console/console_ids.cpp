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

#include "console_ids.h"

#include "packets/compat/profile.h"

namespace console::ids
{

namespace
{

// The id maps of the profile currently being translated for.
auto maps() -> const compat::IdMaps&
{
    return compat::active().maps();
}

// Homepoints were changed December 2013. The Home Point event for each zone before that.
// It runs on today's Home Point #1.
// The events are in event file 5820 + zone.
struct HomePoint
{
    uint16 zone;
    uint16 event;
};

constexpr HomePoint kClassicHomePoints[] = {
    { 26, 250 },    // Tavnazian Safehold
    { 48, 2 },      // Al Zahbi
    { 50, 501 },    // Aht Urhgan Whitegate
    { 53, 238 },    // Nashmau
    { 80, 1 },      // Southern San d'Oria [S]
    { 87, 1 },      // Bastok Markets [S]
    { 94, 1 },      // Windurst Waters [S]
    { 230, 596 },   // Southern San d'Oria
    { 231, 599 },   // Northern San d'Oria
    { 232, 592 },   // Port San d'Oria
    { 234, 80 },    // Bastok Mines
    { 235, 1 },     // Bastok Markets
    { 236, 94 },    // Port Bastok
    { 238, 10010 }, // Windurst Waters
    { 240, 10001 }, // Port Windurst
    { 241, 10001 }, // Windurst Woods
    { 244, 10001 }, // Upper Jeuno
    { 245, 10001 }, // Lower Jeuno
    { 246, 10000 }, // Port Jeuno
    { 247, 1000 },  // Rabao
    { 248, 1000 },  // Selbina
    { 249, 1000 },  // Mhaura
    { 250, 10010 }, // Kazham
    { 252, 1000 },  // Norg
};

struct MapVendor
{
    uint16    zone;
    uint16    event;
    MapOption maps[8];
    uint8     count;
};

// Each map seller's 2010 menu: { zone, event, { { keyItem, slot }, ... }, count }, maps in menu order from option 2.
constexpr MapVendor kMapVendorLookup2010[] = {
    { 50, 563, { { 1856, 58 }, { 1857, 59 }, { 1858, 60 }, { 1874, 68 } }, 4 },                                                // Riyadahf (Aht Urhgan Whitegate)
    { 230, 595, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 397, 4 }, { 404, 5 }, { 405, 6 }, { 429, 7 } }, 8 },       // Violitte (Southern San d'Oria)
    { 231, 567, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 397, 4 }, { 404, 5 }, { 405, 6 }, { 429, 7 } }, 8 },       // Elesca (Northern San d'Oria)
    { 235, 210, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 395, 8 }, { 406, 9 }, { 407, 10 } }, 7 },                  // Karine (Bastok Markets)
    { 236, 115, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 395, 8 }, { 406, 9 }, { 407, 10 } }, 7 },                  // Rex (Port Bastok)
    { 240, 10000, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 408, 11 }, { 409, 12 }, { 399, 13 } }, 7 },              // Mhoji Roccoruh (Port Windurst)
    { 241, 10000, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 408, 11 }, { 409, 12 }, { 399, 13 } }, 7 },              // Pehki Machumaht (Windurst Woods)
    { 244, 10000, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 389, 16 }, { 400, 17 }, { 402, 18 }, { 413, 19 } }, 8 }, // Rusese (Upper Jeuno)
    { 245, 10000, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 389, 16 }, { 400, 17 }, { 402, 18 }, { 413, 19 } }, 8 }, // Promurouve (Lower Jeuno)
    { 247, 1006, { { 414, 32 }, { 427, 44 }, { 420, 37 } }, 3 },                                                               // Ashu Bolkhomo (Rabao)
    { 248, 500, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 } }, 4 },                                                       // Lombaria (Selbina)
    { 249, 500, { { 385, 0 }, { 386, 1 }, { 387, 2 }, { 388, 3 }, { 415, 14 }, { 430, 15 } }, 6 },                             // Ludwig (Mhaura)
};

} // namespace

auto dialog(const uint16 zoneId, const uint16 messageId) -> std::optional<uint16>
{
    return maps().dialog(zoneId, messageId);
}

auto hasZone(const uint16 zoneId) -> bool
{
    return maps().hasZone(zoneId);
}

auto hasItem(const uint16 itemId) -> bool
{
    return maps().hasItem(itemId);
}

auto hasEvent(const uint16 zoneId, const uint16 eventId) -> bool
{
    return maps().hasEvent(zoneId, eventId);
}

auto homePointEvent(const uint16 zoneId) -> std::optional<uint16>
{
    if (maps().hasEvent(zoneId, kHomePointEvent))
    {
        return std::nullopt;
    }

    for (const auto& hp : kClassicHomePoints)
    {
        if (hp.zone == zoneId && maps().hasEvent(zoneId, hp.event))
        {
            return hp.event;
        }
    }
    return std::nullopt;
}

auto isMapVendorEvent(const uint16 zoneId, const uint16 eventId) -> bool
{
    return !getMapVendorOptions(zoneId, eventId).empty();
}

auto getMapVendorOptions(const uint16 zoneId, const uint16 eventId) -> std::span<const MapOption>
{
    for (const auto& vendor : kMapVendorLookup2010)
    {
        if (vendor.zone == zoneId && vendor.event == eventId)
        {
            return { vendor.maps, vendor.count };
        }
    }
    return {};
}

auto mesNum(const uint16 zoneId, const uint16 mesNum) -> std::optional<uint16>
{
    return maps().mesNum(zoneId, mesNum);
}

auto isStatic(const uint32 uniqueNo) -> bool
{
    return compat::IdMaps::isStatic(uniqueNo);
}

auto uniqueNoToClient(const uint32 uniqueNo) -> std::optional<uint32>
{
    return maps().uniqueNoToOld(uniqueNo);
}

auto uniqueNoFromClient(const uint32 uniqueNo) -> std::optional<uint32>
{
    return maps().uniqueNoFromOld(uniqueNo);
}

auto actIndexToClient(const uint16 zoneId, const uint16 actIndex) -> std::optional<uint16>
{
    return maps().actIndexToOld(zoneId, actIndex);
}

auto actIndexFromClient(const uint16 zoneId, const uint16 actIndex) -> std::optional<uint16>
{
    return maps().actIndexFromOld(zoneId, actIndex);
}

} // namespace console::ids
