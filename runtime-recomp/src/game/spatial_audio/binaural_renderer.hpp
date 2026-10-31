#pragma once

#include "spatial_audio/half_band_decimator.hpp"

#include <phonon.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::spatial_audio {

// A direction in the listener's frame: +x right, +y up, -z ahead.
struct Direction {
    float x = 0.0F;
    float y = 0.0F;
    float z = -1.0F;
};

// A mono sound placed around the listener. Called on the audio thread.
class Source {
public:
    virtual ~Source() = default;
    // Writes `frames` mono samples at the renderer's processing rate and the
    // direction for them. Returns false once the sound has ended; the samples
    // of that call are not played, so a last partial block is zero-padded and
    // returned with true.
    virtual bool render(float* mono, std::size_t frames, Direction& direction) = 0;
};

// Renders sources binaurally (HRTF) through Steam Audio and adds them to
// interleaved stereo output. Steam Audio's built-in HRTF needs 44.1 or 48 kHz;
// an output at half that rate (the game's 22,050 Hz) is rendered at double the
// rate and halved again.
//
// Not thread-safe: one thread owns every call, as Steam Audio's effects that
// share an HRTF must run on one thread. Nothing allocates after start().
class Renderer {
public:
    static constexpr std::size_t kVoiceCount = 16;
    static constexpr std::size_t kFrameSize = 256;

    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Prepares rendering for this output rate; already running at it, keeps
    // its voices. On failure the renderer stays stopped and logs why.
    bool start(std::uint32_t output_rate);
    void stop();
    bool running() const { return hrtf_ != nullptr; }
    std::uint32_t output_rate() const { return output_rate_; }
    std::uint32_t processing_rate() const { return processing_rate_; }

    // Plays a source in a free voice; the renderer does not own it. A source
    // stays referenced until it ends and its tail has played out.
    bool play(Source& source);
    bool playing(const Source& source) const;
    bool active() const;

    // Adds every voice to `frames` interleaved L,R frames at the output rate.
    void mix_into(float* stereo, std::size_t frames);

private:
    enum class VoiceState { Idle, Playing, Tail };
    struct Voice {
        Source* source = nullptr;
        VoiceState state = VoiceState::Idle;
        IPLBinauralEffect effect = nullptr;
    };

    void render_block();

    IPLContext context_ = nullptr;
    IPLHRTF hrtf_ = nullptr;
    std::uint32_t output_rate_ = 0;
    std::uint32_t processing_rate_ = 0;
    std::array<Voice, kVoiceCount> voices_{};
    IPLAudioBuffer mono_{};
    IPLAudioBuffer voice_out_{};
    // One rendered block, interleaved at the processing rate, then at the
    // output rate; output_read_ marks what mix_into has used.
    std::array<float, kFrameSize * 2> block_{};
    std::array<float, kFrameSize * 2> output_{};
    std::size_t output_frames_ = 0;
    std::size_t output_read_ = 0;
    HalfBandDecimator decimator_;
};

} // namespace dkr::runtime::spatial_audio
