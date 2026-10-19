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

#include "guards.h"

#include "common/ipp.h"
#include "common/logging.h"
#include "common/timer.h"

#include "ai/ai_container.h"
#include "ai/states/attack_state.h"
#include "entities/char_entity.h"
#include "event_info.h"
#include "map_session.h"
#include "packets/basic.h"
#include "packets/s2c/0x052_eventucoff.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace compat::guards
{

namespace
{

// An event or sequence the server has had open this long, while the 2010 client talks to an NPC, is stale:
// the client cannot send a Talk while it runs an event, so it never got this one. The margin covers an event
// packet that is still on its way.
constexpr auto kStaleAfter = std::chrono::seconds(5);

// A Lua sequence still open after this long, with no event, gets a release.
constexpr auto kSequenceTimeout = std::chrono::seconds(30);

// Log the resend streak at these lengths.
constexpr uint32 kResendLog[] = { 20, 100, 500 };

// One warning per unknown address per this long.
constexpr auto kUnknownAddressQuiet = std::chrono::seconds(30);

struct State
{
    timer::time_point busySince{};     // first seen in an event or sequence after a client packet; zero when idle
    timer::time_point lastEventSent{}; // last event packet (0x032-0x034) that left for the client
    uint32            resendStreak = 0;
    timer::time_point resendSince{};
};

std::mutex                                    gMutex;
std::unordered_map<uint32, State>             gStates;  // by charid
std::unordered_map<uint64, timer::time_point> gUnknown; // by raw IPP, last warning

auto isTalk(CBasicPacket& packet) -> bool
{
    return packet.getType() == 0x01A && packet.getSize() >= 0x0C && packet.ref<uint16>(0x0A) == 0;
}

auto seconds(const timer::duration d) -> double
{
    return std::chrono::duration<double>(d).count();
}

auto openWork(CCharEntity* PChar) -> std::string
{
    if (PChar->isInEvent())
    {
        return fmt::format("event {}", PChar->currentEvent->eventId);
    }
    return PChar->inSequence ? "a Lua sequence" : "nothing";
}

void release(CCharEntity* PChar)
{
    PChar->pushPacket<GP_SERV_COMMAND_EVENTUCOFF>(PChar, GP_SERV_COMMAND_EVENTUCOFF_MODE::Standard);
}

} // namespace

void beforeDispatch(MapSession* PSession, CCharEntity* PChar, CBasicPacket& packet)
{
    if (!PSession->client.translated() || !isTalk(packet) || !PChar->isNpcLocked())
    {
        return;
    }

    const auto        now = timer::now();
    timer::time_point since{};
    {
        std::lock_guard lock(gMutex);
        const auto&     s = gStates[PChar->id];
        since             = std::max(s.busySince, s.lastEventSent);
    }

    // An event that just started may still be on its way to the client; leave it.
    if (since != timer::time_point{} && now - since < kStaleAfter)
    {
        return;
    }

    // Its packet may not even have left yet (an event started by the server between two datagrams).
    for (const auto& queuedPacket : PChar->getPacketList())
    {
        const auto type = queuedPacket->getType();
        if (type >= 0x032 && type <= 0x034)
        {
            return;
        }
    }

    ShowWarningFmt("guards: {} talked to id {:#x} index {} while the server still had {} open{}. The 2010 client cannot talk during an event, so it never got it: closing it.",
                   PChar->getName(),
                   packet.ref<uint32>(0x04),
                   packet.ref<uint16>(0x08),
                   openWork(PChar),
                   since == timer::time_point{} ? std::string() : fmt::format(" for {:.1f}s", seconds(now - since)));

    PChar->inSequence = false;
    if (PChar->isInEvent())
    {
        PChar->endCurrentEvent();
    }
}

void afterDispatch(MapSession* PSession, CCharEntity* PChar, CBasicPacket& packet, const size_t queued, const bool dropped)
{
    if (!PSession->client.translated())
    {
        return;
    }

    const auto        now    = timer::now();
    const bool        locked = PChar->isNpcLocked();
    timer::time_point busySince{};
    {
        std::lock_guard lock(gMutex);
        auto&           s = gStates[PChar->id];
        if (!locked)
        {
            s.busySince = {};
        }
        else if (s.busySince == timer::time_point{})
        {
            s.busySince = now;
        }
        busySince = s.busySince;
    }

    // A sequence that never released. Nothing else would ever free the client.
    if (PChar->inSequence && !PChar->isInEvent() && busySince != timer::time_point{} && now - busySince > kSequenceTimeout)
    {
        ShowWarningFmt("guards: {} has been in a Lua sequence for {:.0f}s with no release: releasing.", PChar->getName(), seconds(now - busySince));
        PChar->inSequence = false;
        release(PChar);
        std::lock_guard lock(gMutex);
        gStates[PChar->id].busySince = {};
    }

    if (!isTalk(packet))
    {
        return;
    }

    // An event or a release queued for this Talk answers it.
    const auto& list = PChar->getPacketList();
    for (size_t i = std::min(queued, list.size()); i < list.size(); ++i)
    {
        const auto type = list[i]->getType();
        if ((type >= 0x032 && type <= 0x034) || type == 0x052)
        {
            return;
        }
    }

    // An event already open (its packet is on its way) or a sequence just started answers it later.
    if (PChar->isNpcLocked())
    {
        return;
    }

    ShowWarningFmt("guards: Talk from {} to id {:#x} index {} got no answer ({}): sending a release.",
                   PChar->getName(),
                   packet.ref<uint32>(0x04),
                   packet.ref<uint16>(0x08),
                   dropped ? "dropped before the server saw it" : "the server sent nothing");
    release(PChar);
}

void onRejected(MapSession* PSession, CCharEntity* PChar, CBasicPacket& packet)
{
    if (!PSession->client.translated())
    {
        return;
    }

    const auto type = packet.getType();
    if (type == 0x01A && packet.getSize() >= 0x0C)
    {
        const auto action = packet.ref<uint16>(0x0A);
        if (action != 0x02 && action != 0x04 && action != 0x0F) // Attack, AttackOff, ChangeTarget
        {
            return;
        }

        const bool attackAnimation = PChar->animation == xi::Animation::Attack;
        const bool attackState     = PChar->PAI->IsCurrentState<CAttackState>();
        ShowWarningFmt("guards: {} sent action {} and it was rejected. Server: animation {}, attack state {}, {}. Sending the real status.",
                       PChar->getName(),
                       action,
                       static_cast<int>(PChar->animation),
                       attackState ? "running" : "not running",
                       openWork(PChar));

        // The animation was overwritten (event, /sit, Lua) while the fight went on. AttackOff could never pass.
        if (action == 0x04 && attackState && !attackAnimation)
        {
            PChar->PAI->Disengage();
        }

        PChar->updatemask |= UPDATE_HP; // a 0x037 with the current status on the next tick
        return;
    }

    if ((type == 0x05B && packet.getSize() >= 0x14) || (type == 0x05C && packet.getSize() >= 0x1C))
    {
        ShowWarningFmt("guards: {} ended event {} but the server has {}.",
                       PChar->getName(),
                       packet.ref<uint16>(type == 0x05B ? 0x12 : 0x1A),
                       openWork(PChar));
    }
}

auto afterTranslateS2C(MapSession* PSession, CBasicPacket& packet, const uint16 type, const uint16 eventId, const bool dropped) -> bool
{
    if (type < 0x032 || type > 0x034)
    {
        return false;
    }

    auto* PChar = PSession->PChar.get();
    if (!dropped)
    {
        if (PChar && packet.getType() >= 0x032 && packet.getType() <= 0x034)
        {
            std::lock_guard lock(gMutex);
            gStates[PChar->id].lastEventSent = timer::now();
        }
        return false;
    }

    // The 2010 client would wait for this event forever, and the server would sit in it.
    ShowWarningFmt("guards: event {} for {} was dropped on the way out (its NPC has no 2010 id?): sending a release and ending the event.",
                   eventId,
                   PChar ? PChar->getName() : "?");
    if (PChar && PChar->currentEvent && PChar->currentEvent->eventId == eventId)
    {
        PChar->endCurrentEvent();
    }

    packet.setType(0x052);
    packet.setSize(0x08);
    packet.ref<uint32>(0x04) = 0; // Standard
    return true;
}

void onResend(MapSession* PSession)
{
    if (!PSession->client.translated() || !PSession->PChar)
    {
        return;
    }

    uint32            streak = 0;
    timer::time_point since{};
    {
        std::lock_guard lock(gMutex);
        auto&           s = gStates[PSession->PChar->id];
        if (s.resendStreak++ == 0)
        {
            s.resendSince = timer::now();
        }
        streak = s.resendStreak;
        since  = s.resendSince;
    }

    if (std::find(std::begin(kResendLog), std::end(kResendLog), streak) != std::end(kResendLog))
    {
        ShowWarningFmt("guards: resent the same datagram to {} {} times in a row over {:.1f}s ({} bytes). The client is not acknowledging it, so nothing new reaches it.",
                       PSession->PChar->getName(),
                       streak,
                       seconds(timer::now() - since),
                       PSession->server_packet_size);
    }
}

void onNewDatagram(MapSession* PSession)
{
    if (!PSession->client.translated() || !PSession->PChar)
    {
        return;
    }

    uint32 streak = 0;
    {
        std::lock_guard lock(gMutex);
        streak = std::exchange(gStates[PSession->PChar->id].resendStreak, 0);
    }

    if (streak >= kResendLog[0])
    {
        ShowInfoFmt("guards: {} acknowledged again after {} resends.", PSession->PChar->getName(), streak);
    }
}

void onUnknownAddress(const IPP& ipp, const std::vector<MapSession*>& sameIp)
{
    std::string known;
    for (const auto* PSession : sameIp)
    {
        if (!PSession->client.translated())
        {
            continue;
        }
        known += fmt::format("{}{} from port {} (last packet {:.0f}s ago)",
                             known.empty() ? "" : ", ",
                             PSession->PChar ? PSession->PChar->getName() : fmt::format("charid {}", PSession->charID),
                             PSession->client_ipp.getPort(),
                             std::chrono::duration<double>(earth_time::now() - PSession->last_update).count());
    }
    if (known.empty())
    {
        return;
    }

    const auto now = timer::now();
    {
        std::lock_guard lock(gMutex);
        auto&           last = gUnknown[ipp.getRawIPP()];
        if (last != timer::time_point{} && now - last < kUnknownAddressQuiet)
        {
            return;
        }
        last = now;
        if (gUnknown.size() > 1024)
        {
            gUnknown.clear();
        }
    }

    ShowWarningFmt("guards: datagram from {} matches no session, but {} has one from the same IP. A router may have moved the client to a new port; the server ignores it.",
                   ipp.toString(),
                   known);
}

} // namespace compat::guards
