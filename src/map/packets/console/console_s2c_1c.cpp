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

// Server->client packets 0x01C-0x03F for the 2010 PS2 client (FFXI-PS2 docs/packets/s2c_1c.md).
// Ids not registered here have the same layout in both clients and pass unchanged.

namespace console
{

namespace
{

// Item storage of the 2010 client: base+0x6200, 6 containers x 81 slots x 0x2C. The item handlers
// index it with the raw packet bytes and no bound check.
constexpr uint8 kContainers = 6;
constexpr uint8 kSlots      = 81;

// Entity table DAT_005db630 has 0x800 entries.
constexpr uint16 kActIndexMax = 0x800;

// Trade tables: other side at base+0xB590 (10 x 0x2C), own side at base+0xB748 (10 x 8).
constexpr uint8 kTradeSlots = 10;

// Shop table at base+0x45FC: 16 entries x 0x34, followed by the shop state at base+0x493C.
constexpr uint8 kShopSlots = 16;

// FUN_002fda60 (message colour) indexes 0x400-byte tables by message number.
constexpr uint16 kBattleMessages = 0x400;

auto itemSlotValid(const uint8 container, const uint8 slot) -> bool
{
    return container < kContainers && slot < kSlots;
}

// 0x01C ITEM_MAX, RecvItemMax 0x38C6D0: u8 size[6] at +4, u16 usable[6] at +0x0A (memcpy 6 and 12).
// Today: u8 ItemNum[18] at +4, u16 ItemNum2[18] at +0x24.
auto itemMax(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);
    rw.clear(0x18);

    for (uint8 i = 0; i < kContainers; ++i)
    {
        rw.out<uint8>(0x04 + i, std::min<uint8>(rw.in<uint8>(0x04 + i), kSlots));
        rw.out<uint16>(0x0A + i * 2, std::min<uint16>(rw.in<uint16>(0x24 + i * 2), kSlots));
    }

    return Result::Rewritten;
}

// 0x01E ITEM_NUM, RecvItemNum 0x38C730: container +8, slot +9. Same layout.
auto itemNum(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return itemSlotValid(packet.ref<uint8>(0x08), packet.ref<uint8>(0x09)) ? Result::Pass : Result::Drop;
}

// 0x01F ITEM_LIST, RecvItemList 0x38C7B0: container +0x0A, slot +0x0B. Same layout.
auto itemList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return itemSlotValid(packet.ref<uint8>(0x0A), packet.ref<uint8>(0x0B)) ? Result::Pass : Result::Drop;
}

// 0x020 ITEM_ATTR, RecvItemAttr 0x38C860: container +0x0E, slot +0x0F. Same layout.
auto itemAttr(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return itemSlotValid(packet.ref<uint8>(0x0E), packet.ref<uint8>(0x0F)) ? Result::Pass : Result::Drop;
}

// 0x021 ITEM_TRADE_REQ, TkRecvItemTradeReq 0x3DFF90: DAT_005db630[ActIndex +8] unchecked.
auto itemTradeReq(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x08) < kActIndexMax ? Result::Pass : Result::Drop;
}

// 0x022 ITEM_TRADE_RES, TkRecvItemTradeRes 0x3DFFE0: ActIndex +0x0C handed on unchecked.
auto itemTradeRes(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x0C) < kActIndexMax ? Result::Pass : Result::Drop;
}

// 0x023 ITEM_TRADE_LIST, RecvItemTradeList 0x38CD60 / TkRecvItemTradeList 0x3E0040: TradeIndex +0x0D.
auto itemTradeList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x0D) < kTradeSlots ? Result::Pass : Result::Drop;
}

// 0x025 ITEM_TRADE_MYLIST, RecvItemTradeMyList 0x38CCE0: TradeIndex +0x0A.
auto itemTradeMyList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x0A) < kTradeSlots ? Result::Pass : Result::Drop;
}

// 0x026 ITEM_SUBCONTAINER (mannequins). The 2010 id is ITEM_UPDATE_ITEMBOX (RecvItemBoxUpdate
// 0x38C990), a 16-entry table keyed on byte +5 with another meaning. Mannequins did not exist.
auto itemSubcontainer(MapSession* /* PSession */, CBasicPacket& /* packet */) -> Result
{
    return Result::Drop;
}

// 0x029 BATTLE_MESSAGE, RecvBattleMessage 0x2FDD10: MessageNum +0x18 indexes 0x400-byte tables.
auto battleMessage(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x18) < kBattleMessages ? Result::Pass : Result::Drop;
}

