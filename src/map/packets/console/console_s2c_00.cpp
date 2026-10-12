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

// Server packets 0x000-0x01B for the 2010 PS2 client.
// Packets not listed here are sent unchanged, or dropped if the client has no handler for them.
// Layouts are in FFXI-PS2 docs/packets/s2c_00.md.

#include "console_groups.h"

#include "map_session.h"

#include <algorithm>
#include <cstring>

namespace console
{

void registerS2C_00(compat::Profile& p);

namespace
{

// The 2010 client knows jobs WAR through SCH (1-20).
constexpr uint8  kLastJob  = 20;
constexpr uint32 kJobMask  = (1u << (kLastJob + 1)) - 1;
constexpr uint8  kFallback = 1; // WAR

// Replaces a main job the 2010 client doesn't know with WAR.
auto clampMainJob(const uint8 job) -> uint8
{
    return job > kLastJob ? kFallback : job;
}

// Clears a sub job the 2010 client doesn't know.
auto clampSubJob(const uint8 job) -> uint8
{
    return job > kLastJob ? 0 : job;
}

// Keeps the chocobo index (bits 5-7 of the first flags byte) in the 2010 range.
// 1 is a rented chocobo and 2 a raised one. Later mounts are shown as a rented chocobo.
auto clampChocoboIndex(const uint8 flags) -> uint8
{
    if ((flags >> 5) > 2)
    {
        return static_cast<uint8>((flags & 0x1F) | (1 << 5));
    }

    return flags;
}

// 0x00A - LOGIN
// Same layout as today. Only values the 2010 client can't show are fixed.
auto login(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    // The Mounted effect can put a later mount here.
    packet.ref<uint8>(0x20) = clampChocoboIndex(packet.ref<uint8>(0x20));

    // The 2010 client reads 0x7C as a 32-bit ship timer, so the Monstrosity flag must be 0.
    packet.ref<uint16>(0x7E) = 0;

    // Mog House models newer than the 2010 client are swapped for ones it has.
    auto& myroom = packet.ref<uint16>(0xAA);
    if (myroom == 745)
    {
        myroom = 0xBD; // San d'Oria [S]
    }
    else if (myroom == 0x124 || (myroom >= 0x267 && myroom <= 0x26A) || myroom == 0x2D9)
    {
        myroom = 0x100; // Adoulin, Mog House 2F, Feretory
    }

    // The server sends the Mog House exit as an index (1 San d'Oria to 5 Aht Urhgan).
    // The 2010 client reads it as a bit mask, one bit per Mog House.
    auto& exitBit = packet.ref<uint8>(0xAE);
    exitBit       = (exitBit >= 1 && exitBit <= 5) ? static_cast<uint8>(1u << (exitBit - 1)) : 0;

    // Clear jobs the 2010 client doesn't know.
    packet.ref<uint8>(0xB4)  = clampMainJob(packet.ref<uint8>(0xB4));
    packet.ref<uint8>(0xB7)  = clampSubJob(packet.ref<uint8>(0xB7));
    packet.ref<uint32>(0xB8) = packet.ref<uint32>(0xB8) & kJobMask;

    return Result::Rewritten;
}

// 0x00D - CHAR_PC
// The 2010 client has no Monstrosity, Geomancy, hitbox or flags6 fields.
// The model and name are moved 10 bytes earlier to close the gap.
auto charPc(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    // Bounds check the entity list for the 2010 client. PCs must sit at 0x400-0x6FF.
    const auto actIndex = rw.in<uint16>(0x08);
    if (actIndex < 0x400 || actIndex >= 0x700)
    {
        return Result::Drop;
    }

    const auto sendFlg = rw.in<uint8>(0x0A);

    // The client reads up to the pet index, plus the model when it's sent.
    size_t newSize = 0x40;
    if (sendFlg & 0x10)
    {
        newSize = 0x50;
    }

    // The client takes the name length from the packet size, up to 15 bytes.
    size_t nameLen = 0;
    if (sendFlg & 0x08)
    {
        nameLen = strnlen(reinterpret_cast<const char*>(rw.src + 0x5A), 15);
        newSize = std::max<size_t>(0x50, 0x54 + nameLen);
    }

    rw.clear(newSize);
    rw.move(0x04, 0x04, 0x3A);
    rw.move(0x48, 0x3E, 0x12);
    if (nameLen > 0)
    {
        rw.move(0x5A, 0x50, nameLen);
    }

    rw.out<uint8>(0x20, clampChocoboIndex(rw.in<uint8>(0x20)));

    return Result::Rewritten;
}

// 0x00E - CHAR_NPC
// Bounds check the entity list for the 2010 client. NPCs must sit at 0x000-0x3FF or 0x700-0x7FF.
auto charNpc(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    const auto actIndex = packet.ref<uint16>(0x08);
    if (actIndex < 0x400 || (actIndex >= 0x700 && actIndex < 0x800))
    {
        return Result::Pass;
    }

    return Result::Drop;
}

// 0x012 - GM
// 0x013 - GMCOMMAND
// The 2010 client copies these into a 160-byte buffer, so anything over 0xA4 bytes overflows it.
// The server doesn't send them today. This caps them in case it starts.
auto gm(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    constexpr size_t kMaxSize = 0xA4;

    if (packet.getSize() <= kMaxSize)
    {
        return Result::Pass;
    }

    packet.setSize(kMaxSize);
    packet.ref<uint8>(kMaxSize - 1) = 0;

    return Result::Rewritten;
}

// Maps a chat type to one the 2010 client knows (0x00-0x19). Returns 0xFF to drop the message.
auto chatKind(const uint8 kind) -> uint8
{
    if (kind <= 0x19)
    {
        return kind;
    }

    switch (kind)
    {
        case 26:      // yell
            return 1; // shout
        case 27:      // linkshell 2
        case 30:      // linkshell 3
            return 5; // linkshell
        case 28:      // linkshell 2, no speaker
        case 31:      // linkshell 3, no speaker
        case 32:
            return 16; // linkshell, no speaker
        case 29:       // system 3
            return 6;  // system 1
        case 33:       // unity
        case 34:       // assist (J)
        case 35:       // assist (E)
            return 1;  // shout: a server-wide "name : text" line
        default:
            return 0xFF;
    }
}

// 0x017 - CHAT_STD
// The 2010 client has a shorter header: GM flag, name, then chat type, with no zone or assist data.
auto chatStd(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    Rewrite rw(packet);

    const auto kind = chatKind(rw.in<uint8>(0x04));
    if (kind == 0xFF)
    {
        return Result::Drop;
    }

    // The 2010 client takes at most 150 bytes of text.
    const size_t oldSize = packet.getSize();
    const size_t avail   = oldSize > 0x17 ? std::min<size_t>(oldSize - 0x17, 0x96) : 0;
    const size_t mesLen  = strnlen(reinterpret_cast<const char*>(rw.src + 0x17), avail);

    rw.clear(0x15 + mesLen);
    rw.out<uint8>(0x04, rw.in<uint8>(0x05) & 0x01);
    rw.move(0x08, 0x05, 15);
    rw.out<uint8>(0x14, kind);
    rw.move(0x17, 0x15, mesLen);

    return Result::Rewritten;
}

// 0x01B - JOB_INFO
// Same layout up to 0x63. The mentor and mastery fields after that are newer than the 2010 client.
auto jobInfo(MapSession* /* PSession */, CBasicPacket& packet) -> Result
{
    packet.ref<uint8>(0x08)  = clampMainJob(packet.ref<uint8>(0x08));
    packet.ref<uint8>(0x0B)  = clampSubJob(packet.ref<uint8>(0x0B));
    packet.ref<uint32>(0x0C) = packet.ref<uint32>(0x0C) & kJobMask;

    // Clear the levels of jobs the 2010 client doesn't know.
    for (size_t job = kLastJob + 1; job < 0x18; ++job)
    {
        packet.ref<uint8>(0x48 + job) = 0;
    }

    packet.setSize(0x64);

    return Result::Rewritten;
}

} // namespace

void registerS2C_00(compat::Profile& p)
{
    p.s2c(0x00A, login);
    p.s2c(0x00D, charPc);
    p.s2c(0x00E, charNpc);
    p.s2c(0x012, gm);
    p.s2c(0x013, gm);
    p.s2c(0x017, chatStd);
    p.s2c(0x01B, jobInfo);
}

} // namespace console
