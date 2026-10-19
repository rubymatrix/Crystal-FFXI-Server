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

#pragma once

#include "common/cbasetypes.h"

#include <cstddef>
#include <vector>

// Soft-lock guards for console clients (sessions with a translation profile).
//
// The 2010 PS2 client never times out a request. After a Talk it cannot move, open the menu or talk
// again until an event or a release (0x052) arrives, and it shows the player engaged until a 0x037 says
// otherwise. The server, on the other hand, answers nothing it rejects or skips. One lost answer therefore
// locks the player until they zone. These hooks make sure each request a console client waits on gets an
// answer, and log every case so the cause can be found. Clients without a profile are never touched.

class CBasicPacket;
class CCharEntity;
class IPP;
struct MapSession;

namespace compat::guards
{

// Before a client packet is dispatched (it passed translateC2S). A Talk while the server still has a stale
// event or sequence open closes it first.
void beforeDispatch(MapSession* PSession, CCharEntity* PChar, CBasicPacket& packet);

// After a client packet was dispatched, or dropped by its translator. queued is the size of PChar's packet
// list before it. A Talk that got neither an event nor a release is released.
void afterDispatch(MapSession* PSession, CCharEntity* PChar, CBasicPacket& packet, size_t queued, bool dropped);

// A client packet failed its size check or validate(). The server sends nothing for those on its own.
void onRejected(MapSession* PSession, CCharEntity* PChar, CBasicPacket& packet);

// An outgoing packet went through translateS2C. eventId is the event an event packet carried before it was
// translated. A dropped event packet is rewritten into a release and the server's event is ended; returns
// true when that happened, so the caller sends the packet after all.
auto afterTranslateS2C(MapSession* PSession, CBasicPacket& packet, uint16 type, uint16 eventId, bool dropped) -> bool;

// The server sent the cached datagram again because the client has not acknowledged it, or sent a new one.
void onResend(MapSession* PSession);
void onNewDatagram(MapSession* PSession);

// A datagram came from an address with no session. sameIp are the live sessions from that IP.
void onUnknownAddress(const IPP& ipp, const std::vector<MapSession*>& sameIp);

} // namespace compat::guards
