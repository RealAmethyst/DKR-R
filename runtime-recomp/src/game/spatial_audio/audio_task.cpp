// Captures the game's positioned voices for binaural rendering by running a
// rewritten copy of each audio frame's command list (research and evidence:
// docs/notes/2026-11-01-positioned-voice-capture.md).
#include "spatial_audio/audio_task.hpp"

#include "accessibility/symbol_lookup.hpp"
#include "spatial_audio/alist_rewrite.hpp"
#include "spatial_audio/envelope.hpp"
#include "spatial_audio/spatial_audio.hpp"
#include "spatial_audio/trace.hpp"
#include "spatial_audio/voice_capture.hpp"

#include "recomp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace dkr::runtime::spatial_audio {
namespace {

// Scratch RDRAM for the rewritten list and the saved voices. The microcode's
// DMA reaches only the first 16 MB; game RAM ends at 0x80800000 and
// n64-modern-runtime keeps its PI handles at 0x80800000 and patch data from
// 0x80801000 (DKR-R registers none), mods and its heap from 0x81000000. The
// top megabyte of that window is used by nothing else.
constexpr std::uint32_t kScratchList = 0x80F00000U;
constexpr std::uint32_t kScratchListBytes = 0x20000U;
constexpr std::uint32_t kScratchCaptures = 0x80F20000U;
constexpr std::uint32_t kScratchCaptureBytes = 0x40000U;
static_assert(kScratchCaptures + kScratchCaptureBytes <= 0x81000000U);

// OSTask in DMEM (copied there by recomp::rsp::run_task): data_ptr, data_size.
constexpr std::uint32_t kTask = 0xFC0;
constexpr std::uint32_t kTaskDataPointer = 0x30;
constexpr std::uint32_t kTaskDataSize = 0x34;
// ENVMIX_STATE: dry and wet amounts.
constexpr std::uint32_t kStateDry = 0x4C;
constexpr std::uint32_t kStateWet = 0x4E;
// DKR's command lists are well below this (audiomgr's list buffers).
constexpr std::size_t kMaxListWords = 0x10000 / 4;

std::array<std::uint32_t, kMaxListWords> g_list;
std::array<std::uint32_t, kScratchListBytes / 4> g_rewritten;
alist::Result g_result;
CapturePlan g_plan;
FrameCapture g_frame;

gpr Reg(std::uint32_t physical) {
    return static_cast<gpr>(static_cast<std::int32_t>(0x80000000U | physical));
}

std::array<std::int16_t, 40> ReadState(std::uint8_t* rdram, std::uint32_t key) {
    std::array<std::int16_t, 40> state{};
    for (std::uint32_t index = 0; index < state.size(); ++index) {
        state[index] = MEM_H(index * 2, Reg(key));
    }
    return state;
}

// alFXEnabled: with the game's reverb off its aux (wet) sends are mixed
// straight into the main output, so they are captured with the dry signal.
std::optional<bool> FxEnabled(std::uint8_t* rdram) {
    static const std::optional<std::uint32_t> address = accessibility::symbols::data("alFXEnabled", 1);
    if (!address) return std::nullopt;
    return MEM_B(0, Reg(alist::physical(*address))) != 0;
}

void Report(const char* reason) {
    static bool reported = false;
    if (reported) return;
    reported = true;
    std::fprintf(stderr, "[boot][spatial-audio] voice capture skipped: %s\n", reason);
}

// The snapshot saw each voice playing a positioned sound, up to a tick ago;
// the game's audio thread may since have freed it or given it to another
// sound (voices are shared with music and unpositioned sounds and can be
// stolen). Keeps only voices still owned by that sound: the handle field
// still holds the state, the state points back at it and is playing, and its
// voice's back-pointer and envmixer state are this voice's. RDRAM is stable
// here: the game's audio thread waits for this task.
void KeepOwnedVoices(std::uint8_t* rdram, CapturePlan& plan) {
    std::size_t kept = 0;
    for (std::size_t voice = 0; voice < plan.count; ++voice) {
        const std::uint32_t state = alist::physical(plan.sound_states[voice]);
        const std::uint32_t field = alist::physical(plan.handle_fields[voice]);
        if (state == 0 || field == 0 || state + 0x40 > 0x800000U || field + 4 > 0x800000U) continue;
        const auto handle = static_cast<std::uint32_t>(MEM_W(0, Reg(field)));
        const auto user_handle = static_cast<std::uint32_t>(MEM_W(0x30, Reg(state)));
        const std::int8_t playing = MEM_B(0x3F, Reg(state));
        const std::uint32_t pvoice = alist::physical(static_cast<std::uint32_t>(MEM_W(0x14, Reg(state))));
        trace::Counters& counters = trace::counters();
        if (alist::physical(handle) != state) {
            trace::count(counters.reject_handle);
            continue;
        }
        if (alist::physical(user_handle) != field) {
            trace::count(counters.reject_user_handle);
            continue;
        }
        if (playing != 1) {
            trace::count(counters.reject_not_playing);
            continue;
        }
        if (pvoice == 0 || pvoice + 0xA4 > 0x800000U) {
            trace::count(counters.reject_voice);
            continue;
        }
        const auto back = static_cast<std::uint32_t>(MEM_W(0x08, Reg(pvoice)));
        const auto envmixer = static_cast<std::uint32_t>(MEM_W(0xA0, Reg(pvoice)));
        if (alist::physical(back) != state + 0x0C || alist::physical(envmixer) != plan.keys[voice]) {
            trace::count(counters.reject_voice);
            continue;
        }
        plan.keys[kept] = plan.keys[voice];
        plan.directions[kept] = plan.directions[voice];
        plan.sound_states[kept] = plan.sound_states[voice];
        plan.handle_fields[kept] = plan.handle_fields[voice];
        ++kept;
    }
    plan.count = kept;
}

} // namespace

