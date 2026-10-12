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

// Server packets 0x080-0x10F for the 2010 PS2 client.
// Packets not listed here are sent unchanged, or dropped if the client has no handler for them.
// Layouts are in FFXI-PS2 docs/packets/s2c_80.md. Id remapping is done in console_ids.h.

#include "console_groups.h"

#include "map_session.h"

#include <optional>

namespace console
{

namespace
{

// Size of the 2010 client's entity list.
constexpr uint16 kEntityCount = 0x800;

// The 2010 client's treasure pool has 10 slots.
constexpr uint8 kTreasureSlots = 10;

// The 2010 client's bazaar list has 81 slots.
constexpr uint8 kBazaarSlots = 81;

// A guild shop list packet holds at most 30 entries.
constexpr uint8 kGuildListEntries = 30;

// Number of titles in the 2010 client's title message table.
constexpr uint32 kTitleCount = 0x2E1;

// Maps a group kind to the 2010 client's numbering: 0 party, 1 linkshell, 2 alliance.
// Today alliance is 5. The client uses the kind as a table index, so anything else is rejected.
auto groupKind(const uint8 kind) -> std::optional<uint8>
{
    switch (kind)
    {
        case 0:
        case 1:
        case 2:
            return kind;
        case 5:
            return static_cast<uint8>(2);
        default:
            return std::nullopt;
    }
}

// The 2010 client stores a group member's zone in one byte. All its zone ids are below 0x100.
auto zoneByte(const uint16 zone) -> uint8
{
    return zone <= 0xFF ? static_cast<uint8>(zone) : 0;
}

// 0x083 - GUILD_BUYLIST
// 0x085 - GUILD_SELLLIST
// The 2010 client trusts the item count. It's clamped to the 30 entries a packet holds.
auto guildList(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0xF4) > kGuildListEntries)
    {
        packet.ref<uint8>(0xF4) = kGuildListEntries;
        return Result::Rewritten;
    }

    return Result::Pass;
}

// 0x08C - MERIT
// The 2010 client trusts the merit count. It's clamped to the entries that fit in the packet.
auto merit(MapSession*, CBasicPacket& packet) -> Result
{
    const auto size = packet.getSize();
    const auto max  = size > 8 ? static_cast<uint16>((size - 8) / 4) : static_cast<uint16>(0);

    if (packet.ref<uint16>(0x04) > max)
    {
        packet.ref<uint16>(0x04) = max;
        return Result::Rewritten;
    }

    return Result::Pass;
}

// 0x0AA - MAGIC_DATA
// The 2010 client reads spells 0-767. The rest of today's table is newer spells, so it's cut.
auto magicData(MapSession*, CBasicPacket& packet) -> Result
{
    packet.setSize(0x04 + 0x60);
    return Result::Rewritten;
}

// 0x0AC - COMMAND_DATA
// The 2010 client has one 1024-bit table: job abilities, then pet abilities, then weapon skills.
// Today's packet has a separate table for each, so they're packed into one. Pet abilities and
// weapon skills past the first 256 don't fit. Traits are sent as their own FEAT_DATA (0x0AB).
auto commandData(MapSession*, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    CBasicPacket feat;
    feat.setType(0x0AB);
    feat.setSize(0x04 + 0x14);
    std::memcpy(static_cast<uint8*>(feat) + 0x04, rw.src + 0xC4, 0x14);
    emitS2C(feat, feat.getSize());

    rw.clear(0x04 + 0x80);
    rw.move(0x44, 0x04, 0x40); // job abilities: bits 0-511
    rw.move(0x84, 0x44, 0x20); // pet abilities 512-767: bits 512-767
    rw.move(0x04, 0x64, 0x20); // weapon skills 0-255: bits 768-1023
    return Result::Rewritten;
}

