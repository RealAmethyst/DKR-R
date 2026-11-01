#include "race_cues/cue_audio.hpp"

#include "race_cues/cue_settings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace dkr::race_cues {
namespace {

// Levels at 100% volume, against the sounds' own peaks; set by ear.
constexpr float kEdgeLevel = 0.5F;
constexpr float kTurnLevel = 0.6F;
// 0.6 raised by 4 dB after the first listening test.
constexpr float kBeaconLevel = 0.95F;
// The beacon's pitch follows the game over about 150 ms, so it glides and
// never jumps between output blocks.
constexpr float kPitchSeconds = 0.15F;
// Three beeps, one starting every 160 ms.
constexpr int kBeeps = 3;
constexpr double kBeepPeriodSeconds = 0.16;
// The edge sound follows its distance over about 40 ms, so it never clicks.
constexpr float kGainSeconds = 0.04F;
// Samples crossfaded where the edge sound loops.
constexpr std::size_t kLoopFade = 88;
constexpr float kPi = 3.14159265F;

// Game thread to audio thread.
std::atomic<bool> g_edge_active{false};
std::atomic<float> g_edge_left{-1.0F};
std::atomic<float> g_edge_right{-1.0F};
std::atomic<bool> g_headphones{false};
std::atomic<std::uint32_t> g_turn_sequence{0};
std::atomic<float> g_turn_x{0.0F};
std::atomic<float> g_turn_y{0.0F};
std::atomic<float> g_turn_z{-1.0F};
std::atomic<bool> g_beacon_active{false};
std::atomic<float> g_beacon_x{0.0F};
std::atomic<float> g_beacon_y{0.0F};
std::atomic<float> g_beacon_z{-1.0F};
std::atomic<float> g_beacon_pitch{1.0F};

// Audio thread.
Sound g_edge;
Sound g_turn;
bool g_loaded = false;
bool g_binaural = false;
std::array<float, 2> g_edge_target{};
std::array<float, 2> g_edge_gain{};
std::array<double, 2> g_edge_position{};
std::uint32_t g_turn_seen = 0;
bool g_turn_playing = false;
double g_turn_elapsed = 0.0;
float g_turn_gain = 0.0F;
Direction g_turn_direction;
Sound g_beacon;
float g_beacon_target = 0.0F;
float g_beacon_gain = 0.0F;
float g_beacon_pitch_target = 1.0F;
float g_beacon_rate = 1.0F;
double g_beacon_position = 0.0;
Direction g_beacon_direction;
std::vector<float> g_scratch;

std::uint16_t U16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t U32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(U16(bytes, offset)) | (static_cast<std::uint32_t>(U16(bytes, offset + 2)) << 16);
}

std::optional<Sound> Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return parse_wav(bytes);
}

// The loop with its end faded into its start, so it repeats without a click.
Sound Looped(Sound sound) {
    const std::size_t length = sound.samples.size();
    if (length <= kLoopFade * 2) return sound;
    for (std::size_t index = 0; index < kLoopFade; ++index) {
        const float in = static_cast<float>(index) / static_cast<float>(kLoopFade);
        sound.samples[index] = sound.samples[index] * in + sound.samples[length - kLoopFade + index] * (1.0F - in);
    }
    sound.samples.resize(length - kLoopFade);
    return sound;
}

float SampleAt(const Sound& sound, double position) {
    const auto index = static_cast<std::size_t>(position);
    if (index + 1 >= sound.samples.size()) return index < sound.samples.size() ? sound.samples[index] : 0.0F;
    const auto fraction = static_cast<float>(position - static_cast<double>(index));
    return sound.samples[index] + (sound.samples[index + 1] - sound.samples[index]) * fraction;
}

float Closeness(float distance, float range) {
    return 1.0F - std::clamp((distance - kEdgeLoudest) / (range - kEdgeLoudest), 0.0F, 1.0F);
}

void RenderEdge(std::size_t side, float* mono, std::size_t frames, std::uint32_t rate) {
    const float follow = 1.0F - std::exp(-1.0F / (kGainSeconds * static_cast<float>(rate)));
    const double step = static_cast<double>(g_edge.rate) / static_cast<double>(rate);
    const auto length = static_cast<double>(g_edge.samples.size());
    for (std::size_t index = 0; index < frames; ++index) {
        g_edge_gain[side] += (g_edge_target[side] - g_edge_gain[side]) * follow;
        mono[index] = length > 0.0 ? SampleAt(g_edge, g_edge_position[side]) * g_edge_gain[side] : 0.0F;
        g_edge_position[side] += step;
        if (g_edge_position[side] >= length) g_edge_position[side] -= length;
    }
}

