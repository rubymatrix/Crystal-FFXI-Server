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

#include "console_groups.h"
#include "console_ids.h"

#include "entities/char_entity.h"
#include "event_info.h"
#include "map_session.h"

#include <algorithm>

// Client -> server packets of the 2010 PS2 client, rewritten into today's layout before dispatch.
//
// Ground truth is the client's own packet builders (FFXI-PS2 decomp/senders/<VA>_*.c): each one
// takes a buffer from gcZoneSendQueSearch (0x2D3470) and sets the size with 0x2D56A0 (size in bytes,
// rounded up to 4). Offsets below include the 4-byte header. Ids that are not registered here are
// byte-identical to today's layout (FFXI-PS2 docs/packets/c2s.md covers every id the client sends).
//
// LSB rejects a packet whose size is outside [offsetof(VLA), roundUp4(sizeof)] (packet_system.cpp),
// so every rewrite sets today's exact size. Rewrite::clear zeroes the tail, which also removes the
// bytes of the next packet in the datagram that parse copied into the scratch packet.
//
// Fields that carry 2010-numbered ids (event ids, NPC indices, items, ...) are not remapped here;
// see "Ids to remap" in c2s.md.

namespace console
{

namespace
{

constexpr uint8 kInventory = 0; // LOC_INVENTORY
constexpr uint8 kMogSafe   = 1; // LOC_MOGSAFE

// Text packets: the 2010 client allows up to 150 characters where today's layout holds 128.
// Anything longer than today's maximum is cut and terminated at the last byte.
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

// 0x01A ACTION: builders 0x37FCE0, 0x381A70, 0x381B00, 0x381E80, 0x381F10, 0x381FF0, 0x3820A0,
// 0x382420, 0x382750, 0x382880, 0x382990, 0x382C60, 0x3855A0, 0x385670, 0x385740, 0x385B00.
// 2010 is 0x10 bytes: UniqueNo +4, ActIndex +8, ActionID +0xA, one u32 parameter +0xC.
// Today ActionBuf is 4 u32 (0x1C bytes); the rest (the cast offset x/z/y) is zero.
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

// 0x01B FRIENDPASS: builders 0x3754D0, 0x3755D0. The client sizes it 0x1C; today it is 8. Para +4.
auto friendPass(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 2);
    return Result::Rewritten;
}

// 0x01E GM: builder 0x3385D0. Command +4, size 6 + strlen (up to 150). Today holds 115 (0x78).
auto gm(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x78);
}

// 0x01F GMCOMMAND: builder 0x338480. GMUniqueNo +4, Command +8, size 0xC + strlen (up to 150).
auto gmCommand(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x88);
}

// 0x04E AUC: builder 0x4ECF60 sends Command 0x10 (clears a sale-status bit, AucWorkIndex 0-7).
// Today's server has no such command and would reject it with a warning each time.
auto auction(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x04) == 0x10)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x050 EQUIP_SET: builder 0x39BD50. PropertyItemIndex +4, EquipKind +5, size 6 (8).
// Today adds Category +6; the 2010 client equips from the inventory only (byte +6 is stale).
auto equipSet(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 2);
    rw.out<uint8>(0x06, kInventory);
    return Result::Rewritten;
}

// 0x05B EVENTEND: EndPara +8, Mode +0xE, EventNum +0x10, EventPara +0x12. Home Point: 2010 event -> 8700;
// on end, 2010 Yes (0) -> SET_HOMEPOINT (1), anything else -> 0.
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

// 0x061 CLISTATUS: builder 0x2F5400. Header only (4); today 8 with unknown00 +4 (0 or 1).
auto cliStatus(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    return Result::Rewritten;
}

// 0x077 GROUP_CHANGE2: builder 0x3B0C70. sName +4 is strncpy'd with 15, so +0x13 is stale when the
// name is 15 characters long; terminate it. Kind +0x14, ChangeKind +0x15, size 0x16 (0x18).
auto groupChange2(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x18);
    rw.move(0x04, 0x04, 15);
    rw.move(0x14, 0x14, 2);
    return Result::Rewritten;
}

// 0x083 SHOP_BUY: builder 0x39B410. ItemNum +4, ShopItemIndex +0xA, byte +0xC, size 0x10.
// ShopNo +8 is never written (stale); today's server wants PropertyItemIndex +0xC == 0.
auto shopBuy(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x10);
    rw.move(0x04, 0x04, 4);
    rw.move(0x0A, 0x0A, 2);
    return Result::Rewritten;
}

// 0x0A0 SWITCH_PROPOSAL: builder 0x4E45C0. Kind +4, Str +5, size 6 + strlen (up to 151).
auto switchProposal(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x88);
}

// 0x0A1 SWITCH_VOTE: builder 0x4E4860. Index +4, Name +5, size 6 + strlen (up to 15, so 0x18).
// Today's maximum is 0x14 (Name[15], not terminated when full).
auto switchVote(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    if (packet.getSize() <= 0x14)
    {
        return Result::Pass;
    }

    packet.setSize(0x14);
    return Result::Rewritten;
}

// 0x0AA GUILD_BUY: builder 0x4B8E50. ItemNo +4, PropertyItemIndex +6, ItemNum +7, size 8.
// Today's server wants PropertyItemIndex == 0 (it picks the slot itself).
auto guildBuy(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    packet.ref<uint8>(0x06) = 0;
    return Result::Rewritten;
}

