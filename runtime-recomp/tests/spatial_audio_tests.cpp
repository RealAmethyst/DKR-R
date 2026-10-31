#include "spatial_audio/binaural_renderer.hpp"
#include "spatial_audio/half_band_decimator.hpp"
#include "spatial_audio/test_tone.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using dkr::runtime::spatial_audio::Direction;
using dkr::runtime::spatial_audio::HalfBandDecimator;
using dkr::runtime::spatial_audio::Renderer;
using dkr::runtime::spatial_audio::Source;
using dkr::runtime::spatial_audio::TestTone;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Energy {
    double left = 0.0;
    double right = 0.0;
};

Energy Measure(const std::vector<float>& stereo, std::size_t first_frame = 0) {
    Energy energy;
    for (std::size_t frame = first_frame; frame < stereo.size() / 2; ++frame) {
        energy.left += stereo[frame * 2] * stereo[frame * 2];
        energy.right += stereo[frame * 2 + 1] * stereo[frame * 2 + 1];
    }
    return energy;
}

// Level of a sine through the decimator relative to its input, in dB.
double DecimatedGainDb(double frequency, double input_rate) {
    HalfBandDecimator decimator;
    decimator.reset();
    constexpr std::size_t kOutputFrames = 4096;
    std::vector<float> input(kOutputFrames * 4);
    for (std::size_t frame = 0; frame < kOutputFrames * 2; ++frame) {
        const float sample = static_cast<float>(std::sin(2.0 * kPi * frequency * frame / input_rate));
        input[frame * 2] = sample;
        input[frame * 2 + 1] = sample;
    }
    std::vector<float> output(kOutputFrames * 2);
    decimator.process(input.data(), output.data(), kOutputFrames);
    // Skip the filter's start-up.
    const Energy out = Measure(output, HalfBandDecimator::kTaps);
    const double out_rms = std::sqrt(out.left / (kOutputFrames - HalfBandDecimator::kTaps));
    return 20.0 * std::log10(out_rms / std::sqrt(0.5));
}

void decimator_keeps_the_audible_band_and_removes_what_would_alias() {
    assert(std::fabs(DecimatedGainDb(1000.0, 44100.0)) < 0.1);
    assert(std::fabs(DecimatedGainDb(8000.0, 44100.0)) < 0.5);
    // Above 13 kHz the input would fold back below the 11,025 Hz Nyquist.
    assert(DecimatedGainDb(13500.0, 44100.0) < -60.0);
    assert(DecimatedGainDb(18000.0, 44100.0) < -70.0);
}

// A steady tone from one direction, ending after a set number of frames.
class ToneSource final : public Source {
public:
    ToneSource(Direction direction, std::uint32_t rate, std::size_t frames)
        : direction_(direction), rate_(rate), remaining_(frames) {}
    bool render(float* mono, std::size_t frames, Direction& direction) override {
        if (remaining_ == 0) return false;
        direction = direction_;
        for (std::size_t index = 0; index < frames; ++index, ++phase_) {
            mono[index] = index < remaining_
                ? 8000.0F * static_cast<float>(std::sin(2.0 * kPi * 500.0 * phase_ / rate_))
                : 0.0F;
        }
        remaining_ = frames < remaining_ ? remaining_ - frames : 0;
        return true;
    }

private:
    Direction direction_;
    std::uint32_t rate_;
    std::size_t remaining_;
    std::size_t phase_ = 0;
};

// Mixes in uneven block sizes like the game's 720..848-frame mixes.
std::vector<float> MixBlocks(Renderer& renderer, std::size_t blocks) {
    constexpr std::size_t kSizes[] = {848, 720, 736, 752};
    std::vector<float> all;
    for (std::size_t block = 0; block < blocks; ++block) {
        std::vector<float> stereo(kSizes[block % 4] * 2, 0.0F);
        renderer.mix_into(stereo.data(), stereo.size() / 2);
        all.insert(all.end(), stereo.begin(), stereo.end());
    }
    return all;
}

void unsupported_rate_stays_off_and_mixes_nothing() {
    Renderer renderer;
    assert(!renderer.start(11025));
    assert(!renderer.running());
    ToneSource source({1.0F, 0.0F, 0.0F}, 11025, 1000);
    assert(!renderer.play(source));
    std::vector<float> stereo(512, 0.25F);
    renderer.mix_into(stereo.data(), 256);
    for (float sample : stereo) assert(sample == 0.25F);
}

