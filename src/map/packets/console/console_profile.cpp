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

#include "console_profile.h"
#include "console_entities.h"
#include "console_groups.h"
#include "console_items.h"

namespace console
{

namespace
{

// Every id gcZoneRecvCallBack / gcZoneRecvCallBack2 registers in the 2010 client (FFXI-PS2 tools/handlers.md).
// Anything else is dropped: ids >= 0x110 would crash it (table B is indexed without a bound), and unknown
// ids below that only log an error on the client.
constexpr uint16 kHandled[] = {
    0x005, 0x006, 0x008, 0x009, 0x00A, 0x00B, 0x00D, 0x00E, 0x011, 0x012, 0x013, 0x014, 0x016, 0x017, 0x01B, 0x01C,
    0x01D, 0x01E, 0x01F, 0x020, 0x021, 0x022, 0x023, 0x024, 0x025, 0x026, 0x027, 0x028, 0x029, 0x02A, 0x02B, 0x02C,
    0x02D, 0x02E, 0x02F, 0x030, 0x031, 0x032, 0x033, 0x034, 0x036, 0x037, 0x038, 0x039, 0x03A, 0x03B, 0x03C, 0x03D,
    0x03E, 0x03F, 0x041, 0x042, 0x043, 0x044, 0x047, 0x04B, 0x04C, 0x04D, 0x04F, 0x050, 0x051, 0x052, 0x053, 0x054,
    0x055, 0x056, 0x057, 0x058, 0x059, 0x05A, 0x05B, 0x05C, 0x05D, 0x05E, 0x05F, 0x060, 0x061, 0x062, 0x063, 0x064,
    0x065, 0x067, 0x069, 0x06F, 0x070, 0x071, 0x073, 0x074, 0x078, 0x079, 0x081, 0x082, 0x083, 0x084, 0x085, 0x086,
    0x08C, 0x096, 0x097, 0x098, 0x099, 0x09A, 0x09B, 0x09C, 0x09D, 0x09E, 0x0A0, 0x0AA, 0x0AB, 0x0AC, 0x0AD, 0x0B4,
    0x0B5, 0x0B6, 0x0B7, 0x0BF, 0x0C8, 0x0C9, 0x0CA, 0x0CC, 0x0D2, 0x0D3, 0x0DC, 0x0DD, 0x0DE, 0x0DF, 0x0E0, 0x0E1,
    0x0E2, 0x0E6, 0x0F4, 0x0F5, 0x0F6, 0x0F9, 0x0FA, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10A, 0x10E, 0x10F,
};

} // namespace

auto makeProfile20100904() -> std::unique_ptr<compat::Profile>
{
    auto p = std::make_unique<compat::Profile>("ps2-20100904", compat::Platform::PS2, "retail PS2 client, SCUS-97266 patch 20100904_2");

    // This is the only PS2 profile, so every PS2 client gets it.

    p->handles(kHandled, std::size(kHandled));
    registerGroups(*p);

    // On every packet the per-id translators kept, in the 2010 layout: items the install lacks, then
    // static entity ids (console_items.cpp, console_entities.cpp)
    p->s2cPass(filterItemsS2C);
    p->s2cPass(remapEntitiesS2C);
    p->c2sPass(remapEntitiesC2S);
    return p;
}

} // namespace console
