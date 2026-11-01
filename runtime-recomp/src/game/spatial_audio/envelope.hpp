#pragma once

#include "spatial_audio/alist_rewrite.hpp"

#include <array>
#include <cstdint>

namespace dkr::runtime::spatial_audio {

// The envmixer's linear volume ramp (env.c _getRate; the microcode adds the
// rate per 8-sample block and spreads it over the block's samples, stopping
// at the target), followed host-side to give a captured voice the game's
// volume.
struct EnvelopeRamp {
    double left = 0.0;
    double right = 0.0;
    double target_left = 0.0;
    double target_right = 0.0;
    // Per 8 samples.
    double rate_left = 0.0;
    double rate_right = 0.0;

    static EnvelopeRamp from_init(const alist::InitGains& gains);
    // ENVMIX_STATE as 40 halves: the last mixed block's volume L integer
    // lanes (0-7) and fractions (8-15), volume R (16-31), then L target, rate
    // (whole, fraction), R target, rate, dry, wet (32-39). Checked against
    // the microcode in voice_capture_tests.cpp.
    static EnvelopeRamp from_state(const std::array<std::int16_t, 40>& state);

    // Steps one sample and returns its left and right volumes (0..32767).
    void step(double& volume_left, double& volume_right);
};

} // namespace dkr::runtime::spatial_audio