// 0x02D BATTLE_MESSAGE2, Recv_battle_message2 0x2FE090: MessageNum +0x18, same tables.
auto battleMessage2(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x18) < kBattleMessages ? Result::Pass : Result::Drop;
}

// Bit stream of 0x028: LSB-first within each byte, each field LSB-first.
struct BitReader
{
    const uint8* data;
    size_t       pos;
    size_t       end;
    bool         ok{ true };

    auto get(const uint8 bits) -> uint32
    {
        uint32 v = 0;
        for (uint8 i = 0; i < bits; ++i)
        {
            if (pos >= end)
            {
                ok = false;
                return 0;
            }
            v |= static_cast<uint32>((data[pos >> 3] >> (pos & 7)) & 1) << i;
            ++pos;
        }
        return v;
    }
};

struct BitWriter
{
    uint8* data;
    size_t pos;
    size_t end;
    bool   ok{ true };

    void put(const uint32 v, const uint8 bits)
    {
        for (uint8 i = 0; i < bits; ++i)
        {
            if (pos >= end)
            {
                ok = false;
                return;
            }
            if ((v >> i) & 1)
            {
                data[pos >> 3] |= static_cast<uint8>(1 << (pos & 7));
            }
            ++pos;
        }
    }
};

constexpr auto fits(const uint32 v, const uint8 bits) -> bool
{
    return v < (1u << bits);
}

// 0x028 BATTLE2, RecvBattleCalc2 0x2FD640, bit reader FUN_0042a5d0 (reads from packet+5).
// Header is the same (32 uid, 6 trg_sum, 4 res_sum, 4 cmd_no, 32 cmd_arg, 32 info) and so are the
// target heads (32 uid, 4 result_sum). Per result:
//   today: miss 3, kind 2, sub_kind 12, info 5, scale 5, value 17, message 10, bit 31,
//          proc 1+(6, 4, 17, 10), react 1+(6, 4, 14, 10)
//   2010:  miss 3, kind 2, sub_kind 11, info 4, scale 5, value 16, message 10, bit 32,
//          proc 1+(6, 4, 14, 10), react 1+(6, 4, 14, 10)
// The 2010 stream is never longer than today's, so it fits in the same buffer.
auto battle2(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    const size_t inSize = packet.getSize();
    if (inSize <= 5)
    {
        return Result::Drop;
    }

    BitReader in{ rw.src, 5 * 8, inSize * 8 };

    rw.clear(4);
    BitWriter out{ static_cast<uint8*>(packet), 5 * 8, (PACKET_SIZE & ~3u) * 8 };

    // The caster and target ids are static entity ids for NPCs and mobs, renumbered since 2010 like
    // every other entity field (console_entities.cpp); one that did not exist in 2010 drops the action.
    const auto uid = [](const uint32 v) { return ids::uniqueNoToClient(v); };

    const auto caster = uid(in.get(32));
    if (!caster)
    {
        return Result::Drop;
    }
    out.put(*caster, 32); // m_uID
    const uint32 trgSum = in.get(6);
    out.put(trgSum, 6);
    out.put(in.get(4), 4);   // res_sum
    out.put(in.get(4), 4);   // cmd_no
    out.put(in.get(32), 32); // cmd_arg
    out.put(in.get(32), 32); // info

    for (uint32 t = 0; t < trgSum && in.ok; ++t)
    {
        const auto target = uid(in.get(32));
        if (!target)
        {
            return Result::Drop;
        }
        out.put(*target, 32); // target m_uID
        const uint32 resultSum = in.get(4);
        if (resultSum > 8)
        {
            return Result::Drop; // result[8] in the client; today's builder never sends more
        }
        out.put(resultSum, 4);

        for (uint32 r = 0; r < resultSum && in.ok; ++r)
        {
            out.put(in.get(3), 3); // miss
            out.put(in.get(2), 2); // kind

            const uint32 subKind = in.get(12);
            out.put(fits(subKind, 11) ? subKind : 0, 11); // animation the 2010 client cannot hold -> none

            const uint32 info = in.get(5);
            out.put(fits(info, 4) ? info : 0, 4);

            out.put(in.get(5), 5); // scale (hit distortion 2 + knockback 3)

            out.put(std::min<uint32>(in.get(17), 0xFFFF), 16); // value, saturated
            out.put(in.get(10), 10);                           // message
            out.put(in.get(31), 32);                           // bit

            const uint32 hasProc = in.get(1);
            out.put(hasProc, 1);
            if (hasProc)
            {
                out.put(in.get(6), 6);                             // proc_kind
                out.put(in.get(4), 4);                             // proc_info
                out.put(std::min<uint32>(in.get(17), 0x3FFF), 14); // proc_value, saturated
                out.put(in.get(10), 10);                           // proc_message
            }

            const uint32 hasReact = in.get(1);
            out.put(hasReact, 1);
            if (hasReact)
            {
                out.put(in.get(6), 6);   // react_kind
                out.put(in.get(4), 4);   // react_info
                out.put(in.get(14), 14); // react_value
                out.put(in.get(10), 10); // react_message
            }
        }
    }

    if (!in.ok || !out.ok)
    {
        return Result::Drop;
    }

    // byte +4 is skipped by the reader; today's builder puts the work size there
    const size_t bytes = (out.pos + 7) / 8;
    rw.out<uint8>(0x04, static_cast<uint8>(bytes));
    packet.setSize(bytes);

    return Result::Rewritten;
}

