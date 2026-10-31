#pragma once

#include "spatial_audio/binaural_renderer.hpp"

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::spatial_audio {

// A listening test for the renderer (DKR_SPATIAL_AUDIO_TEST_TONE=1): three
// short noise bursts on the left, then on the right, ahead and behind, with a
// pause after each position, repeating until the game closes. Broadband noise
// carries more direction cues than a pure tone.
class TestTone final : public Source {
public:
    static constexpr std::size_t kPositions = 4;

    explicit TestTone(std::uint32_t sample_rate);
    bool render(float* mono, std::size_t frames, Direction& direction) override;

    // Position (0 left, 1 right, 2 ahead, 3 behind) and level at a frame
    // offset into the cycle; exposed for tests.
    std::size_t position_at(std::uint64_t frame) const;
    bool sounding_at(std::uint64_t frame) const;

private:
    std::uint64_t burst_frames_;
    std::uint64_t gap_frames_;
    std::uint64_t pause_frames_;
    std::uint64_t ramp_frames_;
    std::uint64_t position_frames_;
    std::uint64_t frame_ = 0;
    std::uint32_t noise_ = 0x2545F491U;
};

} // namespace dkr::runtime::spatial_audio
