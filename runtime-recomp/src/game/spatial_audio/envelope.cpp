#include "spatial_audio/envelope.hpp"

#include <algorithm>

namespace dkr::runtime::spatial_audio {
namespace {

double Fixed(std::int16_t whole, std::int16_t fraction) {
    return whole + static_cast<std::uint16_t>(fraction) / 65536.0;
}

double Advance(double volume, double rate, double target) {
    const double next = volume + rate / 8.0;
    return rate >= 0.0 ? std::min(next, target) : std::max(next, target);
}

} // namespace

EnvelopeRamp EnvelopeRamp::from_init(const alist::InitGains& gains) {
    EnvelopeRamp ramp;
    ramp.left = gains.volume_left;
    ramp.right = gains.volume_right;
    ramp.target_left = gains.target_left;
    ramp.target_right = gains.target_right;
    ramp.rate_left = gains.rate_left / 65536.0;
    ramp.rate_right = gains.rate_right / 65536.0;
    return ramp;
}

EnvelopeRamp EnvelopeRamp::from_state(const std::array<std::int16_t, 40>& state) {
    EnvelopeRamp ramp;
    // The lanes hold the last block mixed; lane 7 is its last sample.
    ramp.rate_left = Fixed(state[33], state[34]);
    ramp.rate_right = Fixed(state[36], state[37]);
    ramp.left = Fixed(state[7], state[15]);
    ramp.right = Fixed(state[23], state[31]);
    ramp.target_left = state[32];
    ramp.target_right = state[35];
    return ramp;
}

void EnvelopeRamp::step(double& volume_left, double& volume_right) {
    left = Advance(left, rate_left, target_left);
    right = Advance(right, rate_right, target_right);
    volume_left = left;
    volume_right = right;
}

} // namespace dkr::runtime::spatial_audio