// 0x037 SERVERSTATUS, RecvServerStatus 0x2FBBD0. Same layout up to +0x4B (BufStatus, UniqueNo,
// Flags0-3, dead counters, costume, warp/fellow index). At +0x4C the 2010 client reads 32 bits,
// one per icon (id = BufStatus[i] | bit << 8); today's packet has 64 bits there, two per icon
// (id = BufStatus[i] | bits << 8). Everything from +0x50 is new.
auto serverStatus(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    const uint64 bits2 = rw.in<uint64>(0x4C);
    uint32       bits1 = 0;
    for (uint8 i = 0; i < 32; ++i)
    {
        const auto hi = static_cast<uint8>((bits2 >> (i * 2)) & 3);
        if (hi > 1)
        {
            rw.out<uint8>(0x04 + i, 0xFF); // effect id >= 0x200: no such effect in 2010
        }
        else if (hi == 1)
        {
            bits1 |= 1u << i;
        }
    }

    // Flags3 byte +0x38 is passed on as it is. Bit 3 (NewCharacterFlag) and bit 4 (MentorFlag) go to actor flags
    // 0x114 bits 4 and 3, the same two bits RecvCharPc sets from +0x2A bit 7 and +0x2B bit 0 of the 0x00D (the
    // New Adventurer and Mentor icons: the client has the Mentor Program texts and /mentor). RecvServerStatus
    // assigns them, so clearing them here wiped what the 0x00D had set within seconds.
    std::memset(static_cast<uint8*>(packet) + 0x4C, 0, PACKET_SIZE - 0x4C);
    rw.out<uint32>(0x4C, bits1);
    packet.setSize(0x50);

    return Result::Rewritten;
}

// 0x03C SHOP_LIST, RecvShopList 0x39B670 (table B only). 2010 entries are 8 bytes at +8
// (u32 price, u16 ItemNo, u8 ShopIndex, u8 pad), count = (size - 16) / 8 + 1, stored at
// table[ShopIndex] with no bound. Today's entries are 12 bytes (+ Skill, GuildInfo).
auto shopList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    const size_t inSize = packet.getSize();
    const size_t inCount = inSize > 8 ? (inSize - 8) / 12 : 0;

    rw.clear(4);

    size_t n = 0;
    for (size_t i = 0; i < inCount; ++i)
    {
        const size_t from  = 0x08 + i * 12;
        const uint8  index = rw.in<uint8>(from + 6);
        if (index >= kShopSlots)
        {
            continue;
        }

        const size_t to = 0x08 + n * 8;
        rw.out<uint32>(to, rw.in<uint32>(from));         // ItemPrice
        rw.out<uint16>(to + 4, rw.in<uint16>(from + 4)); // ItemNo
        rw.out<uint8>(to + 6, index);                    // ShopIndex
        ++n;
    }

    // With no entry the client's count underflows to 0x20000000: never send an empty list.
    if (n == 0)
    {
        return Result::Drop;
    }

    rw.move(0x04, 0x04, 4); // ShopItemOffsetIndex, flags (not read by the 2010 client)
    packet.setSize(0x08 + n * 8);

    return Result::Rewritten;
}

// 0x03D SHOP_SELL, RecvShopSell 0x39B740 / RecvShopSell_A03D 0x4AF340: inventory slot +8
// goes to FUN_0038af60 (inventory record, writes when slot 0). Same layout.
auto shopSell(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x08) < kSlots ? Result::Pass : Result::Drop;
}

