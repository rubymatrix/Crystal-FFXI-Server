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

#include "packets/compat/profile.h"

// Shared types for the console client translators.
// The 2010 client (SCUS-97266, patch 20100904_2) gets the "ps2-20100904" profile when its login says it's a PS2.
// Layouts come from the 2010 client's own packet handlers (FFXI-PS2 docs/packet-handlers.md).
// The "PS2:" notes in packets/s2c and packets/c2s describe the 2016 client. Where they disagree, trust the 2010 handlers.

class CBasicPacket;
struct MapSession;

namespace console
{

using Result     = compat::Result;
using Translator = compat::Translator;
using compat::combine;
using compat::emitS2C;

} // namespace console
