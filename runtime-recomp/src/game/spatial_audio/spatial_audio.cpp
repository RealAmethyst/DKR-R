#include "spatial_audio/spatial_audio.hpp"

#include "race_cues/cue_audio.hpp"
#include "spatial_audio/binaural_renderer.hpp"
#include "spatial_audio/snapshot.hpp"
#include "spatial_audio/test_tone.hpp"
#include "spatial_audio/trace.hpp"
#include "spatial_audio/voice_capture.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>

namespace dkr::runtime::spatial_audio {
namespace {

// A race cue voice, rendered binaurally while the cues are in HEADPHONES.
class CueSource final : public Source {
public:
    explicit CueSource(race_cues::Voice voice) : voice_(voice) {}
    void set_rate(std::uint32_t rate) { rate_ = rate; }
    bool render(float* mono, std::size_t frames, Direction& direction) override {
        if (!race_cues::binaural()) return false;
        race_cues::render_voice(voice_, mono, frames, rate_, direction);
        return true;
    }

private:
    race_cues::Voice voice_;
    std::uint32_t rate_ = 0;
};

// Audio thread (platform mutex held). Twice the voices one frame captures,
// so voices leaving (still draining their tails) never keep a new voice
// from starting; the renderer has room for those, the test tone and the
// race cues.
constexpr std::size_t kSources = alist::kMaxVoices * 2;
static_assert(kSources + 1 + race_cues::kVoices <= Renderer::kVoiceCount);
Renderer g_renderer;
std::optional<TestTone> g_test_tone;
std::array<CueSource, race_cues::kVoices> g_cue_sources{
    CueSource{race_cues::Voice::EdgeLeft}, CueSource{race_cues::Voice::EdgeRight},
    CueSource{race_cues::Voice::Turn}, CueSource{race_cues::Voice::Beacon}};
std::array<CaptureSource, kSources> g_sources;
FrameCapture g_current;

// Game thread to SP task thread. A snapshot the game stopped refreshing
// (its main loop not running) is not used after this many audio frames.
constexpr std::uint32_t kStaleAudioFrames = 8;
SnapshotChannel g_snapshots;
Snapshot g_plan_snapshot;
std::uint64_t g_plan_tick = 0;
std::uint32_t g_plan_unchanged = 0;

// SP task thread to audio thread: frames waiting for their output buffer.
constexpr std::size_t kPending = 4;
// Unmatched frames in a row after which capturing stops: the game's mix would
// otherwise lose those voices with nothing rendered in their place.
constexpr std::uint32_t kUnmatchedLimit = 8;
std::mutex g_pending_mutex;
std::array<FrameCapture, kPending> g_pending;
std::size_t g_pending_first = 0;
std::size_t g_pending_count = 0;
std::uint32_t g_unmatched = 0;
std::atomic<const std::uint8_t*> g_rdram{nullptr};
std::atomic<bool> g_renderer_ready{false};
std::atomic<bool> g_capture_stopped{false};

void CopyFrame(const FrameCapture& from, FrameCapture& to) {
    to.output_address = from.output_address;
    to.frame_samples = from.frame_samples;
    to.voice_count = from.voice_count;
    for (std::size_t voice = 0; voice < from.voice_count; ++voice) {
        to.voices[voice].key = from.voices[voice].key;
        to.voices[voice].direction = from.voices[voice].direction;
        std::copy_n(from.voices[voice].mono.begin(), from.frame_samples, to.voices[voice].mono.begin());
    }
}

void ClearPending() {
    std::scoped_lock lock(g_pending_mutex);
    g_pending_first = 0;
    g_pending_count = 0;
    g_unmatched = 0;
}

// The newest frame for this buffer: output buffers rotate, so an older entry
// with the same address is a frame whose output was never queued.
bool TakeFrame(std::uint32_t output_address, FrameCapture& out) {
    std::scoped_lock lock(g_pending_mutex);
    for (std::size_t index = g_pending_count; index-- > 0;) {
        const std::size_t slot = (g_pending_first + index) % kPending;
        if (g_pending[slot].output_address != output_address) continue;
        CopyFrame(g_pending[slot], out);
        // Older frames were never matched; drop them with this one.
        g_pending_first = (slot + 1) % kPending;
        g_pending_count -= index + 1;
        g_unmatched = 0;
        return true;
    }
    return false;
}

void Finish(CaptureSource& source) {
    if (!source.finishing()) trace::count(trace::counters().sources_finished);
    source.finish();
}

void FinishAllSources() {
    for (CaptureSource& source : g_sources) {
        if (g_renderer.playing(source)) Finish(source);
    }
}

void Dispatch(const FrameCapture& frame) {
    std::array<bool, kSources> fed{};
    for (std::size_t voice = 0; voice < frame.voice_count; ++voice) {
        const CapturedVoice& captured = frame.voices[voice];
        CaptureSource* target = nullptr;
        for (std::size_t index = 0; index < g_sources.size(); ++index) {
            CaptureSource& source = g_sources[index];
            if (g_renderer.playing(source) && !source.finishing() && source.key() == captured.key) {
                target = &source;
                fed[index] = true;
                break;
            }
        }
        if (target == nullptr) {
            for (std::size_t index = 0; index < g_sources.size(); ++index) {
                CaptureSource& source = g_sources[index];
                if (g_renderer.playing(source)) continue;
                source.begin(captured.key);
                if (!g_renderer.play(source)) break;
                trace::count(trace::counters().sources_begun);
                target = &source;
                fed[index] = true;
                break;
            }
        }
        if (target != nullptr) target->push(captured.mono.data(), frame.frame_samples, captured.direction);
    }
    for (std::size_t index = 0; index < g_sources.size(); ++index) {
        if (!fed[index] && g_renderer.playing(g_sources[index])) Finish(g_sources[index]);
    }
}

// DKR_SPATIAL_AUDIO_TEST_TONE=1 plays the renderer's listening test.
bool TestToneRequested() {
    const char* value = std::getenv("DKR_SPATIAL_AUDIO_TEST_TONE");
    return value != nullptr && value[0] == '1';
}

} // namespace

void start(std::uint32_t output_rate) {
    ClearPending();
    g_capture_stopped.store(false, std::memory_order_release);
    if (!g_renderer.start(output_rate)) {
        g_renderer_ready.store(false, std::memory_order_release);
        return;
    }
    g_renderer_ready.store(true, std::memory_order_release);
    if (!TestToneRequested() || (g_test_tone && g_renderer.playing(*g_test_tone))) return;
    g_test_tone.emplace(g_renderer.processing_rate());
    if (g_renderer.play(*g_test_tone)) {
        std::fprintf(stderr, "[boot][spatial-audio] test tone: left, right, ahead, behind\n");
    }
}

void stop() {
    g_renderer_ready.store(false, std::memory_order_release);
    g_renderer.stop();
    ClearPending();
}

bool active() {
    return g_renderer.active();
}

bool binaural_available() {
    return g_renderer.running();
}

void begin_output_block(const void* samples) {
    if (g_renderer.running() && race_cues::binaural()) {
        for (CueSource& source : g_cue_sources) {
            if (g_renderer.playing(source)) continue;
            source.set_rate(g_renderer.processing_rate());
            g_renderer.play(source);
        }
    }
    const std::uint8_t* rdram = g_rdram.load(std::memory_order_acquire);
    if (rdram == nullptr || !g_renderer.running()) return;
    const auto* bytes = static_cast<const std::uint8_t*>(samples);
    if (bytes < rdram || bytes >= rdram + 0x800000) {
        FinishAllSources();
        return;
    }
    const auto address = static_cast<std::uint32_t>(bytes - rdram);
    trace::Counters& counters = trace::counters();
    trace::count(counters.blocks);
    if (TakeFrame(address, g_current)) {
        trace::count(counters.matched);
        Dispatch(g_current);
    } else {
        trace::count(counters.unmatched);
        FinishAllSources();
    }
    // About two seconds of 30 Hz frames.
    trace::report(60);
}

void mix_into(float* stereo, std::size_t frames) {
    g_renderer.mix_into(stereo, frames);
}

void publish_snapshot(const Snapshot& snapshot) {
    g_snapshots.publish(snapshot);
}

bool capture_plan(CapturePlan& plan) {
    plan.count = 0;
    if (!g_renderer_ready.load(std::memory_order_acquire) ||
        g_capture_stopped.load(std::memory_order_acquire) || !g_snapshots.latest(g_plan_snapshot) ||
        !g_plan_snapshot.spatial_allowed()) {
        return false;
    }
    if (g_plan_snapshot.tick != g_plan_tick) {
        g_plan_tick = g_plan_snapshot.tick;
        g_plan_unchanged = 0;
    } else if (++g_plan_unchanged > kStaleAudioFrames) {
        trace::count(trace::counters().no_plan_stale);
        return false;
    }
    // Positioned sounds, nearest first, each voice once. The player's own
    // engine stays centred as the game plays it.
    std::array<std::pair<float, std::size_t>, kMaxEmitters> candidates{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < g_plan_snapshot.emitter_count; ++index) {
        const Emitter& emitter = g_plan_snapshot.emitters[index];
        if (emitter.kind == EmitterKind::PlayerEngine || emitter.envmixer_state == 0) continue;
        const Direction direction = listener_direction(g_plan_snapshot.listener, emitter.position);
        const float distance =
            direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
        candidates[count++] = {distance, index};
    }
    std::sort(candidates.begin(), candidates.begin() + count);
    for (std::size_t candidate = 0; candidate < count && plan.count < plan.keys.size(); ++candidate) {
        const Emitter& emitter = g_plan_snapshot.emitters[candidates[candidate].second];
        const std::uint32_t key = alist::physical(emitter.envmixer_state);
        if (std::find(plan.keys.begin(), plan.keys.begin() + plan.count, key) !=
            plan.keys.begin() + plan.count) {
            continue;
        }
        plan.keys[plan.count] = key;
        plan.directions[plan.count] = listener_direction(g_plan_snapshot.listener, emitter.position);
        plan.sound_states[plan.count] = emitter.sound_state;
        plan.handle_fields[plan.count] = emitter.handle_field;
        ++plan.count;
    }
    return plan.count != 0;
}

void submit_capture(const FrameCapture& capture, const std::uint8_t* rdram) {
    g_rdram.store(rdram, std::memory_order_release);
    std::scoped_lock lock(g_pending_mutex);
    if (g_pending_count == kPending) {
        g_pending_first = (g_pending_first + 1) % kPending;
        --g_pending_count;
        if (++g_unmatched >= kUnmatchedLimit && !g_capture_stopped.exchange(true)) {
            std::fprintf(stderr,
                         "[boot][spatial-audio] voice capture off: captured frames are not "
                         "reaching the output\n");
        }
    }
    CopyFrame(capture, g_pending[(g_pending_first + g_pending_count) % kPending]);
    ++g_pending_count;
}

} // namespace dkr::runtime::spatial_audio
