# Menus between the title screen and a race: speech research

Research for screen-reader speech on the stock screens a player passes through
after START on the title screen. Sources: the matching decomp (`src/menu.c`,
identical for US 1.0 "v77" and US Rev A "v80"; differences are `#if VERSION`
blocks) and the disassembly of both decomp ELFs. Hook sites were read from both
ELFs; `scripts/accessibility_menu_policy.py` re-verifies every word, function
bound and delay slot at compose time, so a site listed here that is wrong fails
the build rather than speaking.

Line numbers refer to `src/menu.c`. Addresses are given as v77 / v80. Unless
stated, a site is a `jal sound_play` (word `0x0C000741`) whose previous
instruction is not a branch or jump. Symbols marked "table" are in both
`modding/reference-us-v*/game_symbols.inc`.

## General facts

- Every screen below opens through `menu_init`, so the existing ScreenOpened
  join (`0x80081590` / `0x800819E0`) fires after the screen's own init, with
  `gCurrentMenuId` set.
- N64Recomp emits a branch target's label before the hook text, so a hook on a
  branch target runs on every path into it. A hook before a `jal` runs before
  that `jal`'s delay-slot instruction: state written in the delay slot is not
  visible yet.
- Several sites need the player index or a new value from a register
  (`ctx->rN`); the handler currently reads no registers.
- The "OK?" prompt on character select, track select and the adventure race
  screen is a C string literal in rodata, not `gMenuText`, and has no symbol
  (character select draws it from `0x800E8230` / `0x800E87C0`).
- `gFileNameValidChars` ("ABCDEFGHIJKLMNOPQRSTUVWXYZ.?" plus spaces) has ELF
  size 0 and is absent from the symbol table (`0x800E0F6C` / `0x800E14EC`).
- All retail input on these screens reads the analog stick.

## Flow

