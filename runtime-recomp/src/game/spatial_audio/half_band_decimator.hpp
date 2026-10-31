#pragma once

#include <array>
#include <cstddef>

namespace dkr::runtime::spatial_audio {

// Halves the sample rate of interleaved stereo audio. A windowed-sinc
// half-band low-pass removes everything above the new Nyquist frequency
// before every second frame is kept, so nothing folds back as aliasing.
class HalfBandDecimator {
public:
    static constexpr std::size_t kTaps = 63;

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

} // namespace dkr::runtime::spatial_audio
