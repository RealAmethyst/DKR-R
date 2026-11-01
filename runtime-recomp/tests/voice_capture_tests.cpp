// Runs DKR's recompiled audio microcode on synthetic command lists to check
// the voice capture rewrite against the microcode itself:
//  - a captured voice's dry signal is gone from the mix, bit for bit, on its
//    INIT frame and, with its envmixer state's dry amount zeroed, on the
//    following CONTINUE frame;
//  - other voices are untouched;
//  - the saved signal is what the envmixer mixed (output = capture * gain),
//    with the gain followed from the list (INIT) or the envmixer state
//    (CONTINUE), whose layout is checked here too.
// Needs the microcode's data segment (ROM-derived, never committed); the
// test takes its path as the first argument.
#include "spatial_audio/alist_rewrite.hpp"
#include "spatial_audio/envelope.hpp"

#include "recomp.h"
#include "librecomp/rsp.hpp"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

// librecomp defines these in the game; the audio commands tested use no
// reciprocal tables.
uint8_t dmem[0x1000];
uint16_t rspReciprocals[512];
uint16_t rspInverseSquareRoots[512];
extern RspUcodeFunc dkrAspMain;

namespace alist = dkr::runtime::spatial_audio::alist;

namespace {

constexpr std::uint32_t kRdramBytes = 16U * 1024U * 1024U;
constexpr std::uint32_t kUcodeData = 0x80100000U;
constexpr std::uint32_t kList = 0x80110000U;
constexpr std::uint32_t kRewritten = 0x80F00000U;
constexpr std::uint32_t kCaptures = 0x80F10000U;
constexpr std::uint32_t kOutput = 0x80120000U;
constexpr std::uint32_t kSamplesX = 0x80130000U;
constexpr std::uint32_t kSamplesY = 0x80131000U;
constexpr std::uint32_t kStateX = 0x80140000U; // ENVMIX_STATE, 80 bytes
constexpr std::uint32_t kStateY = 0x80140100U;
constexpr std::uint32_t kResampleX = 0x80140200U; // RESAMPLE_STATE, 32 bytes
constexpr std::uint32_t kResampleY = 0x80140300U;

constexpr std::uint32_t kSamples = 160; // one subframe (AL_MAX_RSP_SAMPLES)
constexpr std::uint32_t kDecoderOut = 320;
constexpr std::uint32_t kMainR = 1408;
constexpr std::uint32_t kAuxL = 1728;
constexpr std::uint32_t kAuxR = 2048;

std::vector<std::uint8_t> g_ucode_data;
std::vector<std::uint8_t> g_memory(kRdramBytes);

std::uint8_t* Rdram() { return g_memory.data(); }

gpr Reg(std::uint32_t address) { return static_cast<gpr>(static_cast<std::int32_t>(address)); }

void Word(std::uint32_t address, std::uint32_t value) {
    std::uint8_t* rdram = Rdram();
    MEM_W(0, Reg(address)) = static_cast<std::int32_t>(value);
}

std::uint32_t ReadWord(std::uint32_t address) {
    std::uint8_t* rdram = Rdram();
    return static_cast<std::uint32_t>(MEM_W(0, Reg(address)));
}

void Half(std::uint32_t address, std::int16_t value) {
    std::uint8_t* rdram = Rdram();
    MEM_H(0, Reg(address)) = value;
}

std::int16_t ReadHalf(std::uint32_t address) {
    std::uint8_t* rdram = Rdram();
    return static_cast<std::int16_t>(MEM_H(0, Reg(address)));
}

std::uint32_t W0(std::uint32_t op, std::uint32_t flags, std::uint32_t low) {
    return (op << 24) | ((flags & 0xFF) << 16) | (low & 0xFFFF);
}

struct Voice {
    std::uint32_t samples;
    std::uint32_t state;
    std::uint32_t resample_state;
    std::int16_t volume;
    std::int16_t dry;
    std::int16_t wet;
    // A ramp toward target (env.c _getRate's 16.16 rate per 8 samples); a
    // zero rate keeps the volume.
    std::int16_t target = 0;
    std::int32_t rate = 0;
};

// env.c _getRate.
std::int32_t GetRate(double volume, double target, std::int32_t count) {
    double a = (target - volume) / static_cast<float>(count);
    a *= 8;
    if (a < 0.0) a -= 1.0;
    const auto whole = static_cast<std::int16_t>(a);
    // The MIPS float-to-integer conversion truncates; in C++ an out-of-range
    // conversion to a 16-bit type would be undefined, so go through 32 bits.
    const auto fraction = static_cast<std::uint16_t>(
        static_cast<std::int32_t>(0xffff * (a - static_cast<float>(whole))));
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(static_cast<std::uint16_t>(whole)) << 16) |
                                     fraction);
}

