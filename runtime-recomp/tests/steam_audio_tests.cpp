#include <phonon.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr IPLint32 kSampleRate = 48000;
constexpr IPLint32 kFrameSize = 256;
constexpr int kFrames = 40;
constexpr double kPi = 3.14159265358979323846;

struct Energy {
    double left = 0.0;
    double right = 0.0;
};

struct Renderer {
    IPLContext context = nullptr;
    IPLHRTF hrtf = nullptr;
    IPLAudioSettings audio{kSampleRate, kFrameSize};

    Renderer() {
        IPLContextSettings context_settings{};
        context_settings.version = STEAMAUDIO_VERSION;
        [[maybe_unused]] const IPLerror context_status = iplContextCreate(&context_settings, &context);
        assert(context_status == IPL_STATUS_SUCCESS);

        // A zeroed volume renders silence, so it is set explicitly.
        IPLHRTFSettings hrtf_settings{};
        hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
        hrtf_settings.volume = 1.0F;
        [[maybe_unused]] const IPLerror hrtf_status = iplHRTFCreate(context, &audio, &hrtf_settings, &hrtf);
        assert(hrtf_status == IPL_STATUS_SUCCESS);
    }

    ~Renderer() {
        iplHRTFRelease(&hrtf);
        iplContextRelease(&context);
    }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Renders a mono tone from one direction in Steam Audio's listener frame
    // (+x right, +y up, -z ahead) and measures each ear.
    Energy render(IPLVector3 direction) {
        IPLBinauralEffectSettings effect_settings{hrtf};
        IPLBinauralEffect effect = nullptr;
        [[maybe_unused]] const IPLerror effect_status = iplBinauralEffectCreate(context, &audio, &effect_settings, &effect);
        assert(effect_status == IPL_STATUS_SUCCESS);

        IPLAudioBuffer input{};
        IPLAudioBuffer output{};
        [[maybe_unused]] const IPLerror input_status = iplAudioBufferAllocate(context, 1, kFrameSize, &input);
        assert(input_status == IPL_STATUS_SUCCESS);
        [[maybe_unused]] const IPLerror output_status = iplAudioBufferAllocate(context, 2, kFrameSize, &output);
        assert(output_status == IPL_STATUS_SUCCESS);

        IPLBinauralEffectParams params{};
        params.direction = direction;
        params.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
        params.spatialBlend = 1.0F;
        params.hrtf = hrtf;

        Energy energy;
        long sample_index = 0;
        for (int frame = 0; frame < kFrames; ++frame) {
            for (IPLint32 index = 0; index < kFrameSize; ++index, ++sample_index) {
                input.data[0][index] = 0.5F * static_cast<float>(std::sin(2.0 * kPi * 440.0 * sample_index / kSampleRate));
            }
            iplBinauralEffectApply(effect, &params, &input, &output);
            for (IPLint32 index = 0; index < kFrameSize; ++index) {
                energy.left += output.data[0][index] * output.data[0][index];
                energy.right += output.data[1][index] * output.data[1][index];
            }
        }

        iplAudioBufferFree(context, &output);
        iplAudioBufferFree(context, &input);
        iplBinauralEffectRelease(&effect);
        return energy;
    }
};

void default_hrtf_places_sound_on_the_matching_side() {
    Renderer renderer;
    const Energy right = renderer.render(IPLVector3{1.0F, 0.0F, 0.0F});
    const Energy left = renderer.render(IPLVector3{-1.0F, 0.0F, 0.0F});
    assert(right.left > 0.0 && right.right > 0.0);
    assert(right.right > 2.0 * right.left);
    assert(left.left > 2.0 * left.right);
}

void sound_ahead_reaches_both_ears_evenly() {
    Renderer renderer;
    const Energy ahead = renderer.render(IPLVector3{0.0F, 0.0F, -1.0F});
    assert(ahead.left > 0.0 && ahead.right > 0.0);
    const double ratio = ahead.left / ahead.right;
    assert(ratio > 0.5 && ratio < 2.0);
}

} // namespace

int main() {
    default_hrtf_places_sound_on_the_matching_side();
    sound_ahead_reaches_both_ears_evenly();
    std::puts("[test][steam-audio] PASS");
    return 0;
}
