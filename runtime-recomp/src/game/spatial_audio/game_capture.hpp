#pragma once

#include "recomp.h"

#include <cstdint>

namespace dkr::runtime::spatial_audio {

// Game thread, once per authored frame (main_game_loop's frame-begin hook,
// so after the previous tick's audspat_update_all): reads the listener and
// the positioned sounds into a snapshot for the audio thread. Skipped while
// rollback replays frames.
void capture(std::uint8_t* rdram, recomp_context* context);

} // namespace dkr::runtime::spatial_audio
