#pragma once

namespace dkr::runtime::spatial_audio {

// A direction in the listener's frame: +x right, +y up, -z ahead.
struct Direction {
    float x = 0.0F;
    float y = 0.0F;
    float z = -1.0F;
};

} // namespace dkr::runtime::spatial_audio
