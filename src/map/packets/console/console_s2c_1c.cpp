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

// Server packets 0x01C-0x03F for the 2010 PS2 client.
// Packets not listed here have the same layout on both clients and are sent unchanged.
// Layouts are in FFXI-PS2 docs/packets/s2c_1c.md.

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

// The 2010 client has 6 item containers of 81 slots each.
constexpr uint8 kContainers = 6;
constexpr uint8 kSlots      = 81;

// Size of the 2010 client's entity list.
constexpr uint16 kEntityCount = 0x800;

// The 2010 client's trade window holds 10 items on each side.
constexpr uint8 kTradeSlots = 10;

// The 2010 client's shop holds 16 items.
constexpr uint8 kShopSlots = 16;

// The 2010 client's battle message tables have 0x400 entries.
constexpr uint16 kBattleMessages = 0x400;

// True if the container and slot exist on the 2010 client.
auto itemSlotValid(const uint8 container, const uint8 slot) -> bool
{
    return container < kContainers && slot < kSlots;
}

// 0x01C - ITEM_MAX
// The 2010 client has 6 containers instead of 18, and no more than 81 slots in each.
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

// 0x01E - ITEM_NUM
// Bounds check the container and slot for the 2010 client.
auto itemNum(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return itemSlotValid(packet.ref<uint8>(0x08), packet.ref<uint8>(0x09)) ? Result::Pass : Result::Drop;
}

// 0x01F - ITEM_LIST
// Bounds check the container and slot for the 2010 client.
auto itemList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return itemSlotValid(packet.ref<uint8>(0x0A), packet.ref<uint8>(0x0B)) ? Result::Pass : Result::Drop;
}

// 0x020 - ITEM_ATTR
// Bounds check the container and slot for the 2010 client.
auto itemAttr(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return itemSlotValid(packet.ref<uint8>(0x0E), packet.ref<uint8>(0x0F)) ? Result::Pass : Result::Drop;
}

// 0x021 - ITEM_TRADE_REQ
// Bounds check the entity list for the 2010 client.
auto itemTradeReq(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x08) < kEntityCount ? Result::Pass : Result::Drop;
}

// 0x022 - ITEM_TRADE_RES
// Bounds check the entity list for the 2010 client.
auto itemTradeRes(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x0C) < kEntityCount ? Result::Pass : Result::Drop;
}

// 0x023 - ITEM_TRADE_LIST
// Bounds check the trade slot for the 2010 client.
auto itemTradeList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x0D) < kTradeSlots ? Result::Pass : Result::Drop;
}

// 0x025 - ITEM_TRADE_MYLIST
// Bounds check the trade slot for the 2010 client.
auto itemTradeMyList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x0A) < kTradeSlots ? Result::Pass : Result::Drop;
}

// 0x026 - ITEM_SUBCONTAINER
// Mannequins don't exist on the 2010 client. It uses this id for a different item box packet.
auto itemSubcontainer(MapSession* /* PSession */, CBasicPacket& /* packet */) -> Result
{
    return Result::Drop;
}

// 0x029 - BATTLE_MESSAGE
// Bounds check the message number for the 2010 client.
auto battleMessage(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x18) < kBattleMessages ? Result::Pass : Result::Drop;
}

// 0x02D - BATTLE_MESSAGE2
// Bounds check the message number for the 2010 client.
auto battleMessage2(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x18) < kBattleMessages ? Result::Pass : Result::Drop;
}

// Reads the BATTLE2 bit stream. Bits are LSB-first within each byte and within each field.
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

// Writes the BATTLE2 bit stream, in the same bit order as BitReader.
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

// True if v fits in the given number of bits.
constexpr auto fits(const uint32 v, const uint8 bits) -> bool
{
    return v < (1u << bits);
}

