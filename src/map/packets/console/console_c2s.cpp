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

// Client packets from the 2010 PS2 client, rewritten into today's layout before the server handles them.
// Packets not listed here already match today's layout. The server rejects a packet of the wrong size,
// so every rewrite sets today's exact size. Layouts are in FFXI-PS2 docs/packets/c2s.md.

#include "console_groups.h"
#include "console_ids.h"

#include "entities/char_entity.h"
#include "event_info.h"
#include "map_session.h"

#include <algorithm>

namespace console
{

namespace
{

constexpr uint8 kInventory = 0; // LOC_INVENTORY
constexpr uint8 kMogSafe   = 1; // LOC_MOGSAFE

// Cuts a text packet down to today's maximum size and terminates it.
// The 2010 client allows up to 150 characters where today's packets hold 128.
auto clampText(CBasicPacket& packet, const size_t maxSize) -> Result
{
    if (packet.getSize() <= maxSize)
    {
        return Result::Pass;
    }

    packet.setSize(maxSize);
    packet.ref<uint8>(maxSize - 1) = 0;
    return Result::Rewritten;
}

// 0x01A - ACTION
// The 2010 client sends one action parameter. Today's packet has four, and the rest (the cast offset) is left zero.
auto action(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.getSize() >= 0x1C)
    {
        return Result::Pass;
    }

    Rewrite rw(packet);
    rw.clear(0x1C);
    rw.move(0x04, 0x04, 0x0C);
    return Result::Rewritten;
}

// 0x01B - FRIENDPASS
// The 2010 client pads this packet to 0x1C bytes. Only the parameter is kept, at today's size of 8.
auto friendPass(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 2);
    return Result::Rewritten;
}

// 0x01E - GM
// The 2010 client allows longer text. It's cut to today's maximum of 115 characters.
auto gm(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x78);
}

// 0x01F - GMCOMMAND
// The 2010 client allows longer text. It's cut to today's maximum.
auto gmCommand(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x88);
}

// 0x04E - AUC
// The 2010 client sends command 0x10 to clear a sale status flag. The server has no such command
// and would log a warning every time, so it's dropped.
auto auction(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x04) == 0x10)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x050 - EQUIP_SET
// The 2010 client only equips from the inventory and has no container field.
// The byte where today's container goes is leftover data, so it's set to the inventory.
auto equipSet(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 2);
    rw.out<uint8>(0x06, kInventory);
    return Result::Rewritten;
}

// 0x05B - EVENTEND
// The Home Point event has a different number on the 2010 client, so it's changed to today's.
// The 2010 client answers Yes with 0. That becomes 1 (set home point), anything else becomes 0.
auto eventEnd(MapSession* PSession, CBasicPacket& packet) -> Result
{
    auto* PChar = PSession->PChar.get();
    if (!PChar || !PChar->currentEvent || PChar->currentEvent->eventId != ids::kHomePointEvent)
    {
        return Result::Pass;
    }

    const auto old = ids::homePointEvent(packet.ref<uint16>(0x10));
    if (!old || packet.ref<uint16>(0x12) != *old)
    {
        return Result::Pass;
    }

    packet.ref<uint16>(0x12) = ids::kHomePointEvent;
    if (packet.ref<uint16>(0x0E) == 0)
    {
        packet.ref<uint32>(0x08) = packet.ref<uint32>(0x08) == 0 ? 1 : 0;
    }
    return Result::Rewritten;
}

// 0x061 - CLISTATUS
// The 2010 client sends only the header. Today's packet has one more field, sent as zero.
auto cliStatus(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    return Result::Rewritten;
}

// 0x077 - GROUP_CHANGE2
// The 2010 client doesn't terminate a 15-character name, so the byte after it is leftover data.
// The name and the two fields after it are copied into a clean packet.
auto groupChange2(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x18);
    rw.move(0x04, 0x04, 15);
    rw.move(0x14, 0x14, 2);
    return Result::Rewritten;
}

// 0x083 - SHOP_BUY
// The 2010 client never writes the shop number, and the server needs the slot byte at 0x0C to be 0.
// Only the item count and the shop item index are kept.
auto shopBuy(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x10);
    rw.move(0x04, 0x04, 4);
    rw.move(0x0A, 0x0A, 2);
    return Result::Rewritten;
}

