# Spatial audio research: HRTF through Steam Audio, and race cue data

Research for two planned features: binaural (HRTF) rendering of the game's
positioned sounds when the game's sound mode is HEADPHONES, using Valve's
Steam Audio; and later, new spatialised accessibility cues during races
(track edges, turns ahead and similar). Sources: the matching decomp
(`src/`, US 1.0 "v77" and US Rev A "v80"), both decomp ELFs, this repository,
and Steam Audio's official repository and documentation. Addresses are
v77 / v80. Claims marked "inferred" were not verified in code.

## How DKR-R plays audio today

- The game's own synthesiser code (libultra audio, `alAudioFrame`) runs
  recompiled on the guest audio-manager thread (`__amMain`, `audiomgr.c`).
  It builds an audio command list (alist) each retrace.
- The list runs on a static recompilation of the original audio microcode
  (`dkrAspMain`, `runtime-recomp/RecompiledRSP/aspMain.cpp`, generated; never
  edit). It is not a C++ HLE decoder. The microcode is identical in both ROMs.
- Output is a 22,050 Hz stereo mix (`OUTPUT_RATE`, `audiomgr.h`), about 736
  frames per 30 Hz frame, handed over one frame late (`osAiSetNextBuffer`).
- DKR-R receives the finished mix in `dkr::runtime::platform::queue_audio`
  (`runtime_platform.cpp`): R/L word swap, custom music, equaliser (Modern
  presentation), master volume, the UI tone, then `SDL_QueueAudio`. SDL2 is
  opened at the game's rate (`ReopenGameAudioLocked`), 16-bit stereo, 512
  frame callback; SDL resamples to the hardware. It is skipped when
  `netplay::external_side_effects_allowed()` is false.
- Voices are separate as audio only inside the microcode's DMEM, between
  A_RESAMPLE and A_ENVMIXER (`AL_RESAMPLER_OUT`); the envmixer applies volume
  ramps and an equal-power pan and sends dry to main L/R and wet to the aux
  buses (sound effects to BIGROOM echo, music to the custom bus). All N64
  voices are mono. On the CPU side voices exist only as parameters
  (`ALSoundState` → `ALVoice` → `PVoice` → `ALEnvMixer`).
- DKR-R's Vehicles / Nature / Effects volumes scale `AL_SNDP_VOL_EVT` at
  `sndp_set_param`, identified by call scope (hooks around
  `racer_sound_update(_all)` and `audspat_update_all`); Modern presentation
  only, off in the owned rollback runtime. Nothing intercepts voice audio.

## The game's positioned sounds

- Listener: `cam_get_cameras()` (`gCameras[0..3]`, `gCameras[4]` in
  cutscenes; `Camera` 0x44 bytes, rotation y/x/z s16 at 0, position at 0x0C).
  The game pans by camera yaw only (`audspat_calculate_spatial_pan`), ignores
  height, mirrors on Mirrored Tracks, and forces centre pan with more than one
  camera.
- Audio points (`gAudioPoints`, at most 40): position, range, volume, falloff,
  `soundHandle` at 0x18. Created by `audspat_play_sound_at_position` (doors,
  objects, weather, bosses) and level Audio objects; moving objects update
  them. Audio lines (`gAudioLines`, 7 polylines): pan blended per segment;
  jingle lines drive the jingle player.
- Vehicles: the player's own engine is centred during a race (positioned only
  after the finish or in cutscenes). At most two AI engines play
  (`gBackgroundRacerSounds`, one or two players), positioned relative to the
  player's kart, panned by camera 0's yaw, with doppler.
- Most `sound_play` calls (UI, voices, items) are not positioned;
  `sound_play_spatial` only sets echo.
- Voice back to owner: owner slot → `ALSoundState*`; its `ALVoice` at +0x0C,
  `pvoice` at +0x08; the `PVoice` envmixer `state` address appears in that
  voice's A_ENVMIXER command, a per-voice key visible in both guest structures
  and the alist. `ALSoundState.userHandle` (+0x30) points back to the owner
  slot (only on the last state of a composite sound).
- At most 10 sound effects play at once (`sndp_set_active_sound_limit`), plus
  two AI engines.

## Headphones mode

- `gAudioOutputType` (menu) and `gStereoPanMode` (synth, written by
  `set_stereo_pan_mode`): STEREO 0, MONO 1, HEADPHONES 2. In HEADPHONES mode
  `modify_panning` halves the stereo width for effects and music.
- Retail does not save it; DKR-R's redesigned Options menu does
  (`game_sound_mode` in `options_menu/game_audio_memory.cpp`, restored at
  boot by `options_screen.cpp`).

## Ways to get positioned voices into an HRTF

