# crystal

Project Crystal's own module.

## Turning off what the 2010 PS2 client lacks

Turns off the NPCs, mobs and items the 2010 PS2 client (patch `20100904_2`) has no entry for, so a server
that serves it doesn't run content its players can't see.

It uses the same compat maps the map server uses to translate packets for that client,
`res/compat/ps2-20100904/entity_map.bin` and `items.bin`. Anything the translator would hide from a 2010
session is turned off here:

- NPCs (`data/zones/*/npcs.yaml`): `status: disappear` and `widescan: false`, the same as the core does for an
  NPC whose content is off. Scripts can still find them.
- Mobs (`data/zones/*/mobs.yaml`): the spawn loses its template, so it never spawns. The index stays reserved.
- Mob loot (same files): rolls, steal and despoil entries for missing items are dropped. In a `one_of`, a
  missing item becomes `nothing`, so the other items keep their rates.
- `sql/crystal_instance_entities.sql`: instance NPCs and mobs, which are still in SQL.
- `sql/crystal_items.sql`: synth recipes, fishing, gardening and the scripted drop lists. `item_basic` and
  the other item tables are left alone, so characters that already hold a missing item still load.

This turns the content off for every player on the server, PC clients included.

## Using it

Add `crystal` as the **last** line of `modules/init.txt`. Loot lists are replaced whole by a data patch, so
these files were built on top of the patches listed before them. Then run `dbtool` to apply the SQL and restart
`xi_map`.

## Regenerating

The files above are generated and say so in their first lines. Don't edit them by hand; rerun the generator
after changing the zone data, `init.txt` or the compat maps:

```
python tools/client_2010_module.py <server root>
```

It replaces only the files it generated, so anything else added to this module is kept. The generator lives in
the PS2 project (rubymatrix/ffxi-ps2-main).
