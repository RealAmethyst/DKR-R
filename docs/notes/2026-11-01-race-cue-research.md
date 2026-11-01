# Race cue research: track edges and turns ahead

Research for the race sound cues (#35): the track edge sound (#42) and the turn
signal (#43). Checked against the matching decomp (`extern/dkr-decomp`) and,
where stated, measured offline on the US 1.0 (v77) level data; the turn data was
also decoded for Rev A (v80), with identical results. Extracted level data was
kept out of the repository.

## Which surfaces slow a kart

- Surface types are per texture: `TextureInfo.surfaceType` (+0x07) of the
  batch's texture (`structs.h`).
- Cars: `gSurfaceTractionTable` (`racer.c`) drags `velocity -= v^2 * traction`
  per wheel average. 0.004 for DEFAULT, ZIP_PAD, FROZEN_WATER, TAJ_PAD, SNOW and
  the remaining types; 0.007 GRASS; 0.01 SAND, STONE and the egg surfaces.
  STONE also caps top speed (`ASSET_MISC_32[wheelsOnStone]`) and zeroes lateral
  velocity. Floating (`buoyancy != 0`) forces traction 0.02.
- Hovercraft (`func_80046524`) never read the traction table: only STONE slows
  them; grass, sand, snow and water are full speed.
- The tables are global `.data` (the only writer is the anti-tamper
  `drm_vehicle_traction`): there are no per-level or per-weather overrides. One
  rule therefore defines "full speed" on every track, and snow is road on the
  snow tracks.
- Measured main-road surfaces: DEFAULT on most tracks; SNOW on Snowball Valley
  and Walrus Cove; Crescent Island has no SAND texture at all (its beaches are
  full speed). Verges are mostly GRASS (Ancient Lake, Wizpig 1), SAND (Fossil
  Canyon) or STONE (Haunted Woods, Spacedust Alley, Trickytops); several tracks
  have only walls, drops and water.

## Checkpoint gates do not give the road edge

- `checkpoint_update_all` (`objects.c`) stores `scale = level entry / 32`; the
  lateral offset `Object_Racer.0x1BA` is in those units, positive to the right
  of the gate line, rate-limited to 5 per tick and not flipped by Mirrored
  Tracks. The ±64 clamp is AI-only.
- Measured full-speed half-width over the gate half-width (±64): 1.06 to 2.85
  on Ancient Lake, median 1.98 on Fossil Canyon, 1.71 on Darkmoon Caverns,
  1.83 on Spaceport Alpha, and different on each side. The gates are not used.

## Level geometry at run time

- `generate_track` (`tracks.c`) inflates the model into `gCurrentLevelModel`
  and turns its offsets into addresses: `LevelModel` textures +0x00, segments
  +0x04 (0x44 bytes each), bounding boxes +0x08 (6 x s16), counts +0x18/+0x1A.
  Segment: vertices +0x00 (10 bytes, s16 x y z), triangles +0x04 (16 bytes:
  flags, then vertex indices at +1..+3), batches +0x0C (12 bytes: texture +0,
  vertex offset +2, first triangle +4, flags +8; one extra batch closes the
  list), counts +0x1C/+0x1E/+0x20, water height `unk38` +0x38.
- Collision (`generate_collision_candidates`): batches with
  `RENDER_NO_COLLISION` (bit 9) or no texture are skipped; WATER_CALM is never
  solid; INVIS_WALL is solid except for planes; UNK12 only for cars. Triangles
  with flag 0x80 get no collision plane (`track_init_collision`).
  `resolve_collisions`: normal Y 0.707 and up is floor (unless STONE), below
  0.45 is a wall.
- Water: `get_level_segment_waves` collects every triangle under the point from
  the segments around it (water batches and visible solid batches) plus the
  racer's own segment's water level, sorted by height;
  `get_wave_properties` picks the water a racer at that height floats in. A car
  floats once its height less 5 is below that water.
- Cost measured offline: a ±400-unit sideways search touches 1 to 4 segments
  and a median of 27 to 45 triangles; whole levels hold 1,100 to 3,400
  collision triangles.

## Course arrows and turns

- `checkpoint_is_passed` (`objects.c`) copies the gate's `unk3B` into
  `Object_Racer.indicator_type` (0x1F8) with a 120-frame timer when a racer
  crosses that gate: the HUD arrow appears at the gate, giving 0 to 1.5 s of
  warning. C-down toggles the HUD arrows in single player; the timer only runs
  while they are shown, so a cue must not depend on it.
- Values (`CourseIndicatorArrows`, `game_ui.h`): 1-3 left, sharp left, left
  U-turn; 4-6 the same to the right; 7 up; 8 down; 9 "!". Mirrored Tracks flips
  them on screen.
- Coverage, decoded offline: every default-vehicle gate set has arrows; they
  mark 246 of 298 heading changes of 30 degrees or more, and 285 of 298 agree
  with the gate geometry (the rest are S-bends). No arrows at all: Fossil Canyon
  and Ancient Lake planes, Everfrost Peak and Windmill Plains car and
  hovercraft, boss races. Hubs have Taj's gates; battle arenas have none.
  Alternate-route gates never carry arrows.
- An arrow sits on the gate where the bend starts or 1 to 2 gates before.
  Gates are 225 to 3,147 units apart (median 858); staff ghosts drive 700 to
  1,030 units per second.
- `Object_Racer.checkpoint_distance` (0x0A8) is a fraction: 1 just past the
  previous gate, 0 at the next. `nextCheckpoint` is 0x192.
- Gate normals `(rotationXFrac, rotationZFrac)` point against travel.
  `wrongWayCounter` (0x1FC) counts while the racer faces away from the course.

## Axes

- Facing +Z, +X is on the left (262 of 270 authored arrows agree with that
  reading of the gate geometry). The right of a heading (fx, fz) is (-fz, fx).
- A racer's `ox1, oz1` (Object_Racer +0x38/+0x40) is its +Z axis from
  `mtxf_from_transform`; `racer->velocity` is negative going forwards
  (`velocity -= traction * throttle`), so the kart drives along -(ox1, oz1).
- Object velocity (+0x1C) is per 60 Hz tick (moves scale it by the update
  rate).

## Open, to settle in game

- The ride height, step and wall-probe heights of the edge search are
  approximations, not game values; listening tests decide the range.
- Planes: no edge cue yet (height cue is a later design).
