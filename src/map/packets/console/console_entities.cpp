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

#include "console_entities.h"
#include "console_ids.h"

#include "entities/char_entity.h"
#include "map_session.h"
#include "packets/basic.h"

#include <array>
#include <initializer_list>
#include <vector>

// Remaps static entity ids (NPCs, mobs, doors) between today's numbering and the 2010 client's.
// New entities were added to the zones' name lists since 2010, so the ids shifted.
// Field lists come from the "Ids to remap" sections in FFXI-PS2 docs/packets/*.md.

namespace console
{

namespace
{

enum class Kind : uint8
{
    UniqueNo, // 32-bit UniqueNo. The zone is part of the id.
    ActIndex, // 16-bit ActIndex, in the player's current zone.
};

struct Field
{
    uint8 offset;
    Kind  kind;
};

using Fields = std::vector<Field>;

constexpr auto U(const uint8 off) -> Field
{
    return { off, Kind::UniqueNo };
}

constexpr auto A(const uint8 off) -> Field
{
    return { off, Kind::ActIndex };
}

// Every packet field that names an entity, per packet id.
// Offsets are in the 2010 layout. For these packets that's the same as today's.
struct Tables
{
    std::array<Fields, 0x200> s2c;
    std::array<Fields, 0x200> c2s;

    Tables()
    {
        // server -> client
        s2c[0x00E] = { U(0x04), A(0x08) };                   // CHAR_NPC
        s2c[0x021] = { U(0x04), A(0x08) };                   // ITEM_TRADE_REQ
        s2c[0x022] = { U(0x04), A(0x0C) };                   // ITEM_TRADE_RES
        s2c[0x027] = { U(0x04), A(0x08) };                   // TALKNUMWORK2
        s2c[0x029] = { U(0x04), U(0x08), A(0x14), A(0x16) }; // BATTLE_MESSAGE
        s2c[0x02A] = { U(0x04), A(0x18) };                   // TALKNUMWORK
        s2c[0x02D] = { U(0x04), U(0x08), A(0x0C), A(0x0E) }; // BATTLE_MESSAGE2
        s2c[0x032] = { U(0x04), A(0x08) };                   // EVENT
        s2c[0x033] = { U(0x04), A(0x08) };                   // EVENTSTR
        s2c[0x034] = { U(0x04), A(0x28) };                   // EVENTNUM
        s2c[0x036] = { U(0x04), A(0x08) };                   // TALKNUM
        s2c[0x038] = { U(0x04), U(0x08), A(0x10), A(0x12) }; // SCHEDULOR
        s2c[0x039] = { U(0x04), U(0x08), A(0x10), A(0x12) }; // MAPSCHEDULOR
        s2c[0x03A] = { U(0x04), U(0x08), A(0x0C), A(0x0E) }; // MAGICSCHEDULOR
        s2c[0x043] = { U(0x04), A(0x08) };                   // TALKNUMNAME
        s2c[0x058] = { U(0x04), U(0x08), A(0x0C) };          // ASSIST
        s2c[0x05A] = { U(0x04), U(0x08), A(0x0C), A(0x0E) }; // MOTIONMES
        s2c[0x05B] = { U(0x10), A(0x14) };                   // WPOS
        s2c[0x065] = { U(0x10), A(0x14) };                   // WPOS2
        s2c[0x0D2] = { U(0x08), A(0x12) };                   // TROPHY_LIST: the mob that dropped the item
        s2c[0x0F4] = { A(0x04) };                            // TRACKING_LIST: widescan
        s2c[0x0F5] = { A(0x12) };                            // TRACKING_POS

        // client -> server
        c2s[0x015] = { A(0x16) };          // POS: face target
        c2s[0x016] = { A(0x04) };          // CHARREQ
        c2s[0x017] = { A(0x04) };          // CHARREQ2
        c2s[0x01A] = { U(0x04), A(0x08) }; // ACTION
        c2s[0x01C] = { A(0x04) };
        c2s[0x036] = { U(0x04), A(0x3A) }; // ITEM_TRANSFER: trade to an NPC
        c2s[0x037] = { U(0x04), A(0x0C) }; // ITEM_USE
        c2s[0x05B] = { U(0x04), A(0x0C) }; // EVENTEND
        c2s[0x05C] = { U(0x10), A(0x1C) }; // EVENTENDXZY
        c2s[0x05D] = { U(0x04), A(0x08) }; // MOTION
        c2s[0x060] = { U(0x04), A(0x08) }; // PASSWARDS
        c2s[0x06E] = { U(0x04), A(0x08) }; // GROUP_SOLICIT_REQ
        c2s[0x0DD] = { U(0x04), A(0x08) }; // EQUIP_INSPECT
        c2s[0x0F5] = { A(0x04) };          // TRACKING_START
        c2s[0x105] = { U(0x04), A(0x08) }; // BAZAAR_LIST
    }
};

auto tables() -> const Tables&
{
    static const Tables t;
    return t;
}

// Remaps each listed field of one packet.
// An entity with no match in the other numbering drops the packet. Any other index would name a different NPC.
auto apply(const Fields& fields, MapSession* PSession, CBasicPacket& packet, const bool toClient) -> Result
{
    if (fields.empty())
    {
        return Result::Pass;
    }
    if (!PSession->PChar)
    {
        return Result::Drop;
    }

    const auto zone    = static_cast<uint16>(PSession->PChar->getZone());
    const auto size    = packet.getSize();
    bool       changed = false;

    for (const auto& f : fields)
    {
        if (f.kind == Kind::UniqueNo)
        {
            if (f.offset + 4u > size)
            {
                continue;
            }
            const auto was = packet.ref<uint32>(f.offset);
            const auto now = toClient ? ids::uniqueNoToClient(was) : ids::uniqueNoFromClient(was);
            if (!now)
            {
                return Result::Drop;
            }
            changed |= *now != was;
            packet.ref<uint32>(f.offset) = *now;
        }
        else
        {
            if (f.offset + 2u > size)
            {
                continue;
            }
            const auto was = packet.ref<uint16>(f.offset);
            if (was == 0)
            {
                continue; // 0 means no entity.
            }
            const auto now = toClient ? ids::actIndexToClient(zone, was) : ids::actIndexFromClient(zone, was);
            if (!now)
            {
                return Result::Drop;
            }
            changed |= *now != was;
            packet.ref<uint16>(f.offset) = *now;
        }
    }

    return changed ? Result::Rewritten : Result::Pass;
}

} // namespace

auto remapEntitiesS2C(MapSession* PSession, CBasicPacket& packet) -> Result
{
    return apply(tables().s2c[packet.getType()], PSession, packet, true);
}

auto remapEntitiesC2S(MapSession* PSession, CBasicPacket& packet) -> Result
{
    return apply(tables().c2s[packet.getType()], PSession, packet, false);
}

} // namespace console