1. Capture each positioned voice from the alist: after its A_RESAMPLE, save
   the mono buffer to RAM; remove its dry send from the game mix while keeping
   the wet echo send; apply the game's volume on the host and replace the pan
   with HRTF. Needs: the microcode's command and ENVMIX state format (where
   dry and wet live), free DMEM and RAM for capture buffers, guest hooks in
   `alEnvmixerPull` / `alAuxBusPull` (static `_pullSubFrame` appears inlined;
   inferred) for both revisions, and gating like the category mix. A rewrite
   of the list before it runs would need a patch to n64-modern-runtime.
   Medium-high risk; needs a spike.
2. Re-synthesise positioned sounds on the host from the CPU-side parameters
   (VADPCM decode, pitch, envelopes, loops, retriggers, voice stealing) and
   mute the originals. Largest effort; fallback only.

Read-only work (a snapshot of listener and emitters, host rendering in
`queue_audio`) is outside the simulation. Guest writes (muting, list edits)
must follow the category mix's gating: Modern presentation, not in the owned
rollback runtime.

## Steam Audio (official repository, v4.8.1, February 2026)

- Licence Apache-2.0; ship `LICENSE.md` and `THIRDPARTY.md`. Official
  binaries include Intel IPP (Intel Simplified Software License: binary
  redistribution with notice). Third-party components: PFFFT, libmysofa,
  zlib, FlatBuffers, Embree, Radeon Rays / TrueAudio Next (Windows x64 only),
  Google spherical harmonics, the CIPIC HRTF (subject 124).
- Prebuilt shared libraries for Windows x64/x86, Linux x64/x86 and macOS
  universal (25-53 MB each); none for Linux or Windows arm64 (build from
  source there). As a CMake subproject it leaks `BUILD_SHARED_LIBS`, defaults
  tests and samples on, and searches dependencies under the parent's root; a
  pinned prebuilt shared library is the simpler integration.
- The built-in HRTF works only at 24,000, 44,100 or 48,000 Hz. DKR's 22,050 Hz
  mix must be resampled first.
- API: `iplContextCreate`; `iplHRTFCreate` (expensive, not on the audio
  thread; `IPLHRTFSettings.volume` must be 1.0, a zeroed struct is silent;
  optional SOFA file; `normType` RMS evens loudness across directions); per
  voice `iplDirectEffect` (distance attenuation, air absorption, directivity,
  occlusion values) and `iplBinauralEffect` (mono or stereo in, stereo out,
  direction unit vector in listener space, nearest or bilinear
  interpolation, `spatialBlend`). Deinterleaved float buffers; every apply
  call must process exactly the creation `frameSize`; the binaural effect
  delays by `frameSize/4`. Drain tails and reset effects between voices.
- A zero direction divides by zero in the binaural effect;
  `iplCalculateRelativeDirection` returns straight up instead. Its frame is
  right-handed (+x right, +y up, -z ahead); the handedness of DKR's world must
  be verified before use.
- Valve's own integrations render the direct path per source; ambisonics is
  their CPU-saving fallback (order 3 with the built-in HRTF, less precise).
- Effects sharing one HRTF must run on one thread (shared scratch buffers;
  inferred from the implementation).

## Race data for cues (read-only)

- Checkpoint gates: `gTrackCheckpoints` (pointer to up to 60
  `CheckpointNode`, 0x3C bytes), `gNumberOfMainCheckpoints`,
  `gNumberOfTotalCheckpoints`. Node: forward normal (0x00-0x08), plane D
  0x0C, position 0x10-0x18, scale 0x1C, distance to next 0x20, lane offsets
  0x2E, route flags 0x36, alternate route 0x3A, HUD course arrow 0x3B. The
  lateral axis is `(rotationZFrac, 0, -rotationXFrac)`. Gate sets differ per
  vehicle (`gTajChallengeType`).
- Course arrows (0x3B): 1-3 left 45/90/U-turn, 4-6 right, 7 up, 8 down,
  9 "!"; mirrored on Mirrored Tracks. These are authored turn warnings.
- Player 1: `gRacersByPort[0]` → Object (+0x64 is `Object_Racer*`); check
  `gNumRacers`, behaviour `BHV_RACER`, `playerIndex` 0. Object position at
  0x0C-0x14, velocity 0x1C-0x24. `Object_Racer`: next gate 0x192, lap 0x193,
  progress to the next gate 0x0A8, alternate route 0x1C8, vehicle 0x1D6,
  lateral offset from the spline 0x1BA (s16, gate-scale units; about ±64 is
  the gate half-width, inferred; sign not yet confirmed), vertical offset
  0x1BC, wheel surfaces 0x1DC[4] (0xFF no contact), wrong-way counter 0x1FC,
  course arrow shown 0x1F8/0x1F9.
- Walls exist only as collision triangles (normal Y below 0.45) and
  `SURFACE_INVIS_WALL` (17); `camera_obstruction_guest.h` already holds a
  read-only triangle query pattern. Never call the game's collision functions
  from a cue.
- Sampling point: the existing post-mode `main_game_loop` hook (once per
  logic tick, after racers and cameras); skip rollback replays. Absent data
  (hubs, battle arenas, custom tracks) must fail closed.