// 0x03E SHOP_OPEN, RecvShopOpen_A03E 0x4AF740: ShopListNum byte +4 goes to the shop menu, which
// has 16 entries.
auto shopOpen(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    const uint16 num = packet.ref<uint16>(0x04);
    if (num <= kShopSlots)
    {
        return Result::Pass;
    }

    packet.ref<uint16>(0x04) = kShopSlots;
    return Result::Rewritten;
}

// 0x03F SHOP_BUY, Recv_shop_buy 0x4AF2C0: ShopItemIndex +4 indexes the 16-entry shop table.
auto shopBuy(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x04) < kShopSlots ? Result::Pass : Result::Drop;
}

// 0x027 / 0x02A / 0x036: MesNum is a line of the current zone's dialog DAT, today's numbering. The
// 2010 DAT has shifted (lines inserted over the years), so remap it; a line added after 2010 has no
// counterpart and the message is dropped rather than shown as the wrong text.
// 0x032 EVENT / 0x033 EVENTSTR / 0x034 EVENTNUM: EventNum (the zone, whose event files the client
// opens) and EventPara (the event). An event the zone's 2010 files do not carry runs on none of the
// client's entities, and the client never sends its end: the player stays locked (Dkhaaya, zone 50).
// Such an event is not sent, and the server's side of it is closed without its finish handler, as if
// it never started (no quest progress, no reward).
// A dropped event becomes a 0x052 release (Mode 0). 8700 (Home Point) is sent as the zone's 2010
// Home Point event.
template <size_t kEventNumOffset>
auto eventGuard(MapSession* PSession, CBasicPacket& packet) -> Result
{
    const auto zone  = packet.ref<uint16>(kEventNumOffset);
    const auto event = packet.ref<uint16>(kEventNumOffset + 2);
    if (event == ids::kHomePointEvent)
    {
        if (const auto old = ids::homePointEvent(zone))
        {
            packet.ref<uint16>(kEventNumOffset + 2) = *old;
            return Result::Rewritten;
        }
    }

    if (ids::hasEvent(zone, event))
    {
        return Result::Pass;
    }

    auto* PChar = PSession->PChar.get();
    ShowInfoFmt("console: event {} in zone {} is not in the 2010 data; skipped for {}", event, zone, PChar ? PChar->getName() : "?");
    if (PChar && PChar->currentEvent && PChar->currentEvent->eventId == event)
    {
        PChar->endCurrentEvent();
    }

    packet.setType(0x052);
    packet.setSize(0x08);
    packet.ref<uint32>(0x04) = 0;
    return Result::Rewritten;
}

template <size_t kMesNumOffset>
auto zoneDialog(MapSession* PSession, CBasicPacket& packet) -> Result
{
    if (!PSession->PChar)
    {
        return Result::Drop;
    }

    const auto mesNum = ids::mesNum(static_cast<uint16>(PSession->PChar->getZone()), packet.ref<uint16>(kMesNumOffset));
    if (!mesNum)
    {
        return Result::Drop;
    }

    packet.ref<uint16>(kMesNumOffset) = *mesNum;
    return Result::Rewritten;
}

} // namespace

void registerS2C_1C(compat::Profile& p)
{
    p.s2c(0x027, zoneDialog<0x0A>);
    p.s2c(0x032, eventGuard<0x0A>);
    p.s2c(0x033, eventGuard<0x0A>);
    p.s2c(0x034, eventGuard<0x2A>);
    p.s2c(0x02A, zoneDialog<0x1A>);
    p.s2c(0x036, zoneDialog<0x0A>);

    p.s2c(0x01C, itemMax);
    p.s2c(0x01E, itemNum);
    p.s2c(0x01F, itemList);
    p.s2c(0x020, itemAttr);
    p.s2c(0x021, itemTradeReq);
    p.s2c(0x022, itemTradeRes);
    p.s2c(0x023, itemTradeList);
    p.s2c(0x025, itemTradeMyList);
    p.s2c(0x026, itemSubcontainer);
    p.s2c(0x028, battle2);
    p.s2c(0x029, battleMessage);
    p.s2c(0x02D, battleMessage2);
    p.s2c(0x037, serverStatus);
    p.s2c(0x03C, shopList);
    p.s2c(0x03D, shopSell);
    p.s2c(0x03E, shopOpen);
    p.s2c(0x03F, shopBuy);
}

} // namespace console