// 0x0A0 - SWITCH_PROPOSAL
// The 2010 client allows longer text. It's cut to today's maximum.
auto switchProposal(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x88);
}

// 0x0A1 - SWITCH_VOTE
// The 2010 client can send a packet longer than today's maximum.
// It's cut to today's size, which holds a 15-character name with no terminator.
auto switchVote(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.getSize() <= 0x14)
    {
        return Result::Pass;
    }

    packet.setSize(0x14);
    return Result::Rewritten;
}

// 0x0AA - GUILD_BUY
// The 2010 client sends an inventory slot. Today's server picks the slot itself and wants 0 there.
auto guildBuy(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    packet.ref<uint8>(0x06) = 0;
    return Result::Rewritten;
}

// 0x0B5 - CHAT_STD
// The 2010 client allows longer text. It's cut to today's maximum.
auto chatStd(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x88);
}

// 0x0B6 - CHAT_NAME
// In the 2010 client's tell, the name and message start one byte earlier than today.
// They're moved, the first byte is set to 3 as today's server requires, and the message is cut to today's maximum.
auto chatName(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    constexpr size_t kMaxSize = 0x98; // 0x15 + 128, rounded up

    const size_t oldSize = packet.getSize();
    if (oldSize < 0x15)
    {
        return Result::Drop;
    }

    const size_t newSize = std::min<size_t>(oldSize + 1, kMaxSize);
    const size_t mesLen  = std::min<size_t>(oldSize - 0x14, newSize - 0x15);

    Rewrite rw(packet);
    rw.clear(newSize);
    rw.out<uint8>(0x04, 3);
    rw.out<uint8>(0x05, 0);
    rw.move(0x05, 0x06, 15);
    rw.move(0x14, 0x15, mesLen);
    if (oldSize + 1 > kMaxSize)
    {
        rw.out<uint8>(kMaxSize - 1, 0);
    }
    return Result::Rewritten;
}

// 0x0C3 - GROUP_COMLINK_MAKE
// The 2010 client has one linkshell and sends no linkshell number.
// The byte where today's number goes is leftover data, so it's set to linkshell 1.
auto groupComlinkMake(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 1);
    rw.out<uint8>(0x05, 1);
    return Result::Rewritten;
}

// 0x0C4 - GROUP_COMLINK_ACTIVE
// The 2010 client has no container field and one linkshell. The fields after the item index move
// to today's offsets, the container is set to the inventory and the linkshell to 1.
auto groupComlinkActive(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x1C);
    rw.move(0x04, 0x04, 3);
    rw.out<uint8>(0x07, kInventory);
    rw.move(0x07, 0x08, 1);
    rw.move(0x08, 0x0C, 15);
    rw.out<uint8>(0x1B, 1);
    return Result::Rewritten;
}

// 0x0DD - EQUIP_INSPECT
// The 2010 client has no inspect kind. It's left 0, which is a normal /check.
auto equipInspect(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x10);
    rw.move(0x04, 0x04, 8);
    return Result::Rewritten;
}

// 0x0E1 - GET_LSMSG
// 0x0E2 - SET_LSMSG
// 0x0E4 - GET_LSPRIV
// The 2010 client has no container or item index fields, so everything after the flags moves to today's offsets.
// The second flags byte is only real when SET_LSMSG changes access levels. Otherwise it's leftover data and
// is set to 0, which also picks linkshell 1.
auto lsMessage(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.getSize() >= 0x90)
    {
        return Result::Pass;
    }

    Rewrite rw(packet);

    const uint8 flags  = rw.in<uint8>(0x04);
    const bool  level  = packet.getType() == 0x0E2 && (flags & 0x20) != 0;
    const uint8 levels = level ? static_cast<uint8>(rw.in<uint8>(0x05) & 0x3F) : 0;

    rw.clear(0x90);
    rw.out<uint8>(0x04, flags);
    rw.out<uint8>(0x05, levels);
    rw.move(0x06, 0x0A, 2);
    rw.move(0x08, 0x0C, 4);
    rw.move(0x0C, 0x10, 128);
    return Result::Rewritten;
}

