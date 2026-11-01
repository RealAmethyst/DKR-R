# Hub destination beacon research

Research for #44: guidance in the hub world (Timber's Island, level 0,
`RACETYPE_HUBWORLD`). Checked against the matching decomp; positions decoded
offline from the US 1.0 (v77) object maps and level model (Rev A not compared).
Routes below come from an offline approximation and are not validated in game.

## Objects

- The hub spawns two object maps (`LevelHeader` +0xBA and the collectables map
  at +0x36, `track_spawn_objects`); Taj, the balloons and the Future Fun Land
  teleport are in the second.
- At run time: `gObjPtrList` / `gObjectCount`; Object position +0x0C,
  `level_entry` +0x3C, `behaviorId` +0x48; skip particles as `find_taj_object`.
- World exits, `BHV_EXIT` (7), `level_entry` +0x08 = destination level: Dino
  Domain (12) at (-4015, 383, 2683), Dragon Forest (2) at (609, 592, 4137),
  Snowflake Mountain (24) at (-3828, 405, -3653), Sherbet Island (14) at
  (5086, -123, -1187), level 37 (Wizpig) at (-1324, 551, 3138). An exit fires
  inside its radius when the racer crosses its facing plane
  (`object_functions.c`).
- World gates, `BHV_DOOR` (14), in pairs before each exit; balloons needed:
  Dino Domain 1, Snowflake Mountain 2, Sherbet Island 10, Dragon Forest 16. A
  gate is open when course flag `0x10000 << doorID` is set; touching a closed
  one shows game text 42 ("NO ENTRY!! / YOU NEED ~ BALLOONS", the count as the
  display value).
- Taj, `BHV_PARK_WARDEN` (62), roams the central lawn. The Taj pad is a
  surface (TAJPAD floor around the origin); Z on it summons Taj.
- Golden balloons, `BHV_GOLDEN_BALLOON` (77), removed once collected. Free from
  the start: (283, 544, 2677), (2097, 163, -1955), (-2446, 520, 1563),
  (3236, -284, -4252); Taj's prizes appear only after his challenges.
- Future Fun Land teleport, `BHV_TELEPORT` (98), at (5561, -264, 1384).
- Spawn points: a new or loaded adventure starts at (25, 259, 16), on the Taj
  pad; returning from each world starts beside its exit.
- Names: `level_name(levelId)` gives the localized world names, as the track
  select shows them. Taj, balloons, the lighthouse and the Wizpig head have no
  label in the game's text.

## Routes

- No path data in the hub joins the start to the worlds: Taj's challenge gates
  form three closed loops (car 11, hovercraft 21, plane 20 gates) round the
  island, and AI nodes only serve Taj and the balloons.
- The floor geometry (41 segments, 2,805 collision triangles, 1,412 floors,
  1,234 walls, rivers at about y 180, the sea at -301 with no floor) supports
  routing: an offline 40-unit grid found routes from the start to Dino Domain
  (5,239 against 4,841 in a straight line, over the hill past the balloon at
  (-2446, 520, 1563)), Dragon Forest and Future Fun Land. It failed for Sherbet
  Island (sea), the southern region including Snowflake Mountain, and three
  balloons: jumps, one-way drops, water-only areas and narrow gaps need either
  finer cells or authored links.

## Inputs

- Read while driving in the hub: stick, A, B, Z (horn, Taj, talking), R (hop,
  drift, stunts), START (pause), C-up (camera zoom), C-right (minimap or
  speedometer).
- Not read in the hub: the D-pad (no reads outside the menus), C-left, L (the
  game strips held L in `mode_game`), and C-down (course arrows only in races).
- DKR-R maps every N64 button to the host pad (`runtime_input.cpp`); D-pad
  Down + START is Quick Restart when enabled.

## Reference: the FFXIV pathfinder beacon

- One tone (`pathfinder.wav`, 0.125 s) repeating every 0.15 to 1.5 s by
  distance to the destination; placed on a route point a short look-ahead
  ahead, moving forward only; silent while replanning; "Arrived at" speech once
  on arrival; height through a low-pass filter and the HRTF; no distance
  falloff.

## Names in the world hubs (added after the first implementation)

- The boss door is named with the first line of game text 44 (both boss
  doors in each world use `set_current_text(door->textID)` with 44): "BOSS
  LEVEL", "BOSS TUER", "PORTE DU BOSS". Game texts are read from the ROM
  (asset sections 6 and 7; German, French and Japanese at +85, +170, +255 in
  the text table; `game_text.c`), since the game loads them only when shown.
  Japanese uses two-byte font codes, so the boss door is left out there.
- Track doors use `level_name`; the trophy race uses menu text 70; T.T. and the
  way back to the island ("TIMBER'S ISLAND") are mod wording.