// 0x0B5 CHAT_STD: builder 0x39C9B0. Kind +4, 0 +5, Str +6, size 7 + strlen (up to 150).
auto chatStd(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return clampText(packet, 0x88);
}

// 0x0B6 CHAT_NAME: builder 0x39CB80.
// 2010: 0 +4, sName +5 (up to 15 + NUL), Mes +0x14, size 0x15 + strlen (up to 150).
// Today: unknown04 +4 (must be 3), unknown05 +5 (0), sName[15] +6, Mes +0x15.
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

// 0x0C3 GROUP_COMLINK_MAKE: builder 0x3B1040. State +4, size 6 (8). Today adds LinkshellId +5
// (1 or 2); the 2010 client has one linkshell slot (byte +5 is stale).
auto comlinkMake(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 1);
    rw.out<uint8>(0x05, 1);
    return Result::Rewritten;
}

// 0x0C4 GROUP_COMLINK_ACTIVE: builder 0x3B0E60 (computed id).
// 2010: rgba +4 (u16 nibbles), ItemIndex +6, ActiveFlg +7, sComLinkName[15] +8, size 0x18.
// Today: rgba +4, ItemIndex +6, Category +7, ActiveFlg +8, sComLinkName +0xC, LinkshellId +0x1B,
// size 0x1C.
auto comlinkActive(MapSession* /* PSession */, CBasicPacket& packet) -> Result
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

// 0x0DD EQUIP_INSPECT: builder 0x39BF10. UniqueNo +4, ActIndex +8, size 0xC.
// Today adds Kind +0xC (0 = /check), size 0x10.
auto equipInspect(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x10);
    rw.move(0x04, 0x04, 8);
    return Result::Rewritten;
}

// 0x0E1 GET_LSMSG / 0x0E2 SET_LSMSG / 0x0E4 GET_LSPRIV: builders 0x3B27F0, 0x3B2860, 0x3B28F0,
// 0x3B2720. 2010: flags +4/+5, seqId +6, uniqNo +8, sMessage[128] +0xC, size 0x8C.
// Today: flags +4/+5, Category +6, ItemIndex +7, seqId +0xA, uniqNo +0xC, sMessage +0x10, size 0x90.
// Byte +5 (levels, LinkshellId in bits 6-7) is only written when changing the access level
// (0x0E2 with +4 bit 5 set, 0x3B28F0); otherwise it is stale and is zeroed. LinkshellId is LS1 (0).
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

// 0x0FA MYROOM_LAYOUT: builders 0x3C41F0 (leave layout, all zero), 0x3C4270.
// 2010: ItemNo +4, ItemIndex +6, x +7, y +8, z +9, v +0xA, size 0xC.
// Today: ItemNo +4, ItemIndex +6, Category +7, FloorFlg +8, x +9, y +0xA, z +0xB, v +0xC, size 0x10.
// Furniture lives in the Mog Safe; the 2010 mog house has one floor.
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

// 0x0FB MYROOM_BANKIN (builder 0x3C4340), 0x0FD MYROOM_PLANT_CHECK (0x3C4480),
// 0x0FF MYROOM_PLANT_STOP (0x3C45B0): ItemNo +4, ItemIndex +6, size 8. Today adds Category +7.
auto myroomItem(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x08);
    rw.move(0x04, 0x04, 3);
    rw.out<uint8>(0x07, kMogSafe);
    return Result::Rewritten;
}

// 0x0FC MYROOM_PLANT_ADD: builder 0x3C43D0. PlantItemNo +4, AddItemNo +6, PlantItemIndex +8,
// AddItemIndex +9, size 0xA (0xC). Today adds PlantCategory +0xA, AddCategory +0xB.
auto myroomPlantAdd(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x0C);
    rw.move(0x04, 0x04, 6);
    rw.out<uint8>(0x0A, kMogSafe);
    rw.out<uint8>(0x0B, kMogSafe);
    return Result::Rewritten;
}

// 0x0FE MYROOM_PLANT_CROP: builder 0x3C4510. ItemNo +4, ItemIndex +6, CancellFlg +7, size 8.
// Today: ItemNo +4, ItemIndex +6, Category +7, CancellFlg +8, size 0xC.
auto myroomPlantCrop(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x0C);
    rw.move(0x04, 0x04, 3);
    rw.out<uint8>(0x07, kMogSafe);
    rw.move(0x07, 0x08, 1);
    return Result::Rewritten;
}

// 0x102 EXTENDED_JOB: builders 0x2D47E0, 0x2D4A10. Same layout as today (id +4, 0 +5, job data
// +8: JobIndex, SupportJobFlg, 2 pad, 16/20 slots), but 0xA0 bytes; today's union makes it 0xA4.
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

// 0x033 TRADE_RES: the 2010 client has three builders, FUN_0038B7A0 (Kind 0, accept the request),
// FUN_0038B840 (Kind 1, cancel) and FUN_0038B8E0 (Kind 3, the "Trade" button). Today 3 is
// MakeCancell, which LSB ignores, and confirming is Make (2), so a PS2 player could never complete a
// trade. The 2010 client has no builder for today's Make/MakeCancell otherwise, so 3 -> 2 is safe.
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
    p.c2s(0x0C3, &comlinkMake);
    p.c2s(0x0C4, &comlinkActive);
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