void RenderTurn(float* mono, std::size_t frames, std::uint32_t rate) {
    const double beep_seconds = g_turn.rate != 0 ? static_cast<double>(g_turn.samples.size()) / g_turn.rate : 0.0;
    const double end = (kBeeps - 1) * kBeepPeriodSeconds + beep_seconds;
    for (std::size_t index = 0; index < frames; ++index) {
        float sample = 0.0F;
        if (g_turn_playing) {
            for (int beep = 0; beep < kBeeps; ++beep) {
                const double into = g_turn_elapsed - beep * kBeepPeriodSeconds;
                if (into >= 0.0 && into < beep_seconds) sample += SampleAt(g_turn, into * g_turn.rate);
            }
            g_turn_elapsed += 1.0 / rate;
            if (g_turn_elapsed >= end) g_turn_playing = false;
        }
        mono[index] = sample * g_turn_gain;
    }
}

// The beacon's loop, its volume and pitch gliding to their targets; it
// fades in and out like the edge sound, so starting and stopping never click.
void RenderBeacon(float* mono, std::size_t frames, std::uint32_t rate) {
    const float follow = 1.0F - std::exp(-1.0F / (kGainSeconds * static_cast<float>(rate)));
    const float glide = 1.0F - std::exp(-1.0F / (kPitchSeconds * static_cast<float>(rate)));
    const double step = static_cast<double>(g_beacon.rate) / static_cast<double>(rate);
    const auto length = static_cast<double>(g_beacon.samples.size());
    for (std::size_t index = 0; index < frames; ++index) {
        g_beacon_gain += (g_beacon_target - g_beacon_gain) * follow;
        g_beacon_rate += (g_beacon_pitch_target - g_beacon_rate) * glide;
        mono[index] = length > 0.0 ? SampleAt(g_beacon, g_beacon_position) * g_beacon_gain : 0.0F;
        g_beacon_position += step * g_beacon_rate;
        if (g_beacon_position >= length) g_beacon_position -= length;
    }
}

} // namespace

std::optional<Sound> parse_wav(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12 || U32(bytes, 0) != 0x46464952U || U32(bytes, 8) != 0x45564157U) return std::nullopt;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t bits = 0;
    bool format_found = false;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const std::uint32_t id = U32(bytes, offset);
        const std::uint32_t size = U32(bytes, offset + 4);
        const std::size_t body = offset + 8;
        if (size > bytes.size() - body) return std::nullopt;
        if (id == 0x20746D66U && size >= 16) { // "fmt "
            if (U16(bytes, body) != 1) return std::nullopt; // PCM only
            channels = U16(bytes, body + 2);
            rate = U32(bytes, body + 4);
            bits = U16(bytes, body + 14);
            format_found = true;
        } else if (id == 0x61746164U) { // "data"
            if (!format_found || bits != 16 || (channels != 1 && channels != 2) || rate == 0) return std::nullopt;
            Sound sound;
            sound.rate = rate;
            const std::size_t frames = size / (2U * channels);
            sound.samples.resize(frames);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                float sum = 0.0F;
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    sum += static_cast<float>(static_cast<std::int16_t>(U16(bytes, body + (frame * channels + channel) * 2)));
                }
                sound.samples[frame] = sum / static_cast<float>(channels);
            }
            return sound;
        }
        offset = body + size + (size & 1U);
    }
    return std::nullopt;
}

void publish(const CueFrame& frame) {
    g_edge_left.store(frame.edge_left.value_or(-1.0F), std::memory_order_relaxed);
    g_edge_right.store(frame.edge_right.value_or(-1.0F), std::memory_order_relaxed);
    g_headphones.store(frame.headphones, std::memory_order_relaxed);
    g_edge_active.store(frame.edge_active, std::memory_order_release);
}

void signal_turn(const Direction& direction) {
    g_turn_x.store(direction.x, std::memory_order_relaxed);
    g_turn_y.store(direction.y, std::memory_order_relaxed);
    g_turn_z.store(direction.z, std::memory_order_relaxed);
    g_turn_sequence.fetch_add(1, std::memory_order_release);
}

void publish_beacon(const BeaconFrame& frame) {
    g_beacon_x.store(frame.direction.x, std::memory_order_relaxed);
    g_beacon_y.store(frame.direction.y, std::memory_order_relaxed);
    g_beacon_z.store(frame.direction.z, std::memory_order_relaxed);
    g_beacon_pitch.store(frame.pitch, std::memory_order_relaxed);
    g_beacon_active.store(frame.active, std::memory_order_release);
}

