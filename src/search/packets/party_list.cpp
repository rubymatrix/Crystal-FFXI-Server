/*
===========================================================================

  Copyright (c) 2010-2015 Darkstar Dev Teams

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

#include "common/utils.h"

#include "data_loader.h"
#include "enums/search_type.h"
#include "party_list.h"
#include "search.h"

#include <algorithm>
#include <cstddef>

CPartyListPacket::CPartyListPacket(const uint32 partyid, const uint32 Total, const SearchListLayout layout)
: m_offset(192)
, m_layout(layout)
{
    ref<uint8>(m_data, (0x0A)) = 0x80;
    ref<uint8>(m_data, (0x0B)) = 0x82; // packet type

    ref<uint8>(m_data, (0x0E)) = Total; // Number of characters per packet.
}

/************************************************************************
 *                                                                       *
 *  Add the player to the packet.                                        *
 *                                                                       *
 ************************************************************************/

auto CPartyListPacket::AddPlayer(const SearchEntity& player) -> bool
{
    const uint32 size_offset = m_offset / 8;
    if ((sizeof(m_data) - size_offset) < (searchPacketTrailerSize + searchEntryMaxSize))
    {
        return false; // not enough space available, worst case.
    }

    m_offset += 8;

    m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Name), m_offset, 5);

    const auto length = std::min(player.name.size(), size_t(15));
    m_offset          = packBitsLE(m_data, length, m_offset, 4);

    for (std::size_t c = 0; c < length; ++c)
    {
        m_offset = packBitsLE(m_data, player.name[c], m_offset, 7);
    }

    // The 2010 PS2 client reads members like search results: see SearchListLayout
    const bool ps2         = m_layout == SearchListLayout::Ps2_2010;
    const auto areaBits    = ps2 ? 8 : 10;
    const auto areaValue   = ps2 ? (player.zone <= 0xFF ? player.zone : 0) : player.zone;
    const auto flags2Tag   = ps2 ? uint64(0x14) : static_cast<uint64>(SearchType::Flags2);
    const auto languageTag = ps2 ? uint64(0x15) : static_cast<uint64>(SearchType::Language);

    m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Area), m_offset, 5);
    m_offset = packBitsLE(m_data, areaValue, m_offset, areaBits);

    if (!(player.flags1 & 0x4000))
    {
        m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Nation), m_offset, 5);
        m_offset = packBitsLE(m_data, player.nation, m_offset, 2);

        m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Job), m_offset, 5);
        m_offset = packBitsLE(m_data, player.mjob, m_offset, 5);
        m_offset = packBitsLE(m_data, player.sjob, m_offset, 5);

        m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Level), m_offset, 5);
        m_offset = packBitsLE(m_data, player.mlvl, m_offset, 8);
        m_offset = packBitsLE(m_data, player.slvl, m_offset, 8);

        m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Race), m_offset, 5);
        m_offset = packBitsLE(m_data, player.race, m_offset, 4);

        m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Rank), m_offset, 5);
        m_offset = packBitsLE(m_data, player.rank, m_offset, 8);
    }

    m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Flags1), m_offset, 5);
    m_offset = packBitsLE(m_data, player.flags1, m_offset, 16);

    m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Id), m_offset, 5);
    m_offset = packBitsLE(m_data, player.id, m_offset, 20);

    // m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::LinkshellRank),  m_offset, 5);
    // m_offset = packBitsLE(m_data, 0, m_offset,8);

    m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Unknown0E), m_offset, 5);
    m_offset = packBitsLE(m_data, 0, m_offset, 32);

    if (player.seacom_type != 0)
    {
        m_offset = packBitsLE(m_data, static_cast<uint64>(SearchType::Comment), m_offset, 5);
        m_offset = packBitsLE(m_data, player.seacom_type, m_offset, 32);
    }

    m_offset = packBitsLE(m_data, flags2Tag, m_offset, 5);
    m_offset = packBitsLE(m_data, player.flags2, m_offset, 32);

    m_offset = packBitsLE(m_data, languageTag, m_offset, 5);
    m_offset = packBitsLE(m_data, player.languages, m_offset, 16);

    if (m_offset % 8 > 0)
    {
        m_offset += 8 - m_offset % 8; // Byte alignment
    }

    ref<uint8>(m_data, size_offset) = m_offset / 8 - size_offset - 1; // Entity data size
    ref<uint16>(m_data, (0x08))     = m_offset / 8;                   // Size of the data to send

    return true;
}

/************************************************************************
 *                                                                       *
 *  Returns the packet's data.                                           *
 *                                                                       *
 ************************************************************************/

auto CPartyListPacket::GetData() -> uint8*
{
    return m_data;
}

/************************************************************************
 *                                                                       *
 *  Returns the size of the packet.                                      *
 *                                                                       *
 ************************************************************************/

auto CPartyListPacket::GetSize() const -> uint16
{
    return m_offset / 8 + searchPacketTrailerSize;
}
