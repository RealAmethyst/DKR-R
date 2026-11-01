#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Rewrites one audio frame's command list (alist) so that the game's own
// audio microcode saves chosen voices' mono signals and leaves their dry
// signal out of the mix. Command formats are libultra ABI1 (include/PR/abi.h)
// as run by DKR's recompiled microcode; research:
// docs/notes/2026-11-01-positioned-voice-capture.md.
namespace dkr::runtime::spatial_audio::alist {

constexpr std::uint32_t kAdpcm = 1;
constexpr std::uint32_t kClearBuffer = 2;
constexpr std::uint32_t kEnvMixer = 3;
constexpr std::uint32_t kResample = 5;
constexpr std::uint32_t kSaveBuffer = 6;
constexpr std::uint32_t kSetBuffer = 8;
constexpr std::uint32_t kSetVolume = 9;
constexpr std::uint32_t kInterleave = 13;

constexpr std::uint32_t kFlagInit = 0x01;
constexpr std::uint32_t kFlagLeft = 0x02;
constexpr std::uint32_t kFlagVolume = 0x04;
constexpr std::uint32_t kFlagAux = 0x08;

// DMEM offset (relative to the buffer base) of the main left output.
constexpr std::uint32_t kMainLeftOut = 1088;

constexpr std::size_t kMaxVoices = 16;
constexpr std::size_t kMaxChains = 256;

constexpr std::uint32_t physical(std::uint32_t address) { return address & 0x1FFFFFFFU; }

// The SETVOL values an INIT chain gives the envmixer.
struct InitGains {
    std::int16_t volume_left = 0;
    std::int16_t volume_right = 0;
    std::int16_t target_left = 0;
    std::int16_t target_right = 0;
    // 16.16 per 8-sample block (env.c _getRate).
    std::int32_t rate_left = 0;
    std::int32_t rate_right = 0;
    std::int16_t dry = 0;
    std::int16_t wet = 0;
};

// One captured envmixer chain: `samples` mono samples saved at
// `capture_offset` bytes into the capture area, which belong at
// `frame_offset` samples into the frame's output.
struct Chain {
    std::uint8_t voice = 0;
    bool init = false;
    InitGains gains;
    std::uint32_t frame_offset = 0;
    std::uint32_t samples = 0;
    std::uint32_t capture_offset = 0;
};

struct Result {
    std::size_t words = 0;
    // The frame's output buffer (physical) and length in stereo frames.
    std::uint32_t output_address = 0;
    std::uint32_t frame_samples = 0;
    std::size_t chain_count = 0;
    std::array<Chain, kMaxChains> chains{};
    // Per requested voice: captured chains appear in the list, and the first
    // captured chain is INIT (so its pre-task envmixer state is not the one
    // used). A voice that starts a new sound within the frame (its decoder or
    // resampler starting over) is captured from that sound's first chain on.
    std::array<bool, kMaxVoices> present{};
    std::array<bool, kMaxVoices> first_chain_init{};
    // The last INIT chain's real dry and wet amounts, per voice.
    std::array<bool, kMaxVoices> has_init{};
    std::array<std::int16_t, kMaxVoices> last_init_dry{};
    std::array<std::int16_t, kMaxVoices> last_init_wet{};
};

// `voices` holds each voice's envmixer state address (any segment). The
// rewritten list goes to `out`; captures are laid out from 0 in an area of
// `capture_bytes`, each chain's save landing at `capture_base` plus its
// offset. Dry (and, with `zero_wet`, wet) amounts in captured INIT chains are
// zeroed in the copy. Returns false on anything not understood, so the caller
// runs the original list unchanged.
bool rewrite(std::span<const std::uint32_t> in, std::span<std::uint32_t> out,
             std::span<const std::uint32_t> voices, std::uint32_t capture_base,
             std::uint32_t capture_bytes, bool zero_wet, Result& result);

} // namespace dkr::runtime::spatial_audio::alist
