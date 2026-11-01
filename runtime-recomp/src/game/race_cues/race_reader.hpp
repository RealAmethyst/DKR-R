#pragma once

#include "recomp.h"

#include <cstdint>

namespace dkr::race_cues {

// Game thread, once per authored frame (main_game_loop's frame-begin hook):
// reads player 1's kart, the level geometry and the checkpoint gates, and
// hands the track edge and turn signal to the audio thread. Read-only;
// skipped while rollback replays frames.
void capture(std::uint8_t* rdram, recomp_context* context);

} // namespace dkr::race_cues