void load_sounds(const std::filesystem::path& edge, const std::filesystem::path& turn,
                 const std::filesystem::path& beacon) {
    if (g_loaded) return;
    g_loaded = true;
    if (auto sound = Load(edge)) {
        g_edge = Looped(std::move(*sound));
        // The right side starts half way through, so the two sides never
        // merge into one sound in the middle.
        g_edge_position[1] = static_cast<double>(g_edge.samples.size()) / 2.0;
    } else {
        std::fprintf(stderr, "[boot][race-cues] track edge sound unavailable: %s\n", edge.string().c_str());
    }
    if (auto sound = Load(turn)) {
        g_turn = std::move(*sound);
    } else {
        std::fprintf(stderr, "[boot][race-cues] turn signal sound unavailable: %s\n", turn.string().c_str());
    }
    if (auto sound = Load(beacon)) {
        g_beacon = Looped(std::move(*sound));
    } else {
        std::fprintf(stderr, "[boot][race-cues] beacon sound unavailable: %s\n", beacon.string().c_str());
    }
}

void begin_block(bool binaural_available) {
    const CueSettings current = settings();
    const bool active = g_edge_active.load(std::memory_order_acquire);
    const std::array<float, 2> distances{g_edge_left.load(std::memory_order_relaxed),
                                         g_edge_right.load(std::memory_order_relaxed)};
    const float level = kEdgeLevel * static_cast<float>(current.edge_volume) / 100.0F;
    for (std::size_t side = 0; side < 2; ++side) {
        const bool sounding = current.edge_enabled && active && distances[side] >= 0.0F;
        g_edge_target[side] = sounding ? level * Closeness(distances[side], edge_range(current.edge_distance)) : 0.0F;
    }
    const std::uint32_t sequence = g_turn_sequence.load(std::memory_order_acquire);
    if (sequence != g_turn_seen) {
        g_turn_seen = sequence;
        if (current.turn_enabled && !g_turn.samples.empty()) {
            g_turn_direction = {g_turn_x.load(std::memory_order_relaxed), g_turn_y.load(std::memory_order_relaxed),
                                g_turn_z.load(std::memory_order_relaxed)};
            g_turn_gain = kTurnLevel * static_cast<float>(current.turn_volume) / 100.0F;
            g_turn_elapsed = 0.0;
            g_turn_playing = true;
        }
    }
    const bool beacon_active = g_beacon_active.load(std::memory_order_acquire);
    g_beacon_target =
        current.beacon_enabled && beacon_active ? kBeaconLevel * static_cast<float>(current.beacon_volume) / 100.0F
                                                : 0.0F;
    // Fading out keeps its last pitch and direction. While it is silent the
    // loop is not heard, so a new guidance starts at its own pitch instead of
    // gliding up to it.
    if (beacon_active) {
        g_beacon_pitch_target = std::clamp(g_beacon_pitch.load(std::memory_order_relaxed), 0.25F, 4.0F);
        if (g_beacon_gain < 1e-4F) g_beacon_rate = g_beacon_pitch_target;
        g_beacon_direction = {g_beacon_x.load(std::memory_order_relaxed), g_beacon_y.load(std::memory_order_relaxed),
                              g_beacon_z.load(std::memory_order_relaxed)};
    }
    g_binaural = binaural_available && g_headphones.load(std::memory_order_relaxed);
}

bool binaural() {
    return g_binaural;
}

void render_voice(Voice voice, float* mono, std::size_t frames, std::uint32_t rate, Direction& direction) {
    switch (voice) {
        case Voice::EdgeLeft:
            direction = {-1.0F, 0.0F, 0.0F};
            RenderEdge(0, mono, frames, rate);
            break;
        case Voice::EdgeRight:
            direction = {1.0F, 0.0F, 0.0F};
            RenderEdge(1, mono, frames, rate);
            break;
        case Voice::Turn:
            direction = g_turn_direction;
            RenderTurn(mono, frames, rate);
            break;
        case Voice::Beacon:
            direction = g_beacon_direction;
            RenderBeacon(mono, frames, rate);
            break;
    }
}

void mix_panned(float* stereo, std::size_t frames, std::uint32_t rate) {
    if (g_scratch.size() < frames) g_scratch.resize(frames);
    for (std::size_t index = 0; index < kVoices; ++index) {
        Direction direction;
        render_voice(static_cast<Voice>(index), g_scratch.data(), frames, rate, direction);
        // Equal-power pan from the direction's left-right part.
        const float across = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        const float x = across > 0.0F ? direction.x / across : 0.0F;
        const float angle = (x + 1.0F) * kPi / 4.0F;
        const float left = std::cos(angle);
        const float right = std::sin(angle);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            stereo[frame * 2] += g_scratch[frame] * left;
            stereo[frame * 2 + 1] += g_scratch[frame] * right;
        }
    }
}

} // namespace dkr::race_cues