// One voice's chain as alEnvmixerPull and its sources build it (env.c,
// resample.c), with raw samples loaded in place of ADPCM decoding.
// `init` starts the voice's sound (decoder and resampler start over, as after
// a filter reset); `envmixer_init` alone is a volume or pan change, which
// env.c turns into an envmixer INIT within the same sound.
void AppendVoice(std::vector<std::uint32_t>& list, const Voice& voice, bool init,
                 bool envmixer_init = false) {
    const auto add = [&](std::uint32_t w0, std::uint32_t w1) {
        list.push_back(w0);
        list.push_back(w1);
    };
    add(W0(alist::kSetBuffer, 0, kDecoderOut), (0U << 16) | (kSamples * 2 + 32));
    add(W0(4 /* LOADBUFF */, 0, 0), voice.samples);
    add(W0(alist::kSetBuffer, 0, kDecoderOut), (0U << 16) | (kSamples * 2));
    add(W0(alist::kResample, init ? alist::kFlagInit : 0, 0x6000), voice.resample_state);
    add(W0(alist::kSetBuffer, 0, 0), (alist::kMainLeftOut << 16) | (kSamples * 2));
    add(W0(alist::kSetBuffer, alist::kFlagAux, kMainR), (kAuxL << 16) | kAuxR);
    if (init || envmixer_init) {
        add(W0(alist::kSetVolume, alist::kFlagLeft | alist::kFlagVolume, voice.volume), 0);
        add(W0(alist::kSetVolume, alist::kFlagVolume, voice.volume), 0);
        const std::int16_t target = voice.rate == 0 ? voice.volume : voice.target;
        add(W0(alist::kSetVolume, alist::kFlagLeft, target), static_cast<std::uint32_t>(voice.rate));
        add(W0(alist::kSetVolume, 0, target), static_cast<std::uint32_t>(voice.rate));
        add(W0(alist::kSetVolume, alist::kFlagAux, voice.dry), static_cast<std::uint16_t>(voice.wet));
    }
    add(W0(alist::kEnvMixer, (init || envmixer_init ? alist::kFlagInit : 0) | alist::kFlagAux, 0),
        voice.state);
}

std::vector<std::uint32_t> Frame(const std::vector<Voice>& voices, bool init) {
    std::vector<std::uint32_t> list;
    const auto add = [&](std::uint32_t w0, std::uint32_t w1) {
        list.push_back(w0);
        list.push_back(w1);
    };
    add(W0(7 /* SEGMENT */, 0, 0), 0);
    for (std::uint32_t buffer : {alist::kMainLeftOut, kMainR, kAuxL, kAuxR}) {
        add(W0(alist::kClearBuffer, 0, buffer), kSamples * 2);
    }
    for (const Voice& voice : voices) AppendVoice(list, voice, init);
    // save.c: interleave main L/R into DMEM 0 and save it.
    add(W0(alist::kSetBuffer, 0, 0), kSamples * 2);
    add(W0(alist::kInterleave, 0, 0), (alist::kMainLeftOut << 16) | kMainR);
    add(W0(alist::kSetBuffer, 0, 0), kSamples * 4);
    add(W0(alist::kSaveBuffer, 0, 0), alist::physical(kOutput));
    return list;
}

void Run(std::uint32_t list_address, std::size_t words) {
    std::uint8_t* rdram = Rdram();
    std::memset(dmem, 0, sizeof(dmem));
    dma_rdram_to_dmem(rdram, 0, alist::physical(kUcodeData), 0xF80 - 1);
    RSP_MEM_W_STORE(0x30, 0xFC0, list_address);
    RSP_MEM_W_STORE(0x34, 0xFC0, static_cast<std::uint32_t>(words * 4));
    const RspExitReason exit = dkrAspMain(rdram, 0);
    assert(exit == RspExitReason::Broke);
}