// 0x0B4 - CONFIG
// The 2010 client reads a 16-bit field at 0x10 and a byte at 0x12. Today 0x10 is one byte, followed
// by party languages and padding. Those are cleared so the client doesn't read them as its own fields.
auto config(MapSession*, CBasicPacket& packet) -> Result
{
    packet.ref<uint8>(0x11) = 0;
    packet.ref<uint8>(0x12) = 0;
    packet.ref<uint8>(0x13) = 0;
    return Result::Rewritten;
}

// 0x0C8 - GROUP_TBL
// The 2010 client's member entries are 8 bytes with a one-byte zone. Today's are 12 bytes with a
// two-byte zone. The client always reads all 20 entries.
auto groupTbl(MapSession*, CBasicPacket& packet) -> Result
{
    const auto kind = groupKind(packet.ref<uint8>(0x04));
    if (!kind)
    {
        return Result::Drop;
    }

    Rewrite rw(packet);
    rw.clear(0xA8);
    rw.out<uint8>(0x04, *kind);

    for (size_t i = 0; i < 20; ++i)
    {
        const size_t from = 0x08 + i * 12;
        const size_t to   = 0x08 + i * 8;
        rw.move(from, to, 7); // UniqueNo, ActIndex, flags
        rw.out<uint8>(to + 7, zoneByte(rw.in<uint16>(from + 8)));
    }

    return Result::Rewritten;
}

// 0x0C9 - EQUIP_INSPECT
// Only the general mode (1) has a different layout on the 2010 client. Its fields are moved to where
// the client reads them. The client has no master level, so those fields are left out.
auto equipInspect(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x0A) != 1)
    {
        return Result::Pass;
    }

    Rewrite rw(packet);
    rw.clear(0x48);
    rw.move(0x04, 0x04, 0x08); // UniqNo, ActIndex, OptionFlag, +0x0B
    rw.move(0x0E, 0x0E, 2);    // ItemNo
    rw.move(0x20, 0x10, 2);    // sComColor
    rw.move(0x22, 0x12, 2);    // job[2]
    rw.move(0x10, 0x14, 15);   // sComLinkName (15 packed bytes)
    rw.move(0x24, 0x23, 2);    // lvl[2]
    rw.move(0x2C, 0x28, 4);    // BallistaChevronCount
    rw.move(0x30, 0x2C, 1);    // BallistaChevronFlags
    rw.move(0x32, 0x2E, 2);    // BallistaFlags
    rw.move(0x34, 0x30, 4);    // MesNo
    rw.move(0x38, 0x34, 0x14); // Params[5]
    return Result::Rewritten;
}

// 0x0CA - INSPECT_MESSAGE
// Bounds check the title for the 2010 client. A title it doesn't have is sent as title 0.
auto inspectMessage(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint32>(0x90) >= kTitleCount)
    {
        packet.ref<uint32>(0x90) = 0;
        return Result::Rewritten;
    }

    return Result::Pass;
}

// 0x0CC - LINKSHELL_MESSAGE
// The 2010 client has one linkshell. Messages for Linkshell 2 are dropped, or it would show them as its own.
auto linkshellMessage(MapSession*, CBasicPacket& packet) -> Result
{
    if (((packet.ref<uint8>(0x05) >> 6) & 3) != 0)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x0D2 - TROPHY_LIST
// Bounds check the treasure pool slot and the entity list for the 2010 client.
// A bad lot holder is cleared instead of dropping the whole item.
auto trophyList(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x14) >= kTreasureSlots)
    {
        return Result::Drop;
    }

    if (packet.ref<uint16>(0x12) >= kEntityCount)
    {
        packet.ref<uint16>(0x12) = 0;
        packet.ref<uint8>(0x38)  = 0;
        return Result::Rewritten;
    }

    return Result::Pass;
}

