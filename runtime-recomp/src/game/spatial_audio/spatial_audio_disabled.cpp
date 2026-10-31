#include "spatial_audio/spatial_audio.hpp"

// Builds without Steam Audio (DKR_STEAM_AUDIO=OFF) keep today's stereo mix.
namespace dkr::runtime::spatial_audio {

void start(std::uint32_t) {}
void stop() {}
bool active() { return false; }
void mix_into(float*, std::size_t) {}

} // namespace dkr::runtime::spatial_audio