void Store(std::uint32_t address, const std::vector<std::uint32_t>& words) {
    for (std::size_t index = 0; index < words.size(); ++index) {
        Word(address + static_cast<std::uint32_t>(index) * 4, words[index]);
    }
}

std::vector<std::int16_t> Output(std::uint32_t frames = kSamples) {
    std::vector<std::int16_t> out(frames * 2);
    for (std::uint32_t index = 0; index < out.size(); ++index) out[index] = ReadHalf(kOutput + index * 2);
    return out;
}

void Reset() {
    std::fill(g_memory.begin(), g_memory.end(), 0);
    for (std::size_t index = 0; index < g_ucode_data.size(); ++index) {
        std::uint8_t* rdram = Rdram();
        MEM_B(static_cast<std::int32_t>(index), Reg(kUcodeData)) = static_cast<std::int8_t>(g_ucode_data[index]);
    }
    // Two different broadband signals.
    std::uint32_t noise = 12345U;
    for (std::uint32_t index = 0; index < 1024; ++index) {
        noise = noise * 1103515245U + 12345U;
        Half(kSamplesX + index * 2, static_cast<std::int16_t>((noise >> 16) % 16000) - 8000);
        Half(kSamplesY + index * 2,
             static_cast<std::int16_t>(6000.0 * std::sin(index * 0.07)));
    }
}

const Voice kX{kSamplesX, kStateX, kResampleX, 0x5000, 0x7FFF, 0};
const Voice kY{kSamplesY, kStateY, kResampleY, 0x4000, 0x6000, 0x2000};

// Runs `frames` frames (INIT, then CONTINUE) of a list, optionally capturing
// voice X; returns each frame's output.
std::vector<std::vector<std::int16_t>> Play(const std::vector<Voice>& voices, bool capture_x,
                                            std::size_t frames,
                                            std::vector<std::int16_t>* captured = nullptr,
                                            alist::Result* last = nullptr) {
    Reset();
    std::vector<std::vector<std::int16_t>> outputs;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const bool init = frame == 0;
        std::vector<std::uint32_t> list = Frame(voices, init);
        if (!capture_x) {
            Store(kList, list);
            Run(kList, list.size());
            outputs.push_back(Output());
            continue;
        }
        std::vector<std::uint32_t> rewritten(list.size() + 64);
        const std::uint32_t keys[] = {kX.state};
        alist::Result result;
        const bool ok = alist::rewrite(list, rewritten, keys, alist::physical(kCaptures), 0x10000,
                                       true, result);
        assert(ok);
        assert(result.output_address == alist::physical(kOutput));
        assert(result.frame_samples == kSamples);
        assert(result.chain_count == 1 && result.present[0]);
        assert(result.first_chain_init[0] == init);
        // CONTINUE: the dry (and wet) gains come from the envmixer state.
        std::int16_t saved_dry = 0;
        std::int16_t saved_wet = 0;
        if (!init) {
            saved_dry = ReadHalf(kX.state + 0x4C);
            saved_wet = ReadHalf(kX.state + 0x4E);
            Half(kX.state + 0x4C, 0);
            Half(kX.state + 0x4E, 0);
        }
        Store(kRewritten, std::vector<std::uint32_t>(rewritten.begin(), rewritten.begin() + result.words));
        Run(alist::physical(kRewritten), result.words);
        // Restore the voice's real amounts for the next frame.
        Half(kX.state + 0x4C, init ? result.last_init_dry[0] : saved_dry);
        Half(kX.state + 0x4E, init ? result.last_init_wet[0] : saved_wet);
        outputs.push_back(Output());
        if (captured != nullptr) {
            const alist::Chain& chain = result.chains[0];
            captured->clear();
            for (std::uint32_t index = 0; index < chain.samples; ++index) {
                captured->push_back(ReadHalf(kCaptures + chain.capture_offset + index * 2));
            }
        }
        if (last != nullptr) *last = result;
    }
    return outputs;
}

void captured_voice_leaves_the_mix_exactly() {
    const auto without_x = Play({kY}, false, 3);
    // Before and after the other voice in the list.
    for (const std::vector<Voice>& order : {std::vector<Voice>{kX, kY}, std::vector<Voice>{kY, kX}}) {
        const auto with_capture = Play(order, true, 3);
        for (std::size_t frame = 0; frame < 3; ++frame) {
            assert(with_capture[frame] == without_x[frame]);
        }
    }
    // And X was audible to begin with.
    const auto with_x = Play({kX, kY}, false, 3);
    assert(with_x[0] != without_x[0] && with_x[2] != without_x[2]);
}

