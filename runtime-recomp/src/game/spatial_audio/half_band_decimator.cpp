#include "spatial_audio/half_band_decimator.hpp"

#include <cmath>

namespace dkr::runtime::spatial_audio {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Sinc low-pass at a quarter of the input rate under a Blackman window.
// Measured from 44.1 to 22.05 kHz: within 0.5 dB up to 10 kHz, at least 74 dB
// down from 13 kHz, so what folds back lands only in the 9-11 kHz edge.
std::array<float, HalfBandDecimator::kTaps> MakeCoefficients() {
    std::array<float, HalfBandDecimator::kTaps> coefficients{};
    constexpr int kCentre = static_cast<int>(HalfBandDecimator::kTaps / 2);
    double sum = 0.0;
    for (int tap = 0; tap < static_cast<int>(HalfBandDecimator::kTaps); ++tap) {
        const int offset = tap - kCentre;
        const double sinc = offset == 0 ? 0.5 : std::sin(kPi * offset / 2.0) / (kPi * offset);
        const double phase = 2.0 * kPi * tap / (HalfBandDecimator::kTaps - 1);
        const double window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
        coefficients[static_cast<std::size_t>(tap)] = static_cast<float>(sinc * window);
        sum += sinc * window;
    }
    for (float& coefficient : coefficients) {
        coefficient = static_cast<float>(coefficient / sum);
    }
    return coefficients;
}

const std::array<float, HalfBandDecimator::kTaps> kCoefficients = MakeCoefficients();

float Filter(const float* history) {
    float sum = 0.0F;
    for (std::size_t tap = 0; tap < HalfBandDecimator::kTaps; ++tap) {
        sum += history[tap] * kCoefficients[tap];
    }
    return sum;
}

} // namespace

void HalfBandDecimator::reset() {
    left_.fill(0.0F);
    right_.fill(0.0F);
    position_ = 0;
}

void HalfBandDecimator::push(float left, float right) {
    left_[position_] = left;
    left_[position_ + kTaps] = left;
    right_[position_] = right;
    right_[position_ + kTaps] = right;
    position_ = (position_ + 1) % kTaps;
}

void HalfBandDecimator::process(const float* input, float* output,
                                std::size_t output_frames) {
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        push(input[frame * 4], input[frame * 4 + 1]);
        push(input[frame * 4 + 2], input[frame * 4 + 3]);
        // position_ is the oldest sample; the window runs oldest to newest.
        output[frame * 2] = Filter(left_.data() + position_);
        output[frame * 2 + 1] = Filter(right_.data() + position_);
    }
}

} // namespace dkr::runtime::spatial_audio
