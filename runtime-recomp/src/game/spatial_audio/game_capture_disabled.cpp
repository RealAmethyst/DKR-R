#include "spatial_audio/game_capture.hpp"

// Builds without Steam Audio (DKR_STEAM_AUDIO=OFF) read nothing.
namespace dkr::runtime::spatial_audio {

void capture(std::uint8_t*, recomp_context*) {}

} // namespace dkr::runtime::spatial_audio
