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

#include <memory>

namespace console
{

// Builds the profile for the 2010 client (SCUS-97266, patch 20100904_2).
// It uses the console_s2c_*.cpp and console_c2s.cpp translators and the id maps in res/compat/ps2-20100904/.
auto makeProfile20100904() -> std::unique_ptr<compat::Profile>;

} // namespace console