// 0x0D3 - TROPHY_SOLUTION
// Bounds check the treasure pool slot for the 2010 client.
auto trophySolution(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x14) >= kTreasureSlots)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x0DC - GROUP_SOLICIT_REQ
// The group kind is mapped to the 2010 client's numbering. The client prints the inviter's name
// as a string, so its last byte is zeroed to keep it terminated.
auto groupSolicitReq(MapSession*, CBasicPacket& packet) -> Result
{
    const auto kind = groupKind(packet.ref<uint8>(0x0B));
    if (!kind)
    {
        return Result::Drop;
    }

    packet.ref<uint8>(0x0B) = *kind;
    packet.ref<uint8>(0x1B) = 0;
    return Result::Rewritten;
}

// 0x0DD - GROUP_LIST
// 0x0E2 - GROUP_LIST2
// The 2010 client has a one-byte zone and no job fields, so the name sits 8 bytes earlier.
// It also needs the group kind in its own numbering and TP as a percent.
auto groupList(MapSession*, CBasicPacket& packet) -> Result
{
    const auto kind = groupKind(packet.ref<uint8>(0x1C));
    const auto size = packet.getSize();
    if (!kind || size <= 0x2C) // no name, so the client ignores it anyway
    {
        return Result::Drop;
    }

    Rewrite rw(packet);
    rw.clear(size - 8);
    rw.move(0x04, 0x04, 0x1B); // UniqueNo .. Mpp
    rw.out<uint8>(0x1C, *kind);
    rw.out<uint32>(0x10, tpPercent(rw.in<uint32>(0x10)));
    rw.out<uint8>(0x1F, zoneByte(rw.in<uint16>(0x20)));
    rw.move(0x28, 0x20, 16);
    return Result::Rewritten;
}

// 0x0DF - GROUP_ATTR
// Everything after the zone (monstrosity, jobs) is newer than the 2010 client, so it's cut.
// It also needs the group kind in its own numbering and TP as a percent.
auto groupAttr(MapSession*, CBasicPacket& packet) -> Result
{
    const auto kind = groupKind(packet.ref<uint8>(0x18));
    if (!kind)
    {
        return Result::Drop;
    }

    packet.ref<uint8>(0x18)  = *kind;
    packet.ref<uint32>(0x10) = tpPercent(packet.ref<uint32>(0x10));
    packet.setSize(0x1C);
    return Result::Rewritten;
}

// 0x0E0 - GROUP_COMLINK
// The 2010 client has one linkshell. Updates for Linkshell 2 are dropped.
auto groupComlink(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x04) == 2)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x0FA - MYROOM_OPERATION
// The 2010 client reads nothing from this packet, so it isn't sent.
auto myroomOperation(MapSession*, CBasicPacket&) -> Result
{
    return Result::Drop;
}

// 0x105 - BAZAAR_LIST
// Bounds check the bazaar slot for the 2010 client.
auto bazaarList(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x10) >= kBazaarSlots)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x109 - BAZAAR_SELL
// Bounds check the bazaar slot for the 2010 client.
auto bazaarSell(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x20) >= kBazaarSlots)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

} // namespace

void registerS2C_80(compat::Profile& p)
{
    p.s2c(0x083, guildList);
    p.s2c(0x085, guildList);
    p.s2c(0x08C, merit);
    p.s2c(0x0AA, magicData);
    p.s2c(0x0AC, commandData);
    p.s2c(0x0B4, config);
    p.s2c(0x0C8, groupTbl);
    p.s2c(0x0C9, equipInspect);
    p.s2c(0x0CA, inspectMessage);
    p.s2c(0x0CC, linkshellMessage);
    p.s2c(0x0D2, trophyList);
    p.s2c(0x0D3, trophySolution);
    p.s2c(0x0DC, groupSolicitReq);
    p.s2c(0x0DD, groupList);
    p.s2c(0x0DF, groupAttr);
    p.s2c(0x0E0, groupComlink);
    p.s2c(0x0E2, groupList);
    p.s2c(0x0FA, myroomOperation);
    p.s2c(0x105, bazaarList);
    p.s2c(0x109, bazaarSell);
}

} // namespace console
