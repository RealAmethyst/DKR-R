#include "spatial_audio/alist_rewrite.hpp"

namespace dkr::runtime::spatial_audio::alist {
namespace {

std::uint32_t Opcode(std::uint32_t w0) { return w0 >> 24; }
std::uint32_t Flags(std::uint32_t w0) { return (w0 >> 16) & 0xFFU; }
std::int16_t Low(std::uint32_t word) { return static_cast<std::int16_t>(word & 0xFFFFU); }

int VoiceIndex(std::span<const std::uint32_t> voices, std::uint32_t key) {
    for (std::size_t index = 0; index < voices.size(); ++index) {
        if (physical(voices[index]) == physical(key)) return static_cast<int>(index);
    }
    return -1;
}

// The envmixer's input setup is SETBUFF(A_MAIN, in, MAIN_L + n, count)
// directly followed by SETBUFF(A_AUX, ...) (env.c, alEnvmixerPull); its
// ENVMIXER follows within the five INIT SETVOLs.
constexpr std::size_t kEnvMixerWindow = 7;

} // namespace

bool rewrite(std::span<const std::uint32_t> in, std::span<std::uint32_t> out,
             std::span<const std::uint32_t> voices, std::uint32_t capture_base,
             std::uint32_t capture_bytes, bool zero_wet, Result& result) {
    result = Result{};
    if (in.size() % 2 != 0 || voices.size() > kMaxVoices) return false;
    const std::size_t commands = in.size() / 2;
    std::size_t written = 0;
    std::uint32_t capture_cursor = 0;
    std::uint32_t last_count = 0;
    bool after_interleave = false;
    // While copying a captured chain: its index and its ENVMIXER's index.
    Chain* chain = nullptr;
    std::size_t chain_end = 0;

    const auto emit = [&](std::uint32_t w0, std::uint32_t w1) {
        if (written + 2 > out.size()) return false;
        out[written++] = w0;
        out[written++] = w1;
        return true;
    };

    // A voice that starts a new sound partway through the frame (stolen or
    // restarted) first plays out the previous sound's chains, which stay in
    // the game mix; it is captured from the new sound's first chain on. A new
    // sound is recognised by its decoder or resampler starting over (their
    // INIT flag, set after the voice's filter reset, env.c / load.c /
    // resample.c). An envmixer INIT alone is a volume, pan or effect change
    // (env.c forces one for each), within the same sound.
    std::array<std::size_t, kMaxVoices> first_capture{};
    {
        std::array<bool, kMaxVoices> seen{};
        std::array<bool, kMaxVoices> restarted{};
        bool decoder_start = false;
        for (std::size_t index = 0; index < commands; ++index) {
            const std::uint32_t op = Opcode(in[index * 2]);
            if ((op == kAdpcm || op == kResample) && (Flags(in[index * 2]) & kFlagInit) != 0) {
                decoder_start = true;
            }
            if (op != kEnvMixer) continue;
            const bool chain_starts_sound = decoder_start;
            decoder_start = false;
            const int voice = VoiceIndex(voices, in[index * 2 + 1]);
            if (voice < 0 || restarted[voice]) continue;
            if (chain_starts_sound) {
                restarted[voice] = true;
                if (seen[voice]) first_capture[voice] = index;
            }
            seen[voice] = true;
        }
    }

    for (std::size_t index = 0; index < commands; ++index) {
        std::uint32_t w0 = in[index * 2];
        std::uint32_t w1 = in[index * 2 + 1];
        const std::uint32_t op = Opcode(w0);

        if (op == kSetBuffer && (Flags(w0) & kFlagAux) == 0) {
            last_count = w1 & 0xFFFFU;
            const bool envmixer_input = index + 1 < commands &&
                                        Opcode(in[index * 2 + 2]) == kSetBuffer &&
                                        (Flags(in[index * 2 + 2]) & kFlagAux) != 0;
            if (envmixer_input && !voices.empty()) {
                std::size_t mixer = 0;
                for (std::size_t look = index + 2; look < commands && look <= index + kEnvMixerWindow;
                     ++look) {
                    if (Opcode(in[look * 2]) == kEnvMixer) {
                        mixer = look;
                        break;
                    }
                }
                if (mixer == 0) return false;
                const int voice = VoiceIndex(voices, in[mixer * 2 + 1]);
                if (voice >= 0 && mixer >= first_capture[voice]) {
                    const std::uint32_t input = w0 & 0xFFFFU;
                    const std::uint32_t main_out = w1 >> 16;
                    const std::uint32_t count = w1 & 0xFFFFU;
                    if (main_out < kMainLeftOut || (main_out - kMainLeftOut) % 2 != 0 ||
                        count % 2 != 0 || result.chain_count == kMaxChains) {
                        return false;
                    }
                    const std::uint32_t slot = (count + 15U) & ~15U;
                    if (capture_cursor + slot > capture_bytes) return false;
                    chain = &result.chains[result.chain_count++];
                    chain->voice = static_cast<std::uint8_t>(voice);
                    chain->init = (Flags(in[mixer * 2]) & kFlagInit) != 0;
                    chain->frame_offset = result.frame_samples + (main_out - kMainLeftOut) / 2;
                    chain->samples = count / 2;
                    chain->capture_offset = capture_cursor;
                    capture_cursor += slot;
                    chain_end = mixer;
                    // SETBUFF(0, -, out = envmixer input, count), then SAVEBUFF.
                    if (!emit(kSetBuffer << 24, (input << 16) | count) ||
                        !emit(kSaveBuffer << 24, capture_base + chain->capture_offset)) {
                        return false;
                    }
                }
            }
        }

        if (chain != nullptr && op == kSetVolume) {
            const std::uint32_t flags = Flags(w0);
            const bool left = (flags & kFlagLeft) != 0;
            if ((flags & kFlagAux) != 0) {
                chain->gains.dry = Low(w0);
                chain->gains.wet = Low(w1);
                w0 &= 0xFFFF0000U;
                if (zero_wet) w1 &= 0xFFFF0000U;
            } else if ((flags & kFlagVolume) != 0) {
                (left ? chain->gains.volume_left : chain->gains.volume_right) = Low(w0);
            } else {
                (left ? chain->gains.target_left : chain->gains.target_right) = Low(w0);
                (left ? chain->gains.rate_left : chain->gains.rate_right) = static_cast<std::int32_t>(w1);
            }
        }

        if (op == kInterleave) after_interleave = true;
        if (op == kSaveBuffer && after_interleave) {
            // The frame's output: SETBUFF(count = frames * 4), SAVEBUFF(out).
            if (result.output_address == 0) result.output_address = physical(w1);
            result.frame_samples += last_count / 4;
            after_interleave = false;
        }

        if (!emit(w0, w1)) return false;

        if (chain != nullptr && index == chain_end) {
            const std::uint8_t voice = chain->voice;
            if (!result.present[voice]) {
                result.present[voice] = true;
                result.first_chain_init[voice] = chain->init;
            }
            if (chain->init) {
                result.has_init[voice] = true;
                result.last_init_dry[voice] = chain->gains.dry;
                result.last_init_wet[voice] = chain->gains.wet;
            }
            chain = nullptr;
        }
    }
    if (chain != nullptr || result.output_address == 0) return false;
    result.words = written;
    return true;
}

} // namespace dkr::runtime::spatial_audio::alist
