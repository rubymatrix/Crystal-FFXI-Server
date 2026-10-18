/*
===========================================================================

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

#include <httplib.h>

// The API the PlayOnline lobby uses to reach this world: its characters, and entering the world. The lobby never
// touches this world's database; everything it needs from it goes through here.
//
// Every route is under /api/lobby and needs `Authorization: Bearer <network.LOBBY_API_KEY>`. With no key set the
// routes answer 404, so a world that does not serve a lobby exposes nothing. Bodies and answers are JSON. A
// character is named by the content id (chars.accid) it was made on and its charid. The PlayOnline account it
// belongs to is the numeric POL ID (chars.pol_id), which the lobby sends on create and on every enter.
//
//   POST /api/lobby/characters/list        {"characters":[{"contentId":1,"charId":1}, ...]}
//        200 {"characters":[{"found":true,"contentId":1,"charId":1,"name":"...", ...}, {"found":false}, ...]}
//        One answer per request, in order; not found also covers a character on another content id. charId 0
//        finds whichever character the content id holds.
//   POST /api/lobby/characters/check-name  {"name":"Foo"}                          200 {"error":0}
//   POST /api/lobby/characters/create      {"contentId","polId","name","race","face","size","job","nation","startZone"}
//        200 {"charId":3}. Sent again with the same content id, POL ID and name, it answers the same charid.
//   POST /api/lobby/characters/rename      {"contentId","charId","name"}           200 {}
//   POST /api/lobby/characters/delete      {"contentId","charId"}                  200 {}
//   POST /api/lobby/characters/online      {"charIds":[1,2]}                       200 {"online":false}
//   POST /api/lobby/sessions/enter         {"contentId","charId","key":"<40 hex>","serverAddr","serverPort",
//                                           "clientAddr","clientVersion","clientExpansions","account":{"polId"}}
//        200 {}. 409 while the character, or another character of the same POL ID, is in this world.
//
// A refusal is 4xx with {"error":<lobby error code>} for a name (313 unavailable, 314 name server failure) and
// {"error":0,"message":"..."} for anything else; 401 for a wrong key, 5xx when the database fails.
namespace lobby_api
{

void registerRoutes(httplib::Server& server);

} // namespace lobby_api