void capture_is_what_the_envmixer_mixed() {
    std::vector<std::int16_t> captured;
    alist::Result result;
    Play({kX}, true, 1, &captured, &result);
    assert(captured.size() == kSamples);
    assert(result.chains[0].gains.dry == kX.dry && result.chains[0].gains.volume_left == kX.volume);
    const auto reference = Play({kX}, false, 1);
    // Constant volume, no ramp: left = in * volume * dry (Q15 each).
    const double gain = (kX.volume / 32768.0) * (kX.dry / 32768.0);
    double energy = 0.0;
    for (std::uint32_t index = 0; index < kSamples; ++index) {
        const double expected = captured[index] * gain;
        assert(std::fabs(reference[0][index * 2] - expected) <= 2.0);
        energy += captured[index] * static_cast<double>(captured[index]);
    }
    assert(energy > 0.0);
}

void unknown_voices_and_bad_lists_are_left_alone() {
    Reset();
    const std::vector<std::uint32_t> list = Frame({kX, kY}, true);
    std::vector<std::uint32_t> rewritten(list.size() + 64);
    const std::uint32_t none[] = {0x80555500U};
    alist::Result result;
    assert(alist::rewrite(list, rewritten, none, alist::physical(kCaptures), 0x10000, true, result));
    assert(result.chain_count == 0 && result.words == list.size());
    assert(std::equal(list.begin(), list.end(), rewritten.begin()));
    // No room for the inserted commands.
    std::vector<std::uint32_t> small(list.size());
    const std::uint32_t keys[] = {kX.state};
    assert(!alist::rewrite(list, small, keys, alist::physical(kCaptures), 0x10000, true, result));
    // No room for the capture.
    assert(!alist::rewrite(list, rewritten, keys, alist::physical(kCaptures), 16, true, result));
    // No output save: not a frame list.
    const std::vector<std::uint32_t> partial(list.begin(), list.end() - 2);
    assert(!alist::rewrite(partial, rewritten, keys, alist::physical(kCaptures), 0x10000, true, result));
}

std::array<std::int16_t, 40> State(std::uint32_t address) {
    std::array<std::int16_t, 40> state{};
    for (std::uint32_t index = 0; index < 40; ++index) state[index] = ReadHalf(address + index * 2);
    return state;
}

