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

#pragma once

#include "console_client.h"

namespace console
{

// Remap the static entity ids in one packet.
// S2C maps today's ids to the 2010 client's. C2S maps the 2010 client's ids back to today's.
auto remapEntitiesS2C(MapSession* PSession, CBasicPacket& packet) -> Result;
auto remapEntitiesC2S(MapSession* PSession, CBasicPacket& packet) -> Result;

} // namespace console
