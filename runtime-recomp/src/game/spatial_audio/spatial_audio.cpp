#include "spatial_audio/spatial_audio.hpp"

#include "spatial_audio/binaural_renderer.hpp"
#include "spatial_audio/test_tone.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>

namespace dkr::runtime::spatial_audio {
namespace {

Renderer g_renderer;
std::optional<TestTone> g_test_tone;

bool TestToneRequested() {
    const char* value = std::getenv("DKR_SPATIAL_AUDIO_TEST_TONE");
    return value != nullptr && value[0] == '1';
}

} // namespace

void start(std::uint32_t output_rate) {
    if (!g_renderer.start(output_rate)) return;
    if (!TestToneRequested() || (g_test_tone && g_renderer.playing(*g_test_tone))) return;
    g_test_tone.emplace(g_renderer.processing_rate());
    if (g_renderer.play(*g_test_tone)) {
        std::fprintf(stderr, "[boot][spatial-audio] test tone: left, right, ahead, behind\n");
    }
}

void stop() {
    g_renderer.stop();
}

bool active() {
    return g_renderer.active();
}

void mix_into(float* stereo, std::size_t frames) {
    g_renderer.mix_into(stereo, frames);
}

} // namespace dkr::runtime::spatial_audio
