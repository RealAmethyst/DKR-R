#pragma once

#include <array>
#include <cstddef>

namespace dkr::runtime::spatial_audio {

constexpr std::size_t kHalfBandTaps = 63;

// The shared low-pass at a quarter of the higher rate, unity gain at DC.
const std::array<float, kHalfBandTaps>& half_band_coefficients();

// Halves the sample rate of interleaved stereo audio. A windowed-sinc
// half-band low-pass removes everything above the new Nyquist frequency
// before every second frame is kept, so nothing folds back as aliasing.
class HalfBandDecimator {
public:
    static constexpr std::size_t kTaps = kHalfBandTaps;

    void reset();
    // Reads 2 * output_frames input frames and writes output_frames frames.
    void process(const float* input, float* output, std::size_t output_frames);

private:
    // Each channel's history doubled, so a filter window is one contiguous span.
    std::array<float, kTaps * 2> left_{};
    std::array<float, kTaps * 2> right_{};
    std::size_t position_ = 0;

    void push(float left, float right);
};

// Doubles the sample rate of mono audio: each sample is followed by a zero
// and the result is filtered with the same low-pass, at twice the gain.
class HalfBandInterpolator {
public:
    void reset();
    // Reads `frames` samples and writes 2 * frames.
    void process(const float* input, float* output, std::size_t frames);

private:
    std::array<float, kHalfBandTaps * 2> history_{};
    std::size_t position_ = 0;

    float push(float sample);
};

} // namespace dkr::runtime::spatial_audio
