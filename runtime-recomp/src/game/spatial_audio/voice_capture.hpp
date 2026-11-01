#pragma once

#include "spatial_audio/alist_rewrite.hpp"
#include "spatial_audio/binaural_renderer.hpp"
#include "spatial_audio/direction.hpp"
#include "spatial_audio/half_band_decimator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::spatial_audio {

// Longer than any frame DKR's audio manager makes (720-848 samples NTSC).
constexpr std::size_t kMaxFrameSamples = 1024;

// The positioned voices to capture in the next audio frame.
struct CapturePlan {
    std::size_t count = 0;
    // Each voice's envmixer state address (PVoice + 0xA0).
    std::array<std::uint32_t, alist::kMaxVoices> keys{};
    std::array<Direction, alist::kMaxVoices> directions{};
    // The positioned sound that owned the voice when the snapshot was taken
    // (ALSoundState, the emitter's handle field), re-checked at the task.
    std::array<std::uint32_t, alist::kMaxVoices> sound_states{};
    std::array<std::uint32_t, alist::kMaxVoices> handle_fields{};
};

// One voice's captured audio for a frame: its mono signal with the game's
// volume applied (not its pan), at the game's output rate.
struct CapturedVoice {
    std::uint32_t key = 0;
    Direction direction;
    std::array<float, kMaxFrameSamples> mono{};
};

// Everything captured in one audio frame, matched to the output buffer the
// game later queues by its address.
struct FrameCapture {
    std::uint32_t output_address = 0;
    std::uint32_t frame_samples = 0;
    std::size_t voice_count = 0;
    std::array<CapturedVoice, alist::kMaxVoices> voices{};
};

// A captured voice played through the renderer: frames are pushed at the
// output rate as they arrive, doubled to the processing rate, and pulled in
// renderer blocks. One renderer block of silence leads, so a block is never
// pulled before its samples arrived. Audio thread only.
class CaptureSource final : public Source {
public:
    void begin(std::uint32_t key);
    std::uint32_t key() const { return key_; }
    void push(const float* mono, std::size_t frames, const Direction& direction);
    // No more frames will come; ends once what is queued has played.
    void finish() { finishing_ = true; }
    bool finishing() const { return finishing_; }
    bool render(float* mono, std::size_t frames, Direction& direction) override;

private:
    static constexpr std::size_t kCapacity = 8192;
    std::array<float, kCapacity> fifo_{};
    std::size_t read_ = 0;
    std::size_t size_ = 0;
    std::array<float, kMaxFrameSamples * 2> doubled_{};
    HalfBandInterpolator interpolator_;
    Direction direction_;
    std::uint32_t key_ = 0;
    bool finishing_ = false;
};

} // namespace dkr::runtime::spatial_audio
