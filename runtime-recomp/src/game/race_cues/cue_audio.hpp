#pragma once

#include "spatial_audio/direction.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

// The sounds of the race cues. The game thread publishes what the cues should
// say; the audio thread plays them, through the binaural renderer in the
// HEADPHONES sound mode and panned left and right otherwise (MONO included).
namespace dkr::race_cues {

using Direction = runtime::spatial_audio::Direction;

// Mono samples in the game mix's 16-bit scale.
struct Sound {
    std::vector<float> samples;
    std::uint32_t rate = 0;
};

// RIFF/WAVE 16-bit PCM, mono or stereo (mixed down).
std::optional<Sound> parse_wav(std::span<const std::uint8_t> bytes);

// What the cues say now, in the player's hearing (Mirrored Tracks already
// applied). Distances are from the kart's centre; empty means no edge in reach.
struct CueFrame {
    bool edge_active = false;
    std::optional<float> edge_left;
    std::optional<float> edge_right;
    // The HEADPHONES sound mode.
    bool headphones = false;
};

// The distance at which the edge sound is loudest; it starts at the player's
// TRACK EDGE DISTANCE (edge_range).
constexpr float kEdgeLoudest = 8.0F;

// Game thread.
void publish(const CueFrame& frame);
// Plays the turn signal (three beeps) from `direction` in the listener frame.
void signal_turn(const Direction& direction);

// The hub beacon: while active, a continuous loop from `direction` in the
// listener frame, played `pitch` times its own speed (1 far away, higher
// closer: beacon_pitch in hub_beacon.hpp).
struct BeaconFrame {
    bool active = false;
    Direction direction;
    float pitch = 1.0F;
};
void publish_beacon(const BeaconFrame& frame);

// Audio thread (or before audio starts).
void load_sounds(const std::filesystem::path& edge, const std::filesystem::path& turn,
                 const std::filesystem::path& beacon);

enum class Voice : std::size_t { EdgeLeft, EdgeRight, Turn, Beacon };
constexpr std::size_t kVoices = 4;

// Audio thread, once per output block before anything is rendered: whether
// the binaural renderer takes the cues this block.
void begin_block(bool binaural_available);
bool binaural();
// Binaural: the voice's next mono samples and its direction.
void render_voice(Voice voice, float* mono, std::size_t frames, std::uint32_t rate, Direction& direction);
// Not binaural: adds every voice, panned, to interleaved L,R frames.
void mix_panned(float* stereo, std::size_t frames, std::uint32_t rate);

} // namespace dkr::race_cues
