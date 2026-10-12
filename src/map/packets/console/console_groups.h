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

#include "packets/basic.h"

#include <algorithm>
#include <cstring>

// Translator groups for the 2010 client, plus small helpers the translators share.
// Each console_<dir>_<group>.cpp file defines one register function.

namespace console
{

void registerS2C_00(compat::Profile& p); // console_s2c_00.cpp: 0x000-0x01B
void registerS2C_1C(compat::Profile& p); // console_s2c_1c.cpp: 0x01C-0x03F
void registerS2C_40(compat::Profile& p); // console_s2c_40.cpp: 0x040-0x07F
void registerS2C_80(compat::Profile& p); // console_s2c_80.cpp: 0x080-0x10F
void registerC2S(compat::Profile& p);    // console_c2s.cpp

// Registers every translator group on a profile.
inline void registerGroups(compat::Profile& p)
{
    registerS2C_00(p);
    registerS2C_1C(p);
    registerS2C_40(p);
    registerS2C_80(p);
    registerC2S(p);
}

// The server keeps TP as 0-3000. The 2010 client stores it as a percent (0-300).
// Any TP above 0 is sent as at least 1.
inline auto tpPercent(const uint32 tp) -> uint32
{
    return tp == 0 ? 0 : std::max<uint32>(1, tp / 10);
}

// Rewrites a packet from a saved copy of the original.
// Reading from the copy lets fields move in either direction without overwriting each other.
struct Rewrite
{
    CBasicPacket& packet;
    uint8         src[0x200]{};

    explicit Rewrite(CBasicPacket& p)
    : packet(p)
    {
        std::memcpy(src, static_cast<uint8*>(p), 0x1FF);
    }

    template <typename T>
    auto in(const size_t off) const -> T
    {
        T v;
        std::memcpy(&v, src + off, sizeof(T));
        return v;
    }

    template <typename T>
    void out(const size_t off, const T v)
    {
        std::memcpy(static_cast<uint8*>(packet) + off, &v, sizeof(T));
    }

    // Copies n bytes from the original offset `from` to the new offset `to`.
    void move(const size_t from, const size_t to, const size_t n)
    {
        std::memcpy(static_cast<uint8*>(packet) + to, src + from, n);
    }

    // Zeroes everything after the 4-byte header and sets the new size. The size must be a multiple of 4.
    void clear(const size_t newSize)
    {
        std::memset(static_cast<uint8*>(packet) + 4, 0, 0x1FF - 4);
        packet.setSize(newSize);
    }
};

} // namespace console