// 0x0FA - MYROOM_LAYOUT
// The 2010 client has no container or floor fields. Furniture is in the Mog Safe,
// and the 2010 Mog House has one floor.
auto myroomLayout(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x10);
    rw.move(0x04, 0x04, 3);
    rw.out<uint8>(0x07, kMogSafe);
    rw.out<uint8>(0x08, 0);
    rw.move(0x07, 0x09, 4);
    return Result::Rewritten;
}

// 0x0FB - MYROOM_BANKIN
// 0x0FD - MYROOM_PLANT_CHECK
// 0x0FF - MYROOM_PLANT_STOP
// The 2010 client has no container field. These items are in the Mog Safe.
auto myroomItem(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 3);
    rw.out<uint8>(0x07, kMogSafe);
    return Result::Rewritten;
}

// 0x0FC - MYROOM_PLANT_ADD
// The 2010 client has no container fields for the plant and the added item. Both are in the Mog Safe.
auto myroomPlantAdd(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x0C);
    rw.move(0x04, 0x04, 6);
    rw.out<uint8>(0x0A, kMogSafe);
    rw.out<uint8>(0x0B, kMogSafe);
    return Result::Rewritten;
}

// 0x0FE - MYROOM_PLANT_CROP
// The 2010 client has no container field, so the cancel flag moves to today's offset. The plant is in the Mog Safe.
auto myroomPlantCrop(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x0C);
    rw.move(0x04, 0x04, 3);
    rw.out<uint8>(0x07, kMogSafe);
    rw.move(0x07, 0x08, 1);
    return Result::Rewritten;
}

// 0x102 - EXTENDED_JOB
// Same layout as today, but the 2010 client sends 0xA0 bytes and today's packet is 0xA4.
auto extendedJob(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.getSize() >= 0xA4)
    {
        return Result::Pass;
    }

    Rewrite rw(packet);
    rw.clear(0xA4);
    rw.move(0x04, 0x04, 2);
    rw.move(0x08, 0x08, 0x98);
    return Result::Rewritten;
}

// 0x033 - TRADE_RES
// The 2010 client's Trade button sends kind 3. Today 3 means cancel confirm, which the server ignores,
// so PS2 players could never finish a trade. It's changed to 2 (confirm). The 2010 client sends 2 and 3 no other way.
auto tradeRes(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint32>(0x04) != 3)
    {
        return Result::Pass;
    }
    packet.ref<uint32>(0x04) = 2;
    return Result::Rewritten;
}

} // namespace

void registerC2S(compat::Profile& p)
{
    p.c2s(0x033, &tradeRes);
    p.c2s(0x01A, &action);
    p.c2s(0x01B, &friendPass);
    p.c2s(0x01E, &gm);
    p.c2s(0x01F, &gmCommand);
    p.c2s(0x04E, &auction);
    p.c2s(0x050, &equipSet);
    p.c2s(0x05B, &eventEnd);
    p.c2s(0x061, &cliStatus);
    p.c2s(0x077, &groupChange2);
    p.c2s(0x083, &shopBuy);
    p.c2s(0x0A0, &switchProposal);
    p.c2s(0x0A1, &switchVote);
    p.c2s(0x0AA, &guildBuy);
    p.c2s(0x0B5, &chatStd);
    p.c2s(0x0B6, &chatName);
    p.c2s(0x0C3, &groupComlinkMake);
    p.c2s(0x0C4, &groupComlinkActive);
    p.c2s(0x0DD, &equipInspect);
    p.c2s(0x0E1, &lsMessage);
    p.c2s(0x0E2, &lsMessage);
    p.c2s(0x0E4, &lsMessage);
    p.c2s(0x0FA, &myroomLayout);
    p.c2s(0x0FB, &myroomItem);
    p.c2s(0x0FC, &myroomPlantAdd);
    p.c2s(0x0FD, &myroomItem);
    p.c2s(0x0FE, &myroomPlantCrop);
    p.c2s(0x0FF, &myroomItem);
    p.c2s(0x102, &extendedJob);
}

} // namespace console