void silent_until_a_source_plays() {
    Renderer renderer;
    assert(renderer.start(22050));
    assert(renderer.processing_rate() == 44100);
    assert(!renderer.active());
    std::vector<float> stereo(1472, 0.25F);
    renderer.mix_into(stereo.data(), 736);
    for (float sample : stereo) assert(sample == 0.25F);
}

void game_rate_source_is_placed_and_drains_after_it_ends() {
    Renderer renderer;
    const auto started = std::chrono::steady_clock::now();
    assert(renderer.start(22050));
    const auto start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::printf("[test][spatial-audio] start took %lld ms\n", static_cast<long long>(start_ms));

    ToneSource right({1.0F, 0.0F, 0.0F}, renderer.processing_rate(), 44100 / 2);
    assert(renderer.play(right));
    assert(renderer.playing(right));
    assert(!renderer.play(right));
    const std::vector<float> heard = MixBlocks(renderer, 20);
    const Energy energy = Measure(heard);
    assert(energy.right > 0.0);
    assert(energy.right > 2.0 * energy.left);

    // Half a second of source at 44.1 kHz is 11,025 output frames; the
    // twenty blocks above are longer, so the voice has ended and drained.
    // What remains of the last rendered block plays out with the next mix.
    assert(!renderer.playing(right));
    MixBlocks(renderer, 1);
    assert(!renderer.active());
    const Energy after = Measure(MixBlocks(renderer, 4));
    assert(after.left == 0.0 && after.right == 0.0);

    // The freed voice can be used again, here from the left.
    ToneSource left({-1.0F, 0.0F, 0.0F}, renderer.processing_rate(), 4410);
    assert(renderer.play(left));
    const Energy left_energy = Measure(MixBlocks(renderer, 10));
    assert(left_energy.left > 2.0 * left_energy.right);
}

void sixteen_voices_then_no_more() {
    Renderer renderer;
    assert(renderer.start(48000));
    assert(renderer.processing_rate() == 48000);
    std::vector<ToneSource> sources;
    sources.reserve(Renderer::kVoiceCount + 1);
    for (std::size_t index = 0; index <= Renderer::kVoiceCount; ++index) {
        sources.emplace_back(Direction{0.0F, 0.0F, -1.0F}, 48000, 48000);
    }
    for (std::size_t index = 0; index < Renderer::kVoiceCount; ++index) {
        assert(renderer.play(sources[index]));
    }
    assert(!renderer.play(sources[Renderer::kVoiceCount]));
    renderer.stop();
    assert(!renderer.active());
}

void zero_direction_is_played_ahead() {
    Renderer renderer;
    assert(renderer.start(22050));
    ToneSource centre({0.0F, 0.0F, 0.0F}, renderer.processing_rate(), 4410);
    assert(renderer.play(centre));
    const std::vector<float> heard = MixBlocks(renderer, 8);
    for (float sample : heard) assert(std::isfinite(sample));
    const Energy energy = Measure(heard);
    assert(energy.left > 0.0);
    const double ratio = energy.left / energy.right;
    assert(ratio > 0.5 && ratio < 2.0);
}

void test_tone_cycles_left_right_ahead_behind() {
    TestTone tone(44100);
    const std::uint64_t burst = 44100 * 150 / 1000;
    const std::uint64_t gap = 44100 * 100 / 1000;
    const std::uint64_t position = 3 * (burst + gap) + 44100 * 1200 / 1000;
    assert(tone.position_at(0) == 0);
    assert(tone.position_at(position) == 1);
    assert(tone.position_at(position * 2) == 2);
    assert(tone.position_at(position * 3) == 3);
    assert(tone.position_at(position * 4) == 0);
    assert(tone.sounding_at(0) && tone.sounding_at(burst - 1));
    assert(!tone.sounding_at(burst) && !tone.sounding_at(burst + gap - 1));
    assert(tone.sounding_at(burst + gap));
    assert(!tone.sounding_at(3 * (burst + gap)) && !tone.sounding_at(position - 1));

    std::vector<float> mono(Renderer::kFrameSize);
    Direction direction;
    assert(tone.render(mono.data(), mono.size(), direction));
    assert(direction.x == -1.0F);
    double energy = 0.0;
    for (float sample : mono) energy += sample * sample;
    assert(energy > 0.0);
}

} // namespace

int main() {
    decimator_keeps_the_audible_band_and_removes_what_would_alias();
    unsupported_rate_stays_off_and_mixes_nothing();
    silent_until_a_source_plays();
    game_rate_source_is_placed_and_drains_after_it_ends();
    sixteen_voices_then_no_more();
    zero_direction_is_played_ahead();
    test_tone_cycles_left_right_ahead_behind();
    std::puts("[test][spatial-audio] PASS");
    return 0;
}
