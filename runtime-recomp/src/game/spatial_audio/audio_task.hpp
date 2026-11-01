#pragma once

#include "librecomp/rsp.hpp"

#include <cstdint>

namespace dkr::runtime::spatial_audio {

// SP task thread: runs one audio task through `microcode`. When positioned
// voices are to be rendered binaurally, the microcode runs a rewritten copy
// of the game's command list that saves those voices and leaves their dry
// signal out of the mix (alist_rewrite.hpp); otherwise the task runs as is.
RspExitReason run_audio_task(std::uint8_t* rdram, std::uint32_t ucode_address,
                             RspUcodeFunc* microcode);

} // namespace dkr::runtime::spatial_audio