// Microcode envmixer output against capture * followed volume * dry, over an
// INIT frame with a ramp and the CONTINUE frame after it.
void followed_gain_matches_the_envmixer() {
    Voice ramp = kX;
    ramp.volume = 0x1000;
    ramp.target = 0x6000;
    ramp.rate = GetRate(0x1000, 0x6000, 400);
    ramp.dry = 0x6000;

    // Reference: the unedited list, keeping each frame's pre-task state.
    Reset();
    std::vector<std::vector<std::int16_t>> reference;
    std::vector<std::array<std::int16_t, 40>> before;
    for (std::size_t frame = 0; frame < 2; ++frame) {
        before.push_back(State(ramp.state));
        const std::vector<std::uint32_t> list = Frame({ramp}, frame == 0);
        Store(kList, list);
        Run(kList, list.size());
        reference.push_back(Output());
        if (frame == 0) {
            // The state layout the CONTINUE gain is read from.
            const std::array<std::int16_t, 40> after = State(ramp.state);
            assert(after[32] == ramp.target && after[35] == ramp.target);
            assert(after[33] == static_cast<std::int16_t>(ramp.rate >> 16));
            assert(after[34] == static_cast<std::int16_t>(ramp.rate & 0xFFFF));
            assert(after[38] == ramp.dry && after[39] == ramp.wet);
            assert(after[0] > ramp.volume && after[0] < ramp.target);
        }
    }

    // The same frames captured.
    for (std::size_t frame = 0; frame < 2; ++frame) {
        std::vector<std::int16_t> captured;
        alist::Result result;
        Reset();
        // Recreate the pre-task state of this frame.
        for (std::size_t run = 0; run < frame; ++run) {
            const std::vector<std::uint32_t> list = Frame({ramp}, run == 0);
            Store(kList, list);
            Run(kList, list.size());
        }
        const std::array<std::int16_t, 40> state = State(ramp.state);
        assert(state == before[frame]);
        std::vector<std::uint32_t> list = Frame({ramp}, frame == 0);
        std::vector<std::uint32_t> rewritten(list.size() + 64);
        const std::uint32_t keys[] = {ramp.state};
        assert(alist::rewrite(list, rewritten, keys, alist::physical(kCaptures), 0x10000, true, result));
        Store(kRewritten, std::vector<std::uint32_t>(rewritten.begin(), rewritten.begin() + result.words));
        Run(alist::physical(kRewritten), result.words);
        const alist::Chain& chain = result.chains[0];
        dkr::runtime::spatial_audio::EnvelopeRamp envelope =
            chain.init ? dkr::runtime::spatial_audio::EnvelopeRamp::from_init(chain.gains)
                       : dkr::runtime::spatial_audio::EnvelopeRamp::from_state(state);
        const double dry = (chain.init ? chain.gains.dry : state[38]) / 32768.0;
        double peak = 0.0;
        double worst = 0.0;
        for (std::uint32_t index = 0; index < chain.samples; ++index) {
            const double sample = ReadHalf(kCaptures + chain.capture_offset + index * 2);
            double left = 0.0;
            double right = 0.0;
            envelope.step(left, right);
            const double expected = sample * (left / 32768.0) * dry;
            worst = std::max(worst, std::fabs(reference[frame][index * 2] - expected));
            peak = std::max(peak, std::fabs(static_cast<double>(reference[frame][index * 2])));
        }
        std::printf("[test][voice-capture] frame %zu: peak %.0f, worst error %.1f\n", frame, peak, worst);
        assert(peak > 500.0);
        assert(worst <= 4.0);
    }
}

// Subframes of one frame, each a list of (voice, INIT) chains, saved one
// after another into the output as synthesizer.c does.
// Chain modes: kContinue, kStart (a new sound), kChange (a volume or pan
// change: envmixer INIT only).
enum Mode { kContinue, kStart, kChange };

std::vector<std::uint32_t> Subframes(const std::vector<std::vector<std::pair<Voice, Mode>>>& subframes) {
    std::vector<std::uint32_t> list;
    const auto add = [&](std::uint32_t w0, std::uint32_t w1) {
        list.push_back(w0);
        list.push_back(w1);
    };
    std::uint32_t output = alist::physical(kOutput);
    for (const auto& chains : subframes) {
        add(W0(7 /* SEGMENT */, 0, 0), 0);
        for (std::uint32_t buffer : {alist::kMainLeftOut, kMainR, kAuxL, kAuxR}) {
            add(W0(alist::kClearBuffer, 0, buffer), kSamples * 2);
        }
        for (const auto& [voice, mode] : chains) AppendVoice(list, voice, mode == kStart, mode == kChange);
        add(W0(alist::kSetBuffer, 0, 0), kSamples * 2);
        add(W0(alist::kInterleave, 0, 0), (alist::kMainLeftOut << 16) | kMainR);
        add(W0(alist::kSetBuffer, 0, 0), kSamples * 4);
        add(W0(alist::kSaveBuffer, 0, 0), output);
        output += kSamples * 4;
    }
    return list;
}

std::vector<std::int16_t> RunList(const std::vector<std::uint32_t>& list, bool capture_x,
                                  alist::Result* result = nullptr) {
    if (!capture_x) {
        Store(kList, list);
        Run(kList, list.size());
        return Output(kSamples * 2);
    }
    std::vector<std::uint32_t> rewritten(list.size() + 64);
    const std::uint32_t keys[] = {kX.state};
    alist::Result local;
    alist::Result& out = result != nullptr ? *result : local;
    assert(alist::rewrite(list, rewritten, keys, alist::physical(kCaptures), 0x10000, true, out));
    Store(kRewritten, std::vector<std::uint32_t>(rewritten.begin(), rewritten.begin() + out.words));
    Run(alist::physical(kRewritten), out.words);
    return Output(kSamples * 2);
}

