# Capturing positioned game voices for HRTF (spike #33)

Findings for rendering the game's positioned sounds binaurally (epic #29):
capture each positioned voice as mono audio and remove its dry signal from
the game's own mix, keeping its echo. Sources: the recompiled audio
microcode (`runtime-recomp/RecompiledRSP/aspMain.cpp`, identical text and
data in both revisions), the matching decomp's synth code
(`src/libultra/src/audio/`: `env.c`, `resample.c`, `load.c`,
`synthesizer.c`, `auxbus.c`, `mainbus.c`, `reverb.c`, `save.c`,
`audiomgr.c`), and n64-modern-runtime. "Inferred" marks claims not checked
in code.

## Verdict: go

Rewrite a copy of each audio frame's command list (alist) on the host,
right before the microcode runs, so that the microcode itself saves each
positioned voice's mono signal, and zero that voice's dry gain. The capture
is bit-exact by construction and nothing of the microcode is reimplemented.

## Where the host gets in

- DKR-R chooses the microcode per RSP task (`GetRspMicrocode`,
  `runtime-recomp/src/game/game_main.cpp`) and returns a function that calls
  `dkrAspMain`. `recomp::rsp::run_task` (`librecomp/src/rsp.cpp`) copies the
  `OSTask` to DMEM 0xFC0 first, then calls it synchronously.
- The microcode reads the list address and size from that DMEM copy
  (`aspMain.cpp`, start). Pointing DMEM 0xFC0+0x30 / +0x34 at a rewritten
  list in RDRAM makes it run that list instead. ABI1 lists hold no internal
  pointers, so a copy runs the same.
- The microcode's DMA masks addresses with 0xFFFFF8 (`librecomp/rsp.hpp`):
  only the first 16 MB of RDRAM is reachable.

## Scratch RDRAM

- 0x80000000-0x807FFFFF is game RAM; DKR-R grows the game's main pool over
  0x80400000-0x807FFFFF for custom tracks (`custom_tracks_hooks.cpp`).