// 0x028 - BATTLE2
// Some result fields are narrower on the 2010 client, so the bit stream is repacked.
// Values too big for the 2010 field are capped, or cleared where a capped value would be wrong.
//
// The header and the target heads are the same. Each result, in bits:
//   field      today               2010
//   miss       3                   3
//   kind       2                   2
//   sub_kind   12                  11
//   info       5                   4
//   scale      5                   5
//   value      17                  16
//   message    10                  10
//   bit        31                  32
//   proc       1 + (6, 4, 17, 10)  1 + (6, 4, 14, 10)
//   react      1 + (6, 4, 14, 10)  1 + (6, 4, 14, 10)
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

    // NPC and mob ids have been renumbered since 2010. If the caster or a target doesn't
    // exist on the 2010 client, the action is dropped.
    const auto uid = [](const uint32 v)
    {
        return ids::uniqueNoToClient(v);
    };

    const auto caster = uid(in.get(32));
    if (!caster)
    {
        return Result::Drop;
    }
    out.put(*caster, 32);
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
        out.put(*target, 32);
        const uint32 resultSum = in.get(4);
        if (resultSum > 8)
        {
            return Result::Drop; // The 2010 client holds 8 results per target.
        }
        out.put(resultSum, 4);

        for (uint32 r = 0; r < resultSum && in.ok; ++r)
        {
            out.put(in.get(3), 3); // miss
            out.put(in.get(2), 2); // kind

            const uint32 subKind = in.get(12);
            out.put(fits(subKind, 11) ? subKind : 0, 11); // Animations the 2010 client can't hold become none.

            const uint32 info = in.get(5);
            out.put(fits(info, 4) ? info : 0, 4);

            out.put(in.get(5), 5); // scale (hit distortion 2, knockback 3)

            out.put(std::min<uint32>(in.get(17), 0xFFFF), 16); // value, capped
            out.put(in.get(10), 10);                           // message
            out.put(in.get(31), 32);                           // bit

            const uint32 hasProc = in.get(1);
            out.put(hasProc, 1);
            if (hasProc)
            {
                out.put(in.get(6), 6);                             // proc_kind
                out.put(in.get(4), 4);                             // proc_info
                out.put(std::min<uint32>(in.get(17), 0x3FFF), 14); // proc_value, capped
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

    // The 2010 client doesn't read byte 4. Today's server puts the size there, so keep doing that.
    const size_t bytes = (out.pos + 7) / 8;
    rw.out<uint8>(0x04, static_cast<uint8>(bytes));
    packet.setSize(bytes);

    return Result::Rewritten;
}

// 0x037 - SERVERSTATUS
// The 2010 client has 1 high bit per status icon instead of 2, and nothing after it.
// Effects that need the second bit don't exist on the 2010 client, so their icons are cleared.
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
            rw.out<uint8>(0x04 + i, 0xFF);
        }
        else if (hi == 1)
        {
            bits1 |= 1u << i;
        }
    }

    // Byte 0x38 is sent as is. Its New Adventurer and Mentor bits set the same icons the 0x00D does,
    // and this packet overwrites them. Clearing them here used to remove those icons within seconds.
    std::memset(static_cast<uint8*>(packet) + 0x4C, 0, PACKET_SIZE - 0x4C);
    rw.out<uint32>(0x4C, bits1);
    packet.setSize(0x50);

    return Result::Rewritten;
}

// 0x03C - SHOP_LIST
// The 2010 client's shop entries are 8 bytes (price, item, shop slot). Today's add skill and guild info.
// Bounds check the shop slot for the 2010 client.
auto shopList(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    const size_t inSize  = packet.getSize();
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
        rw.out<uint32>(to, rw.in<uint32>(from));         // price
        rw.out<uint16>(to + 4, rw.in<uint16>(from + 4)); // item
        rw.out<uint8>(to + 6, index);                    // shop slot
        ++n;
    }

    // The 2010 client works out the item count from the size. An empty list would wrap around.
    if (n == 0)
    {
        return Result::Drop;
    }

    rw.move(0x04, 0x04, 4); // Offset and flags. The 2010 client doesn't read them.
    packet.setSize(0x08 + n * 8);

    return Result::Rewritten;
}

// 0x03D - SHOP_SELL
// Bounds check the inventory slot for the 2010 client.
auto shopSell(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x08) < kSlots ? Result::Pass : Result::Drop;
}

// 0x03E - SHOP_OPEN
// The 2010 client's shop menu has 16 entries. Larger item counts are capped to that.
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

// 0x03F - SHOP_BUY
// Bounds check the shop slot for the 2010 client.
auto shopBuy(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    return packet.ref<uint16>(0x04) < kShopSlots ? Result::Pass : Result::Drop;
}

// 0x032 - EVENT
// 0x033 - EVENTSTR
// 0x034 - EVENTNUM
// The 2010 client can only run events that are in its own zone files. For a newer event it never
// sends the end, and the player stays locked (Dkhaaya, zone 50). So the event isn't sent: the client
// gets a 0x052 release instead, and the server closes its side without the finish handler, as if the
// event never started. No quest progress, no reward.
// The Home Point event (8700) is sent as the zone's 2010 Home Point event.
template <size_t kEventNumOffset>
auto eventGuard(MapSession* PSession, CBasicPacket& packet) -> Result
{
    const auto zone  = packet.ref<uint16>(kEventNumOffset);
    const auto event = packet.ref<uint16>(kEventNumOffset + 2);
    if (event == ids::kHomePointEvent)
    {
        if (const auto old = ids::homePointEvent(zone))
        {
            ShowInfoFmt("console: Home Point event {} -> {} in zone {}", event, *old, zone);
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

// 0x027 - TALKNUMWORK2
// 0x02A - TALKNUMWORK
// 0x036 - TALKNUM
// The message number is a line in the zone's dialog file, and lines have moved since 2010.
// It's remapped to the 2010 line. A line added after 2010 is dropped rather than shown as the wrong text.
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