- Title START → Character Select (3).
- Character Select → Caution (28, single player's first visit per power-on)
  → Game Select (19); multiplayer or tracks mode → Track Select (15).
- Game Select: ADVENTURE / ADVENTURE TWO → File Select (6); TRACKS → Track
  Select (15). B → Character Select.
- File Select: existing save → hub level directly (no `menu_init`); new save
  → name entry → new-game cinematic (23) → hub. B → Game Select.
- Track Select: race loads directly; Trophy column → Trophy Race Round (20).
- Adventure hub race door → Adventure track screen (5), shown only for a race
  already won or a challenge track; otherwise it loads the level on its first
  frame with nothing drawn.

## Character Select (menu id 3)

Displayed (`charselect_render_text`, 6836-6866, while `gMenuDelay` is between
-23 and 23):

- Heading `gMenuText[ASSET_MENU_TEXT_PLAYERSELECT]` (135) "PLAYER SELECT".
- "OK?" literal when `gNumberOfReadyPlayers == gNumberOfActivePlayers > 0`.
- Characters are 3D models holding signs (texture chosen by controller
  index); no names are drawn. `gTitleCinematicText` names belong to the
  attract intro and have no T.T. entry.

State (table): `gPlayersCharacterArray` s8[8] (per-controller cursor, a row of
the current select table, -1 inactive), `gActivePlayersArray` s8[4],
`gCharselectStatus` s8[4] (0 unconfirmed, 1 just confirmed, 2 ready),
`gNumberOfActivePlayers`, `gNumberOfReadyPlayers`, `gCurrCharacterSelectData`
(points at `gCharacterSelectBytesDefault` 112 / `...DrumStick` 126 /
`...TT` 126 / `...Complete` 140), `gMenuDelay`, `gEnteredCharSelectFrom`.

Select table rows are 14 bytes: up s8[2] +0, down s8[2] +2, leftward
neighbours s8[4] +4 (decomp name `rightInput`), rightward neighbours s8[4] +8
(decomp name `leftInput`), `voiceID` s16 +0xC (the `Character` enum).
Table order: 0 Krunch, 1 Diddy, 2 Bumper, 3 Banjo, 4 Conker, 5 Tiptup,
6 Pipsy, 7 Timber, 8 Drumstick or T.T., 9 T.T. (complete table).

Grids (locked characters are absent):

- Default: top Krunch, Diddy, Bumper, Banjo; bottom Conker, Tiptup, Pipsy,
  Timber.
- Drumstick: top adds Drumstick after Diddy.
- T.T. only: bottom adds T.T. after Tiptup.
- Complete: both. Retail bug: down on T.T. moves to Tiptup.

Movement (`charselect_move`, 7179): no wrap; vertical checked first; a
candidate held by another active player is skipped unless cheat bit 22; a
failed move plays the horn.

Sites (player index register in brackets):

- Join, `charselect_new_player`: `0x8008B480` / `0x8008B938` [r17].
- Move succeeded, `charselect_move`: `0x8008C0E0` / `0x8008C598` [r20];
  cursor already written.
- Move failed: `0x8008C0F4` / `0x8008C5AC` [r20].
- Confirm, `charselect_input`: `0x8008B9B4` / `0x8008BE6C` [r20]; status and
  ready count updated.
- Un-confirm, `charselect_input`: `0x8008B848` / `0x8008BD00` [r20].
- Un-confirm at "OK?", `charselect_pick`: `0x8008B714` / `0x8008BBCC` [r16].
- Final confirm, `charselect_pick`: `0x8008B600` / `0x8008BAB8` [r17].
- Leave (no sound): before `lui a2,0x800E` (`0x3C06800E`), `0x8008B880` /
  `0x8008BD38` [r20]; active count already decremented, cursor and active
  flag not yet cleared.
- Back to title: `jal transition_begin`, `0x8008B948` (`0x0C030076`) /
  `0x8008BE00` (`0x0C0301BE`).

## Caution (menu id 28)

- Heading `gMenuText[152]` "CAUTION"; body lines `gMenuText[166..176]`; no
  options, no hints. Input ignored for 60 frames; then A, B or START
  dismisses (`0x8008C44C` / `0x8008C904`) and Game Select opens.

## Game Select (menu id 19)

- Text through `gGameSelectElements` (`MenuElement` 0x20 bytes, type +0x13,
  `asciiText` +0x14): elements 0-1 title `gMenuText[76]` "GAME SELECT"; item i
  is element `i*2+3`: ADVENTURE (142), ADVENTURE TWO (143, only when
  unlocked), TRACKS (144). Read through the pointer: the two arrays' symbol
  sizes are understated.
- Cursor `gMenuCurIndex` (0 on every open), last index `gMenuStage`
  (1 or 2). No wrap. Two players' inputs are combined.
- Sites: down `0x8008CA78` / `0x8008CF30`; up `0x8008CAA8` / `0x8008CF60`
  (index already stored); confirm `0x8008CA18` / `0x8008CED0`; back
  `jal transition_begin` `0x8008CA30` (`0x0C030076`) / `0x8008CEE8`
  (`0x0C0301BE`).

## File Select (menu id 6)

Displayed (`fileselect_render`, 7590-7746):

- Title "GAME SELECT": `gMenuText[76]` in v77, `gMenuText[188]` in v80.
- Three slots: name (`gSavefileInfo[i].name` trimmed, or `gFilenames[i]`
  "GAME A/B/C" when unstarted) and "NEW" (75) when unstarted. Started slots
  draw a balloon icon (gold, or diamond for Adventure Two), a cross and a
  two-digit balloon count, all artwork. A slot of the other adventure is
  drawn darker and refuses selection with the horn.
- COPY (80) and ERASE (81) below the slots.
- `SavefileInfo` (12 bytes): `isAdventure2` u8 +0, `isStarted` u8 +1,
  `balloonCount` u16 +2, `name[4]` +4. `gSavefileInfo` is filled two frames
  after opening; read it at the "slots ready" site.

State (table): `gMenuStage` (0 slots, 1 COPY, 2 ERASE), `gSaveFileIndex`,
`gSaveFileIndex2`, `gSaveFileIndex3`, `gFileCopy`, `gFileErase`, `gFileNew`,
`gFileConfirm`, `gIsInAdventureTwo`.

Root sites: slots ready, `sw t0,0x44(sp)` (`0xAFA80044`) `0x8008DF74` /
`0x8008E42C`; cursor `0x8008D8A0` / `0x8008DD58`; wrong-adventure horn
`0x8008D6F0` / `0x8008DBA8`; slot confirm `0x8008D70C` / `0x8008DBC4`; enter
copy `b` (`0x10000057`) `0x8008D74C` / `0x8008DC04`; enter erase `b`
(`0x10000048`) `0x8008D788` / `0x8008DC40`; back `b` (`0x10000043`)
`0x8008D79C` / `0x8008DC54`.

The post-input joins below run every frame in their mode; the speech hooks
use the per-outcome sites in `fileselect_input_root`, `_copy` and `_erase`
instead (listed in `scripts/accessibility_menu_policy.py`).

Copy prompts: "SELECT GAME TO COPY" (77), "CHOOSE GAME TO COPY TO" (78), then
"OK?". Erase prompts: "SELECT GAME TO ERASE" (79), then "OK?". Post-input
joins where the frame's state is final: after copy `b` (`0x10000098`)
`0x8008E028` / `0x8008E4E0`; after erase `b` (`0x10000090`) `0x8008E048` /
`0x8008E500`. Individual copy and erase sound sites (cursor, cancel, refuse,
confirm) are listed in the research reports and follow the same pattern.

Name entry (`filename_init`, `filename_render`, `filename_enter`):

- Prompt "ENTER YOUR INITIALS" (8); a 31-position carousel: A-Z, ".", "?"
  from `gFileNameValidChars`, then "SP", "DEL", "OK" rodata literals; the
  name so far. Three letters; the cursor jumps to OK when full.
- `filename_enter` is shared with the post-race time-trial initials:
  distinguish by `gNameEntryTargetX == &gIndexOfCurInputCharacter` (post-race
  uses `D_800E0FA4`).
- Sites: opened `sw zero,0x40(sp)` (`0xAFA00040`) `0x8008E264` /
  `0x8008E71C`; character typed `0x80097FD4` / `0x80098510`; carousel move
  `0x80098154` / `0x80098690` (new index in r2, stored after the call); DEL
  `0x80098040` / `0x8009857C`; B delete `0x80098104` / `0x80098640`; name
  finished `0x8008E16C` / `0x8008E624` (`0x240D0001`); name cancelled
  `0x8008E0AC` / `0x8008E564` (`0x240F0044`).

## Track Select, tracks mode (menu id 15)

`gTrackmenuType`: 0 grid, 1 setup, 2 load, -1 leave.

Grid: rows are worlds (Y 0-4), columns X 0-5. The world name is drawn above
the frame (`level_name(level_world_id(Y+1))`); the cell label is
`level_name(trackMenuIds[Y*6+X])`, or `gDefaultTrackName` "?" when locked.
`level_name` reads `gLevelNames[id]` and skips 1, 2 or 3 NUL-separated
entries for German, French or Japanese. Columns 0-3 races, 4 Trophy Race, 5
battle arena (none in Future Fun Land). Locked cells are reachable; cleared
and trophy markers are artwork.

Cursor (table): `gTrackSelectCursorX`, `gTrackSelectCursorY`,
`gTrackIdForPreview` (-1 locked), `gTrackSelectIDs` s16 (symbol size 48 for
`[4][6]`, but row 4 is read past the end, overlapping `gFFLUnlocked`),
`gFFLUnlocked`. With the custom-track catalogue active,
`gTrackSelectRenderDetails` is moved and `level_name` may be overridden.

Setup stages (`gMenuStage`):

- -1: ADVENTURE (146) / ADVENTURE TWO (147), only when Adventure Two is
  unlocked and X≠5; `gTracksMenuAdventureHighlightIndex`.
- 0: vehicle per player (`gPlayerSelectVehicle` s8[4], 0 car, 1 hovercraft,
  2 plane; `gPlayerSelectConfirm` s8[4]); vehicle choice only on a cleared
  course. Vehicles and the "VEHICLE" title are artwork.
- 1: time trial, one player (`gTracksMenuTimeTrialHighlightIndex`, 0 off,
  1 on); artwork only.
- 2: "OK?"; or two players: "NUMBER OF RACERS" (136) with 2, 4 or 6
  (`gMultiplayerSelectedNumberOfRacers` 0-2, digits are sprites); Trophy
  column "OK?"; battle arena shows the rules text (game text 60+Y) instead.
- 3: two-player "OK?".
- Setup draws the track name, and for X<4 "BEST TIME" (9) and "BEST LAP"
  (10) with initials and times for player 1's vehicle
  (`courseTimesPtr`/`flapTimesPtr` u16 frames, `courseInitialsPtr`/
  `flapInitialsPtr` 5-bit packed).
- v80 removes hovercraft on Spaceport Alpha and plane on Frosty Village with
  two or more players.
- T.T. speaks the track name (`gTTVoiceLines`) shortly after setup opens.

Sites:

- `trackmenu_input` (v77 = v80 - 0x4B8): grid cursor `0x80090E3C` /
  `0x800912F4` (X and Y written; `gTrackIdForPreview` not yet); locked cell
  `0x80090D28` / `0x800911E0`; setup opened `b` (`0x1000000C`) `0x80090C78` /
  `0x80091130`; back `jal transition_begin` `0x80090D4C` / `0x80091204`.
- `func_80092188` (different scheduling per revision): battle text loaded
  (`0x3C028012`) `0x8009220C` / `0x80092728`; returned to grid
  (`0x3C028012`) `0x800923D4` / `0x800928F0`; all vehicles confirmed
  `0x80092730` / `0x80092C80`; end-of-switch back `0x80092B28` /
  `0x80093070`, select `0x80092B40` / `0x80093088`, change `0x80092B60` /
  `0x800930A8` (only one fires per frame; diff cached state).
- The custom-vehicles patch can return early from `func_80092188`, skipping
  these sites while its own picker is active.

## Adventure track screen (menu id 5)

- Init stage `gMenuStage`: -1 means nothing is shown (first visit to a race,
  hub warp, boss); stay silent.
- Shown: level name (`level_name(gLevelSettings[2])`). Challenge tracks add
  the rules text box (game text `59 + world`). A won race whose world's boss
  is beaten (or Future Fun Land), with time trial off, offers "SILVER COIN"
  (11) "CHALLENGE" (13 in v77, 191 in v80). Otherwise the vehicle stage
  (vehicles artwork, choice only once silver coins are done), then "OK?".
  With time trial on, BEST TIME / BEST LAP with initials.
- Sites (`menu_adventure_track_loop`, v77 = v80 - 0x504): start
  `0x800937F8` / `0x80093CFC`; B at confirm, offer or challenge
  `0x80093810` / `0x80093D14`; B leaving from vehicle stage `0x80093888` /
  `0x80093D8C`; A to "OK?" `0x800938BC` / `0x80093DC0`; vehicle changed
  `0x80093958` / `0x80093E5C` (new vehicle in r3, stored later).

## Text boxes from `game_text.c`

Battle rules, challenge rules, locked doors, and the hub's T.T. and Taj
menus use `set_current_text` and the buffer at `D_8012A7A0` (table), with
byte codes 0 line break, 1 page break, 2 end, 3-12 commands. A general text
box reader would cover all of them; it is outside the menu hooks above.
