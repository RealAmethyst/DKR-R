#pragma once

#include <cstddef>
#include <cstdint>

// DKR-R's binaural (HRTF) rendering of positioned sounds, driven by the host
// audio output (runtime_platform.cpp). The output calls come from the thread
// that holds the platform mutex. Builds without Steam Audio make these no-ops.
namespace dkr::runtime::spatial_audio {

struct Snapshot;
struct CapturePlan;
struct FrameCapture;

// The game's output device opened (or reopened) at this rate.
void start(std::uint32_t output_rate);
void stop();
// True while anything is sounding or still draining.
bool active();
// True while the binaural renderer runs, so it can take the race cues.
bool binaural_available();
// Before mix_into for the game's output buffer at `samples` (inside RDRAM):
// hands that frame's captured voices to the renderer.
void begin_output_block(const void* samples);
// Adds the rendered sound to interleaved L,R frames at the output rate, in
// the game mix's sample scale.
void mix_into(float* stereo, std::size_t frames);

// SP task thread, before each audio task: the positioned voices to capture,
// or false to run the game's list unchanged (not HEADPHONES with one camera,
// online, renderer not running, or captures stopped being collected).
bool capture_plan(CapturePlan& plan);
// SP task thread, after the task: the frame's captured voices.
void submit_capture(const FrameCapture& capture, const std::uint8_t* rdram);

// Game thread (game_capture.cpp): the newest listener and positioned sounds.
void publish_snapshot(const Snapshot& snapshot);

} // namespace dkr::runtime::spatial_audio
