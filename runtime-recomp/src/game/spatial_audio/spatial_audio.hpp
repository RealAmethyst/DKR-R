#pragma once

#include <cstddef>
#include <cstdint>

// DKR-R's binaural (HRTF) rendering of positioned sounds, driven by the host
// audio output (runtime_platform.cpp). Every call comes from the thread that
// holds the platform mutex. Builds without Steam Audio make these no-ops.
namespace dkr::runtime::spatial_audio {

// The game's output device opened (or reopened) at this rate.
void start(std::uint32_t output_rate);
void stop();
// True while anything is sounding or still draining.
bool active();
// Adds the rendered sound to interleaved L,R frames at the output rate, in
// the game mix's sample scale.
void mix_into(float* stereo, std::size_t frames);

} // namespace dkr::runtime::spatial_audio