- n64-modern-runtime (`librecomp/include/librecomp/addresses.hpp`): PI
  handles at 0x80800000, patch data from 0x80801000 (DKR-R registers no
  patches, so none is written), mods and the recomp heap from 0x81000000
  (beyond the microcode's reach).
- So the capture scratch (rewritten list plus saved voice buffers) is
  reserved at 0x80F00000-0x80FFFFFF, a region nothing in the runtime or
  DKR-R uses (checked 2026-11-01). Correction to the research agent's
  suggestion of 0x80800000, which holds the PI handles.

## The microcode (ABI1 with a linear envmixer)

- The standard 16 ABI1 commands (dispatch table in the ucode data):
  0 SPNOOP, 1 ADPCM, 2 CLEARBUFF, 3 ENVMIXER, 4 LOADBUFF, 5 RESAMPLE,
  6 SAVEBUFF, 7 SEGMENT, 8 SETBUFF, 9 SETVOL, 10 DMEMMOVE, 11 LOADADPCM,
  12 MIXER, 13 INTERLEAVE, 14 POLEF, 15 SETLOOP. The envmixer ramps are
  linear (matching `env.c`'s `_getRate`).
- Parameter block at DMEM 0x360 (in, out, count, volumes, dry/wet,
  targets, rates); buffer offsets are relative to DMEM 0x5C0. DKR emits
  `aSegment(0, 0)` and physical addresses.
- SETBUFF: in = w0 & 0xFFFF, out = w1 >> 16, count = w1 & 0xFFFF (A_AUX
  form sets the aux buffers). SAVEBUFF saves `count` bytes from DMEM `out`
  to RDRAM w1.
- ENVMIXER (verified): on INIT the gains vector is loaded from DMEM 0x370
  (lane 6 dry, lane 7 wet, set by `SETVOL A_AUX`); on CONTINUE the 80-byte
  ENVMIX_STATE is loaded and the gains vector is state+0x40, so dry is
  state+0x4C and wet state+0x4E; the state is written back at the end. The
  CPU never reads the state.
- ENVMIX_STATE as 40 halves (checked against the microcode in
  `runtime-recomp/tests/voice_capture_tests.cpp`): the last mixed block's
  left volume integer lanes 0-7 and fractions 8-15, right volume 16-31, then
  left target, rate whole, rate fraction, right target, rate whole, rate
  fraction, dry, wet. Lane 7 is the volume of the block's last sample; the
  rate is per 8 samples (16.16, `_getRate`).

## A voice's commands

- One `_pullSubFrame` chain per voice per subframe (160 samples at most):
  decoder commands (LOADADPCM, SETBUFF, LOADBUFF, ADPCM, SETLOOP, DMEMMOVE,
  CLEARBUFF), then SETBUFF + RESAMPLE (or a DMEMMOVE at unity pitch), then
  `SETBUFF(A_MAIN, envIn, MAIN_L + n, count)`, `SETBUFF(A_AUX, ...)`, on
  INIT five SETVOLs, and `ENVMIXER(state)`.
- The ENVMIXER's w1 is `PVoice + 0xA0` (physical), the key the snapshot
  (#32) records for each positioned sound. Chains never interleave; a chain
  runs from after the previous ENVMIXER (or the aux bus's CLEARs) to its
  own ENVMIXER (inferred from the pull order; to be checked by tests).
- Frame order per subframe: SEGMENT, clear main L/R, aux bus 0 (music),
  mix to main, aux bus 1 (sound effects, BIGROOM reverb), mix, INTERLEAVE,
  SAVEBUFF of the output (`osVirtualToPhysical(info->data)` plus nOut * 4
  per subframe).

## Capture and dry removal

- Before each captured chain's `SETBUFF(A_MAIN)` insert
  `SETBUFF(in = 0, out = <envIn>, count = <count>)` and
  `SAVEBUFF(<scratch>)`: the envmixer's input, the voice after decoding and
  pitch, before volume and pan.
- Dry removal: zero the dry gain in the copied `SETVOL A_AUX` (INIT) and in
  ENVMIX_STATE+0x4C (CONTINUE) before the task; restore it afterwards. The
  ramps (volume lanes, rates, targets) do not depend on it.
- When the game's reverb is off (`alFXEnabled == 0`, set per music
  sequence) the wet send goes straight to main; then wet must be zeroed too
  and rendered as part of the HRTF signal.
- The host reapplies the voice's volume (the envmixer volume lanes and dry
  amount); pan is replaced by the HRTF direction.

## Pairing with the output

The finished mix reaches `queue_audio` one frame later through
`osAiSetNextBuffer`; its RDRAM address equals the list's first output
SAVEBUFF address, so a capture is matched to its output buffer by address
(three rotating buffers, in order).

## Evidence

`runtime-recomp/tests/voice_capture_tests.cpp` runs the recompiled
microcode (`dkrAspMain`) on synthetic lists built like `env.c` and
`save.c` (needs the ROM-derived `aspMain.databin.bin`, configured as
`DKR_ASPMAIN_DATA`; skipped without it): with a voice captured, the output
equals the list without that voice bit for bit over an INIT and two
CONTINUE frames, before or after another voice; without zeroing the state's
dry amount the CONTINUE frames differ (negative control); the followed
volume reproduces the envmixer's dry output within 1 LSB on a ramp. The
microcode text and data are byte-identical in both revisions.

## Risks

- Voice stealing: a PVoice is reused at once while the old sound fades out;
  keep it in the game mix until the new sound's first chain. A new sound is
  recognised by its decoder or resampler INIT (set by the filter reset
  before a start), not by an envmixer INIT: `env.c` forces an envmixer INIT
  for every volume, pan or effect-amount change, which positioned sounds get
  every tick (found 2026-11-01: treating those as restarts left part of
  every frame in the original mix, heard as stutter between the HRTF and
  the original).
- Composite sounds: one voice per layer; map each.
- The snapshot is taken on the game thread and the list is built on the
  audio thread: re-check each voice's sound inside the task.
- Zero-command frames produce no capture; online rollback replays queue
  audio differently (address pairing does not apply there).
- Mixing an HRTF signal on top of a saturating game mix can clip.
