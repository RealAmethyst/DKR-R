#include "spatial_audio/audio_task.hpp"

// Builds without Steam Audio (DKR_STEAM_AUDIO=OFF) run every task as is.
namespace dkr::runtime::spatial_audio {

RspExitReason run_audio_task(std::uint8_t* rdram, std::uint32_t ucode_address,
                             RspUcodeFunc* microcode) {
    return microcode(rdram, ucode_address);
}

} // namespace dkr::runtime::spatial_audio
