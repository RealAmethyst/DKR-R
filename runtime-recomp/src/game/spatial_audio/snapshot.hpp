#pragma once

#include "spatial_audio/direction.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::spatial_audio {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

// The camera the game's own sound panning uses: gCameras[0], or gCameras[4]
// while a cutscene camera is active. World axes: +Y up; yaw 0 faces +Z and
// increasing yaw turns right.
struct Listener {
    Vec3 position;
    std::int16_t yaw = 0;
    // Mirrored Tracks flips the screen (not the world); the game flips its pan.
    bool mirrored = false;
};

enum class EmitterKind : std::uint8_t { Point, Line, AiEngine, PlayerEngine };

// A positioned sound the game is playing.
struct Emitter {
    EmitterKind kind = EmitterKind::Point;
    // Guest address of the AudioPoint, AudioLine or VehicleSoundData.
    std::uint32_t owner = 0;
    // World position; for a line, its point closest to the listener.
    Vec3 position;
    // Guest address of the sound's ALSoundState, and of the emitter's field
    // holding that handle (the state's userHandle points back at it).
    std::uint32_t sound_state = 0;
    std::uint32_t handle_field = 0;
    // The voice's ENVMIX_STATE address, which its A_ENVMIXER command carries;
    // 0 while the sound has no voice yet.
    std::uint32_t envmixer_state = 0;
    // ALSoundState volume (the last volume event, volume << 8) and pan.
    std::int16_t volume = 0;
    std::uint8_t pan = 64;
};

// 40 audio points, 7 audio lines, 2 AI engines and up to 4 players' three
// engine sounds.
constexpr std::size_t kMaxEmitters = 40 + 7 + 2 + 12;

// Read from the game once per tick.
struct Snapshot {
    bool valid = false;
    std::uint64_t tick = 0;
    bool headphones = false;
    bool online = false;
    std::uint8_t cameras = 0;
    Listener listener;
    bool has_player_kart = false;
    Vec3 player_kart;
    std::size_t emitter_count = 0;
    std::array<Emitter, kMaxEmitters> emitters{};

    // Binaural rendering applies only in the HEADPHONES sound mode with one
    // camera (the game centres positioned sounds in split screen), offline.
    bool spatial_allowed() const { return valid && headphones && cameras == 1 && !online; }
};

// The direction of a world position from the listener, in the renderer's
// listener frame (+x right, +y up, -z ahead), unnormalised.
Direction listener_direction(const Listener& listener, const Vec3& position);

// The point of segment a-b closest to `point`.
Vec3 closest_point_on_segment(const Vec3& point, const Vec3& a, const Vec3& b);

// Hands the latest snapshot from the game thread to the audio thread without
// locking: one writer, one reader, three buffers.
class SnapshotChannel {
public:
    void publish(const Snapshot& snapshot);
    // Copies the newest published snapshot; false if none was ever published.
    bool latest(Snapshot& out);

private:
    static constexpr std::uint8_t kFresh = 4;
    std::array<Snapshot, 3> buffers_{};
    std::uint8_t write_ = 0;
    std::uint8_t read_ = 1;
    // Index of the buffer between writer and reader, with kFresh set when
    // the writer has filled it since the reader last took it.
    std::atomic<std::uint8_t> middle_{2};
    bool published_ = false;
};

} // namespace dkr::runtime::spatial_audio
