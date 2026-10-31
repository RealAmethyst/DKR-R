#include "spatial_audio/test_tone.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace dkr::runtime::spatial_audio {
namespace {

constexpr int kBursts = 3;
constexpr float kLevel = 6000.0F;
constexpr double kPi = 3.14159265358979323846;

constexpr std::array<Direction, TestTone::kPositions> kDirections{{
    {-1.0F, 0.0F, 0.0F},
    {1.0F, 0.0F, 0.0F},
    {0.0F, 0.0F, -1.0F},
    {0.0F, 0.0F, 1.0F},
}};

} // namespace

TestTone::TestTone(std::uint32_t sample_rate)
    : burst_frames_(sample_rate * 150ULL / 1000ULL),
      gap_frames_(sample_rate * 100ULL / 1000ULL),
      pause_frames_(sample_rate * 1200ULL / 1000ULL),
      ramp_frames_(sample_rate * 5ULL / 1000ULL),
      position_frames_(kBursts * (burst_frames_ + gap_frames_) + pause_frames_) {}

std::size_t TestTone::position_at(std::uint64_t frame) const {
    return static_cast<std::size_t>((frame / position_frames_) % kPositions);
}

bool TestTone::sounding_at(std::uint64_t frame) const {
    const std::uint64_t offset = frame % position_frames_;
    if (offset >= kBursts * (burst_frames_ + gap_frames_)) return false;
    return offset % (burst_frames_ + gap_frames_) < burst_frames_;
}

bool TestTone::render(float* mono, std::size_t frames, Direction& direction) {
    // A block is placed where it starts; blocks are a few milliseconds and
    // every burst lies inside one position.
    direction = kDirections[position_at(frame_)];
    for (std::size_t index = 0; index < frames; ++index, ++frame_) {
        float sample = 0.0F;
        if (sounding_at(frame_)) {
            const std::uint64_t into_burst =
                (frame_ % position_frames_) % (burst_frames_ + gap_frames_);
            const std::uint64_t edge = std::min(into_burst, burst_frames_ - 1 - into_burst);
            float envelope = 1.0F;
            if (edge < ramp_frames_) {
                envelope = static_cast<float>(
                    0.5 - 0.5 * std::cos(kPi * static_cast<double>(edge) / ramp_frames_));
            }
            noise_ ^= noise_ << 13;
            noise_ ^= noise_ >> 17;
            noise_ ^= noise_ << 5;
            const float white = static_cast<float>(noise_) / 4294967296.0F * 2.0F - 1.0F;
            sample = white * kLevel * envelope;
        }
        mono[index] = sample;
    }
    return true;
}

} // namespace dkr::runtime::spatial_audio
