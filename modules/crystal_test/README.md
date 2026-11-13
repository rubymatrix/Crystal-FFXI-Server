# crystal_test

GM tools for testing a server, mainly against the 2010 PS2 client. Load it only on test servers: add a
`crystal_test` line to `modules/init.txt` there. Never list it on a live server.

## `!story`

Steps a character through the storylines up to Treasures of Aht Urhgan: San d'Oria, Bastok, Windurst,
Rise of the Zilart, Chains of Promathia and Treasures of Aht Urhgan.

| Command | What it does |
|---|---|
| `!story` | Help, and the storyline currently selected. |
| `!story lines` | Every storyline with its current mission and status. |
| `!story start <line> [mission]` | Selects a storyline (`sandoria`, `bastok`, `windurst`, `nation`, `rotz`, `cop`, `toau`). With a mission (`2-1`, `5_3`, `07`) it jumps there: earlier missions are marked complete, the status is reset, and for nation missions the nation and rank are set. |
| `!story show [line]` | Lists what advances the current mission, best first, without changing anything. |
| `!story next [line] [n]` | Gives what step `n` (default 1) needs and teleports the character there. |

There is no walkthrough table. The command reads the mission scripts in `scripts/missions/` the way the
interaction framework does, so it follows them as they change:

- It runs every section `check` of the storyline's missions against a stand-in for the character. Sections
  that pass give the NPCs, mobs, zone-in cutscenes and trigger areas that act now. Progress events rank
  above ordinary events; flavour messages are left out.
- Trade steps are found by calling the NPC's `onTrade` with an empty trade while the `npcUtil` trade checks
  record what they look for. Those items (and gil) are given before the teleport.
- If nothing passes, the checks run again as if the character held every key item and item. A section that
  passes only then is waiting on the key items and items it asked for, and `!story next` gives them.
- Mission battlefields come from `xi.battlefield.contents`: the one for the current mission and status is a
  step at its entrance NPC, with its required key items and trade items given. Fights are never won for you.
- A section that only reacts to an event finishing (accepting a nation mission from a gate guard's menu,
  walking into a geyser in Dangruf Wadi for Bastok 1-2) is traced to what starts that event: each NPC's own
  `onTrigger` and the zone's `onTriggerAreaEnter` are dry-run, and the ones that start it become steps.
  Trigger area positions are read from the `register...TriggerArea` lines in the zone's `Zone.lua`; a
  cylinder has no height, so the first walkable height on the navmesh is used.

- Between nation missions the gate guards only list the next mission once the rank and the rank bar allow it
  (`xi.mission.getMissionMask`). `!story next` raises the rank to the next mission's (`2-1` needs rank 2)
  and fills the rank bar to 4000, the cap, which covers every mission's crystal requirement. `!story show`
  says what it would set.

Handlers run during the dry run cannot change the character or the NPC: every method that is not a getter
or `has` check is ignored, and event starts are only recorded. Global functions a script calls still run (a
geyser's visual effect, for example).

Known limits: a step that needs a quest, fame, a level or rank points outside the nation rank set by `start`
is not detected; `!story show` then lists nothing, and the mission script says what it is waiting for.