// Two subframes: captures are placed one subframe apart and the voice stays
// out of both (its second chain continues from the zeroed state the first,
// INIT chain wrote).
void subframes_are_placed_in_order() {
    const std::vector<std::vector<std::pair<Voice, Mode>>> frame = {
        {{kX, kStart}, {kY, kStart}}, {{kX, kContinue}, {kY, kContinue}}};
    const std::vector<std::vector<std::pair<Voice, Mode>>> without = {{{kY, kStart}}, {{kY, kContinue}}};
    Reset();
    alist::Result result;
    const std::vector<std::int16_t> captured = RunList(Subframes(frame), true, &result);
    assert(result.frame_samples == kSamples * 2);
    assert(result.output_address == alist::physical(kOutput));
    assert(result.chain_count == 2);
    assert(result.chains[0].frame_offset == 0 && result.chains[0].init);
    assert(result.chains[1].frame_offset == kSamples && !result.chains[1].init);
    Reset();
    assert(captured == RunList(Subframes(without), false));
}

// A voice that continues and then starts again in one frame (stolen): the
// continuing part is the previous sound's and stays in the mix; the new
// sound is captured from its INIT.
void restarted_voice_keeps_its_previous_sound() {
    const std::vector<std::vector<std::pair<Voice, Mode>>> start = {{{kX, kStart}}};
    const std::vector<std::vector<std::pair<Voice, Mode>>> stolen = {{{kX, kContinue}}, {{kX, kStart}}};
    Reset();
    RunList(Subframes(start), false);
    const std::vector<std::int16_t> unedited = RunList(Subframes(stolen), false);
    Reset();
    RunList(Subframes(start), false);
    alist::Result result;
    const std::vector<std::int16_t> captured = RunList(Subframes(stolen), true, &result);
    assert(result.chain_count == 1 && result.chains[0].init);
    assert(result.chains[0].frame_offset == kSamples);
    assert(result.first_chain_init[0] && result.has_init[0]);
    bool previous_audible = false;
    for (std::uint32_t index = 0; index < kSamples * 2; ++index) {
        assert(captured[index] == unedited[index]);
        previous_audible = previous_audible || unedited[index] != 0;
    }
    assert(previous_audible);
    for (std::uint32_t index = kSamples * 2; index < kSamples * 4; ++index) assert(captured[index] == 0);
}

// The game changes a positioned sound's volume and pan every tick, and each
// change re-initialises its envmixer: the whole frame stays captured and out
// of the mix.
void volume_change_keeps_the_whole_frame_captured() {
    const std::vector<std::vector<std::pair<Voice, Mode>>> start = {{{kX, kStart}, {kY, kStart}}};
    const std::vector<std::vector<std::pair<Voice, Mode>>> changed = {
        {{kX, kContinue}, {kY, kContinue}}, {{kX, kChange}, {kY, kContinue}}};
    const std::vector<std::vector<std::pair<Voice, Mode>>> start_y = {{{kY, kStart}}};
    const std::vector<std::vector<std::pair<Voice, Mode>>> continue_y = {{{kY, kContinue}}, {{kY, kContinue}}};
    Reset();
    RunList(Subframes(start), true);
    // As run_audio_task does for a voice whose first chain continues.
    Half(kX.state + 0x4C, 0);
    Half(kX.state + 0x4E, 0);
    alist::Result result;
    const std::vector<std::int16_t> captured = RunList(Subframes(changed), true, &result);
    assert(result.chain_count == 2);
    assert(!result.chains[0].init && result.chains[1].init);
    assert(result.chains[0].frame_offset == 0 && result.chains[1].frame_offset == kSamples);
    Reset();
    RunList(Subframes(start_y), false);
    assert(captured == RunList(Subframes(continue_y), false));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("[test][voice-capture] SKIP: no microcode data path");
        return 77;
    }
    std::ifstream file(argv[1], std::ios::binary);
    g_ucode_data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (g_ucode_data.empty()) {
        std::printf("[test][voice-capture] SKIP: cannot read %s\n", argv[1]);
        return 77;
    }
    captured_voice_leaves_the_mix_exactly();
    capture_is_what_the_envmixer_mixed();
    unknown_voices_and_bad_lists_are_left_alone();
    followed_gain_matches_the_envmixer();
    subframes_are_placed_in_order();
    restarted_voice_keeps_its_previous_sound();
    volume_change_keeps_the_whole_frame_captured();
    std::puts("[test][voice-capture] PASS");
    return 0;
}
