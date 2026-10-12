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

// Server packets 0x040-0x07F for the 2010 PS2 client.
// Packets not listed here are sent unchanged, or dropped if the 2010 client has no handler for them.
// Layouts are in FFXI-PS2 docs/packets/s2c_40.md.

#include "console_groups.h"

#include "ability.h"
#include "entities/char_entity.h"
#include "map_session.h"
#include "packets/s2c/0x062_clistatus2.h"
#include "recast_container.h"

#include <algorithm>

namespace console
{

namespace
{

// Size of the 2010 client's entity list.
constexpr uint16 kEntityCount = 0x800;

// The 2010 client knows jobs WAR through SCH (1-20).
constexpr uint8 kMaxJob = 20;

// The 2010 client stores 31 ability recast timers.
constexpr std::size_t kRecastSlots = 31;

// 0x043 - TALKNUMNAME
// Bounds check the entity list for the 2010 client.
auto talkNumName(MapSession*, CBasicPacket& packet) -> Result
{
    if ((packet.ref<uint16>(0x0A) & 0x8000) == 0 && packet.ref<uint16>(0x08) >= kEntityCount)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x044 - EXTENDED_JOB
// Only BLU and PUP exist on the 2010 client. MON and other later jobs are dropped.
auto extendedJob(MapSession*, CBasicPacket& packet) -> Result
{
    const auto job = packet.ref<uint8>(0x04);
    if (job == 0 || job > kMaxJob || packet.ref<uint8>(0x05) > 1)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x04B - PBX_RESULT
// Bounds check the delivery box slot for the 2010 client.
auto pbxResult(MapSession*, CBasicPacket& packet) -> Result
{
    const auto command = packet.ref<uint8>(0x04);
    const auto result  = packet.ref<int8>(0x0C);
    const auto slot    = packet.ref<int8>(0x06);

    const bool unchecked = command == 3 || command == 4 || command == 6 || command == 7 || command == 8 || command == 9;
    if (unchecked && (result == 1 || result == 2) && (slot < 0 || slot >= 8))
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x050 - EQUIP_LIST
// The 2010 client assumes equipped items are in the inventory. Items equipped from a wardrobe
// are shown as an empty slot, otherwise it would show whatever is in that inventory slot.
auto equipList(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint8>(0x05) >= 16)
    {
        return Result::Drop;
    }

    if (packet.ref<uint8>(0x06) != 0)
    {
        packet.ref<uint8>(0x04) = 0;
        packet.ref<uint8>(0x06) = 0;
        return Result::Rewritten;
    }

    return Result::Pass;
}

// 0x051 - GRAP_LIST
// The 2010 client has no costumes or monstrosity. It would read the costume model as race and face.
auto grapList(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint16>(0x14) == 0xFFFF)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x055 - SCENARIOITEM
// The 2010 client only has the first 4 key item tables. The rest are newer key items.
auto scenarioItem(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint16>(0x84) >= 4)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x056 - MISSION
// Mission logs newer than the 2010 client (Adoulin, Coalition, TVR) are dropped.
auto mission(MapSession*, CBasicPacket& packet) -> Result
{
    const auto port = packet.ref<uint16>(0x24);
    if (port == 0xFFFF || (port >= 0x30 && port <= 0x3F) || (port >= 0x50 && port <= 0xEF))
    {
        return Result::Pass;
    }

    return Result::Drop;
}

// 0x05A - MOTIONMES
// Bounds check the emote and the entity list for the 2010 client.
auto motionMes(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint16>(0x10) >= 0x4A || packet.ref<uint16>(0x0C) >= kEntityCount)
    {
        return Result::Drop;
    }

    if (packet.ref<uint32>(0x08) != 0 && packet.ref<uint16>(0x0E) >= kEntityCount)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x05B - WPOS
// 0x065 - WPOS2
// Bounds check the entity list for the 2010 client.
auto wpos(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint16>(0x14) >= kEntityCount)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x061 - CLISTATUS
// Everything after the Mog House field (item level, unity, mastery) is newer than the 2010 client.
auto cliStatus(MapSession*, CBasicPacket& packet) -> Result
{
    packet.setSize(0x54);
    return Result::Rewritten;
}

// Fills the ability recast slots of a CLISTATUS2.
// Each slot is: seconds left (16 bits), charge reduction in 1/60ths (8 bits), recast id (8 bits).
// The 2010 client finds an ability's timer by its recast id. Slot 0 is the 2-hour.
void fillRecasts(CCharEntity* PChar, CBasicPacket& packet)
{
    std::memset(packet[0x04], 0, kRecastSlots * 4);

    std::size_t slot = 1;
    for (const auto& recast : *PChar->PRecastContainer->GetRecastList(RECAST_ABILITY))
    {
        if (recast.ID == Recast::Mount)
        {
            continue;
        }

        const auto remaining = recast.RecastTime == 0s ? 0s : std::chrono::ceil<std::chrono::seconds>(recast.TimeStamp - timer::now() + recast.RecastTime);
        const auto seconds   = static_cast<uint32>(std::clamp<int64>(timer::count_seconds(remaining), 0, 0xFFFF));

        if (recast.ID == Recast::Special)
        {
            packet.ref<uint32>(0x04) = seconds;
            continue;
        }

        uint32 reduction = 0;
        if (recast.maxCharges != 0)
        {
            if (const auto* charge = ability::GetCharge(PChar, static_cast<uint16>(recast.ID)))
            {
                const auto base   = timer::count_seconds(charge->chargeTime);
                const auto actual = timer::count_seconds(recast.chargeTime);
                if (base > actual)
                {
                    reduction = static_cast<uint32>(std::min<int64>((60 * (base - actual) + base / 2) / base, 0x3F));
                }
            }
        }

        if (slot >= kRecastSlots)
        {
            break;
        }
        packet.ref<uint32>(0x04 + 4 * slot++) = seconds | reduction << 16 | (static_cast<uint32>(recast.ID) & 0xFF) << 24;
    }
}

// 0x062 - CLISTATUS2
// The 2010 client reads ability recasts from this packet. The server leaves them empty and sends ABIL_RECAST instead.
auto cliStatus2(MapSession* PSession, CBasicPacket& packet) -> Result
{
    if (auto* PChar = PSession->PChar.get())
    {
        fillRecasts(PChar, packet);
        return Result::Rewritten;
    }

    return Result::Pass;
}

// 0x063 - MISCDATA
// The 2010 client only knows the merits type. The others are newer.
auto miscData(MapSession*, CBasicPacket& packet) -> Result
{
    if (packet.ref<uint16>(0x04) != 2)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x067 - CHAR_SYNC
// The 2010 client only handles sync, entity name and pet. Bounds check the entity list for it.
// Sync: the mount id overlaps a field the 2010 client reads, so it's cleared.
// Entity name: the 2010 client reads the name 4 bytes earlier than the server writes it.
auto charSync(MapSession*, CBasicPacket& packet) -> Result
{
    const auto kind = packet.ref<uint8>(0x04) & 0x3F;

    if (kind == 4)
    {
        return packet.ref<uint16>(0x0C) < kEntityCount ? Result::Pass : Result::Drop;
    }

    if (kind != 2 && kind != 3)
    {
        return Result::Drop;
    }

    if (packet.ref<uint16>(0x06) >= kEntityCount)
    {
        return Result::Drop;
    }

    if (kind == 2)
    {
        packet.ref<uint8>(0x13) = 0;
        packet.ref<uint8>(0x14) = 0;
        return Result::Rewritten;
    }

    Rewrite rw(packet);
    rw.move(0x18, 0x14, 0x18);
    return Result::Rewritten;
}

// 0x068 - PET_SYNC
// The 2010 client has no PET_SYNC. It gets the same data as the pet type of CHAR_SYNC.
// Note: unhandled packets are dropped before this runs, so it's inactive until 0x068 is allowed through.
auto petSync(MapSession*, CBasicPacket& packet) -> Result
{
    const auto pet = packet.ref<uint16>(0x0C);
    if (pet == 0 || pet >= kEntityCount)
    {
        return Result::Drop;
    }

    packet.ref<uint32>(0x10) = tpPercent(packet.ref<uint16>(0x10));
    packet.setType(0x067);
    return Result::Rewritten;
}

// 0x069 - CHOCOBO_RACING
// Bounds check the race tables for the 2010 client.
auto chocoboRacing(MapSession*, CBasicPacket& packet) -> Result
{
    const auto   mode  = packet.ref<uint8>(0x04);
    const size_t index = packet.ref<uint8>(0x05);
    const size_t size  = packet.ref<uint8>(0x06);

    if (mode == 2 && index * 0x0C + size > 0x60)
    {
        return Result::Drop;
    }

    if (mode == 3 && index * 0x0C + size > 0x180)
    {
        return Result::Drop;
    }

    if (mode == 4 && size > 4)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x071 - INFLUENCE
// The 2010 client only knows Campaign. Colonization is Adoulin.
auto influence(MapSession*, CBasicPacket& packet) -> Result
{
    return packet.ref<uint8>(0x04) == 2 ? Result::Pass : Result::Drop;
}

// 0x074 - CHOCOBO_LIST
// Bounds check the chocobo table for the 2010 client.
auto chocoboList(MapSession*, CBasicPacket& packet) -> Result
{
    const auto mode = packet.ref<uint8>(0x10);
    if ((mode == 2 || mode == 3) && packet.ref<uint8>(0x11) != 0)
    {
        return Result::Drop;
    }

    return Result::Pass;
}

// 0x119 - ABIL_RECAST
// The 2010 client has no ABIL_RECAST. It's sent as a full CLISTATUS2, which is where it reads recasts from.
auto abilRecast(MapSession* PSession, CBasicPacket& packet) -> Result
{
    auto* PChar = PSession->PChar.get();
    if (PChar == nullptr)
    {
        return Result::Drop;
    }

    GP_SERV_COMMAND_CLISTATUS2 status(PChar);
    std::memcpy(packet[0x04], status[0x04], status.getSize() - 0x04);
    packet.setType(0x062);
    packet.setSize(status.getSize());
    fillRecasts(PChar, packet);
    return Result::Rewritten;
}

} // namespace

void registerS2C_40(compat::Profile& p)
{
    p.s2c(0x043, talkNumName);
    p.s2c(0x044, extendedJob);
    p.s2c(0x04B, pbxResult);
    p.s2c(0x050, equipList);
    p.s2c(0x051, grapList);
    p.s2c(0x055, scenarioItem);
    p.s2c(0x056, mission);
    p.s2c(0x05A, motionMes);
    p.s2c(0x05B, wpos);
    p.s2c(0x061, cliStatus);
    p.s2c(0x062, cliStatus2);
    p.s2c(0x063, miscData);
    p.s2c(0x065, wpos);
    p.s2c(0x067, charSync);
    p.s2c(0x068, petSync);
    p.s2c(0x069, chocoboRacing);
    p.s2c(0x071, influence);
    p.s2c(0x074, chocoboList);
    p.s2c(0x119, abilRecast);
}

} // namespace console