RspExitReason run_audio_task(std::uint8_t* rdram, std::uint32_t ucode_address,
                             RspUcodeFunc* microcode) {
    if (!capture_plan(g_plan)) return microcode(rdram, ucode_address);
    trace::Counters& counters = trace::counters();
    trace::count(counters.planned_tasks);
    trace::count(counters.planned_voices, static_cast<std::uint32_t>(g_plan.count));
    KeepOwnedVoices(rdram, g_plan);
    trace::count(counters.kept_voices, static_cast<std::uint32_t>(g_plan.count));
    if (g_plan.count == 0) return microcode(rdram, ucode_address);
    const std::optional<bool> fx_enabled = FxEnabled(rdram);
    const std::uint32_t list_address = alist::physical(RSP_MEM_W_LOAD(kTaskDataPointer, kTask));
    const std::uint32_t list_bytes = RSP_MEM_W_LOAD(kTaskDataSize, kTask);
    if (!fx_enabled) {
        Report("alFXEnabled not found");
        return microcode(rdram, ucode_address);
    }
    if (list_bytes == 0 || list_bytes % 8 != 0 || list_bytes > kMaxListWords * 4 ||
        list_address + list_bytes > 0x800000U) {
        Report("unexpected command list size");
        return microcode(rdram, ucode_address);
    }
    const std::size_t words = list_bytes / 4;
    for (std::size_t index = 0; index < words; ++index) {
        g_list[index] = static_cast<std::uint32_t>(MEM_W(static_cast<std::int32_t>(index * 4), Reg(list_address)));
    }
    const bool zero_wet = !*fx_enabled;
    if (!alist::rewrite(std::span<const std::uint32_t>(g_list.data(), words), g_rewritten,
                        std::span<const std::uint32_t>(g_plan.keys.data(), g_plan.count),
                        alist::physical(kScratchCaptures), kScratchCaptureBytes, zero_wet, g_result) ||
        g_result.chain_count == 0 || g_result.frame_samples > kMaxFrameSamples) {
        trace::count(counters.rewrite_failed);
        return microcode(rdram, ucode_address);
    }
    for (std::size_t chain = 0; chain < g_result.chain_count; ++chain) {
        const alist::Chain& captured = g_result.chains[chain];
        if (captured.frame_offset + captured.samples > g_result.frame_samples) {
            return microcode(rdram, ucode_address);
        }
    }

    // Voices whose first chain continues use their envmixer state's gains:
    // keep them for the gain and restore, and zero the dry amount (and wet).
    std::array<std::array<std::int16_t, 40>, alist::kMaxVoices> states{};
    for (std::size_t voice = 0; voice < g_plan.count; ++voice) {
        if (!g_result.present[voice] || g_result.first_chain_init[voice]) continue;
        states[voice] = ReadState(rdram, g_plan.keys[voice]);
        MEM_H(kStateDry, Reg(g_plan.keys[voice])) = 0;
        if (zero_wet) MEM_H(kStateWet, Reg(g_plan.keys[voice])) = 0;
    }

    for (std::size_t index = 0; index < g_result.words; ++index) {
        MEM_W(static_cast<std::int32_t>(index * 4), Reg(alist::physical(kScratchList))) =
            static_cast<std::int32_t>(g_rewritten[index]);
    }
    RSP_MEM_W_STORE(kTaskDataPointer, kTask, alist::physical(kScratchList));
    RSP_MEM_W_STORE(kTaskDataSize, kTask, static_cast<std::uint32_t>(g_result.words * 4));
    const RspExitReason exit = microcode(rdram, ucode_address);

    // The voices' real amounts for the frames after this one.
    for (std::size_t voice = 0; voice < g_plan.count; ++voice) {
        if (!g_result.present[voice]) continue;
        const bool init = g_result.has_init[voice];
        MEM_H(kStateDry, Reg(g_plan.keys[voice])) = init ? g_result.last_init_dry[voice] : states[voice][38];
        if (zero_wet) {
            MEM_H(kStateWet, Reg(g_plan.keys[voice])) = init ? g_result.last_init_wet[voice] : states[voice][39];
        }
    }

    // Each voice's mono signal with the game's volume: the envmixer's ramp
    // and dry (plus wet with reverb off) amount; the pan is not applied, the
    // renderer places the voice instead. Equal-power pan keeps
    // left^2 + right^2 constant, so their mean is the level at either ear of
    // a centred sound.
    g_frame.output_address = g_result.output_address;
    g_frame.frame_samples = g_result.frame_samples;
    g_frame.voice_count = 0;
    std::array<int, alist::kMaxVoices> slot{};
    std::array<EnvelopeRamp, alist::kMaxVoices> ramps{};
    std::array<double, alist::kMaxVoices> amount{};
    for (std::size_t voice = 0; voice < g_plan.count; ++voice) {
        slot[voice] = -1;
        if (!g_result.present[voice]) continue;
        slot[voice] = static_cast<int>(g_frame.voice_count);
        CapturedVoice& captured = g_frame.voices[g_frame.voice_count++];
        captured.key = g_plan.keys[voice];
        captured.direction = g_plan.directions[voice];
        std::fill_n(captured.mono.begin(), g_frame.frame_samples, 0.0F);
        if (!g_result.first_chain_init[voice]) {
            ramps[voice] = EnvelopeRamp::from_state(states[voice]);
            amount[voice] = states[voice][38] + (zero_wet ? states[voice][39] : 0);
        }
    }
    for (std::size_t chain = 0; chain < g_result.chain_count; ++chain) {
        const alist::Chain& captured = g_result.chains[chain];
        if (captured.init) {
            ramps[captured.voice] = EnvelopeRamp::from_init(captured.gains);
            amount[captured.voice] = captured.gains.dry + (zero_wet ? captured.gains.wet : 0);
        }
        CapturedVoice& out = g_frame.voices[static_cast<std::size_t>(slot[captured.voice])];
        const double scale = amount[captured.voice] / (32768.0 * 32768.0);
        for (std::uint32_t index = 0; index < captured.samples; ++index) {
            const std::int16_t sample = MEM_H(static_cast<std::int32_t>(captured.capture_offset + index * 2),
                                             Reg(alist::physical(kScratchCaptures)));
            double left = 0.0;
            double right = 0.0;
            ramps[captured.voice].step(left, right);
            const double level = std::sqrt((left * left + right * right) / 2.0);
            out.mono[captured.frame_offset + index] += static_cast<float>(sample * level * scale);
        }
    }
    trace::count(counters.captured_voices, static_cast<std::uint32_t>(g_frame.voice_count));
    trace::count(counters.submitted);
    submit_capture(g_frame, rdram);
    return exit;
}

} // namespace dkr::runtime::spatial_audio
