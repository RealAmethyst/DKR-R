// Reads the game's audio listener and positioned sounds once per tick for the
// spatial audio renderer. Read-only: no guest writes and no game randomness;
// the one guest call (get_filtered_cheats) only reads. Layouts are from the
// matching decomp (camera.h, audio_spatial.h, audio_vehicle.h, audiosfx.h,
// libaudio.h, audio_internal.h) and identical in both revisions; every global
// is looked up by name.
#include "spatial_audio/game_capture.hpp"

#include "accessibility/guest_text.hpp"
#include "accessibility/symbol_lookup.hpp"
#include "game_payload.hpp"
#include "revision_addresses.hpp"
#include "runtime_netplay.hpp"
#include "spatial_audio/snapshot.hpp"
#include "spatial_audio/spatial_audio.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <optional>

namespace dkr::runtime::spatial_audio {
namespace {

using accessibility::guest_byte;
using accessibility::guest_half;
using accessibility::guest_word;
namespace symbols = accessibility::symbols;

constexpr std::uint32_t kCameraSize = 0x44;
constexpr std::uint32_t kCutsceneCameraBase = 4;
constexpr std::uint32_t kMaxAudioPoints = 40;
constexpr std::uint32_t kAudioLines = 7;
constexpr std::uint32_t kAudioLineSize = 0x180;
constexpr std::uint32_t kMaxLineSegments = 29;
constexpr std::uint32_t kSoundStates = 32;
constexpr std::uint32_t kSoundStateSize = 0x40;
constexpr std::int16_t kHeadphones = 2;
constexpr std::uint32_t kMirroredTracks = 1U << 2;

struct Globals {
    rom::Revision revision{};
    std::uint32_t cameras = 0;
    std::uint32_t num_cameras = 0;
    std::uint32_t cutscene_camera = 0;
    std::uint32_t stereo_pan_mode = 0;
    std::uint32_t sound_player = 0;
    std::uint32_t audio_points = 0;
    std::uint32_t num_audio_points = 0;
    std::uint32_t audio_lines = 0;
    std::uint32_t background_racer_sounds = 0;
    std::uint32_t racers_by_port = 0;
    std::uint32_t num_racers = 0;
};

std::uint64_t g_tick = 0;
std::uint32_t g_reported = 0;

// Each reason is logged once per run.
void Report(std::uint32_t bit, const char* reason) {
    if ((g_reported & bit) != 0) return;
    g_reported |= bit;
    std::fprintf(stderr, "[boot][spatial-audio] snapshot off: %s\n", reason);
}

std::optional<Globals> ResolveGlobals() {
    static std::optional<Globals> cached;
    const rom::Revision revision = revision_addresses::selected_revision();
    if (cached && cached->revision == revision) return cached;
    struct Want {
        std::uint32_t Globals::*field;
        const char* name;
        std::uint32_t size;
    };
    constexpr Want kWants[] = {
        {&Globals::cameras, "gCameras", kCameraSize * 8},
        {&Globals::num_cameras, "gNumCameras", 4},
        {&Globals::cutscene_camera, "gCutsceneCameraActive", 1},
        {&Globals::stereo_pan_mode, "gStereoPanMode", 2},
        {&Globals::sound_player, "gSoundPlayer", 0x44},
        {&Globals::audio_points, "gAudioPoints", 4},
        {&Globals::num_audio_points, "gNumAudioPoints", 2},
        {&Globals::audio_lines, "gAudioLines", kAudioLineSize * kAudioLines},
        {&Globals::background_racer_sounds, "gBackgroundRacerSounds", 8},
        {&Globals::racers_by_port, "gRacersByPort", 4},
        {&Globals::num_racers, "gNumRacers", 4},
    };
    Globals globals;
    globals.revision = revision;
    for (const Want& want : kWants) {
        const std::optional<std::uint32_t> address = symbols::data(want.name, want.size);
        if (!address) {
            std::fprintf(stderr, "[boot][spatial-audio] snapshot off: symbol %s not found\n", want.name);
            return std::nullopt;
        }
        globals.*want.field = *address;
    }
    cached = globals;
    return cached;
}

std::optional<float> GuestFloat(std::uint8_t* rdram, std::uint32_t address) {
    const std::optional<std::uint32_t> word = guest_word(rdram, address);
    if (!word) return std::nullopt;
    return std::bit_cast<float>(*word);
}

std::optional<Vec3> GuestVec3(std::uint8_t* rdram, std::uint32_t address) {
    const std::optional<float> x = GuestFloat(rdram, address);
    const std::optional<float> y = GuestFloat(rdram, address + 4);
    const std::optional<float> z = GuestFloat(rdram, address + 8);
    if (!x || !y || !z) return std::nullopt;
    return Vec3{*x, *y, *z};
}

// The sound behind an emitter's handle field, if it is still that emitter's:
// a handle inside the sound player's fixed state array, in use, whose
// userHandle points back at the field (sndp_deallocate clears the field, but
// a stale copy may name a reused state).
bool ReadSound(std::uint8_t* rdram, std::uint32_t states, std::uint32_t handle_field,
               Emitter& emitter) {
    const std::optional<std::uint32_t> handle = guest_word(rdram, handle_field);
    if (!handle || *handle < states || *handle >= states + kSoundStates * kSoundStateSize ||
        (*handle - states) % kSoundStateSize != 0) {
        return false;
    }
    const std::optional<std::int8_t> state = guest_byte(rdram, *handle + 0x3F);
    const std::optional<std::uint32_t> user_handle = guest_word(rdram, *handle + 0x30);
    if (!state || *state == 0 || !user_handle || *user_handle != handle_field) return false;
    const std::optional<std::int16_t> volume = guest_half(rdram, *handle + 0x34);
    const std::optional<std::int8_t> pan = guest_byte(rdram, *handle + 0x3C);
    if (!volume || !pan) return false;
    emitter.sound_state = *handle;
    emitter.handle_field = handle_field;
    emitter.volume = *volume;
    emitter.pan = static_cast<std::uint8_t>(*pan);
    emitter.envmixer_state = 0;
    // ALSoundState.voice (+0x0C) .pvoice (+0x08); the voice is allocated on
    // the audio thread after the play event, freed or stolen at any time. The
    // game itself checks pvoice; its back-pointer must still be this voice.
    const std::optional<std::uint32_t> pvoice = guest_word(rdram, *handle + 0x14);
    if (*state == 1 && pvoice && *pvoice != 0) {
        const std::optional<std::uint32_t> back = guest_word(rdram, *pvoice + 0x08);
        // PVoice.envmixer (+0x8C) .state (+0x14).
        const std::optional<std::uint32_t> envmixer = guest_word(rdram, *pvoice + 0xA0);
        if (back && *back == *handle + 0x0C && envmixer) emitter.envmixer_state = *envmixer;
    }
    return true;
}

bool Add(Snapshot& snapshot, const Emitter& emitter) {
    if (snapshot.emitter_count == snapshot.emitters.size()) return false;
    snapshot.emitters[snapshot.emitter_count++] = emitter;
    return true;
}

void ReadPoints(std::uint8_t* rdram, const Globals& globals, std::uint32_t states,
                Snapshot& snapshot) {
    const std::optional<std::uint32_t> array = guest_word(rdram, globals.audio_points);
    const std::optional<std::int16_t> count = guest_half(rdram, globals.num_audio_points);
    if (!array || !count || *array == 0) return;
    const std::uint32_t points = static_cast<std::uint16_t>(*count);
    if (points > kMaxAudioPoints) {
        Report(1U << 8, "gNumAudioPoints above 40");
        return;
    }
    for (std::uint32_t index = 0; index < points; ++index) {
        const std::optional<std::uint32_t> point = guest_word(rdram, *array + index * 4);
        if (!point || *point == 0) continue;
        const std::optional<Vec3> position = GuestVec3(rdram, *point);
        Emitter emitter;
        emitter.kind = EmitterKind::Point;
        emitter.owner = *point;
        if (!position || !ReadSound(rdram, states, *point + 0x18, emitter)) continue;
        emitter.position = *position;
        Add(snapshot, emitter);
    }
}

void ReadLines(std::uint8_t* rdram, const Globals& globals, std::uint32_t states,
               Snapshot& snapshot) {
    for (std::uint32_t index = 0; index < kAudioLines; ++index) {
        const std::uint32_t line = globals.audio_lines + index * kAudioLineSize;
        // Type 1 lines drive the music jingle player, not a positioned sound.
        const std::optional<std::int8_t> type = guest_byte(rdram, line);
        const std::optional<std::uint32_t> sound_bite = guest_word(rdram, line + 0x16C);
        const std::optional<std::int8_t> segments = guest_byte(rdram, line + 0x17C);
        if (!type || *type != 0 || !sound_bite || *sound_bite == 0 || !segments ||
            *segments <= 0 || static_cast<std::uint32_t>(*segments) > kMaxLineSegments) {
            continue;
        }
        Emitter emitter;
        emitter.kind = EmitterKind::Line;
        emitter.owner = line;
        if (!ReadSound(rdram, states, line + 0x178, emitter)) continue;
        // As audspat_update_all: segment k runs from vertex k to vertex k + 1.
        bool found = false;
        float best = 0.0F;
        bool readable = true;
        for (std::int8_t segment = 0; segment < *segments && readable; ++segment) {
            const std::optional<Vec3> a = GuestVec3(rdram, line + 0x04 + segment * 12U);
            const std::optional<Vec3> b = GuestVec3(rdram, line + 0x04 + (segment + 1U) * 12U);
            if (!a || !b) {
                readable = false;
                break;
            }
            const Vec3 closest = closest_point_on_segment(snapshot.listener.position, *a, *b);
            const float dx = closest.x - snapshot.listener.position.x;
            const float dy = closest.y - snapshot.listener.position.y;
            const float dz = closest.z - snapshot.listener.position.z;
            const float distance = dx * dx + dy * dy + dz * dz;
            if (!found || distance < best) {
                best = distance;
                emitter.position = closest;
                found = true;
            }
        }
        if (readable && found) Add(snapshot, emitter);
    }
}

// Engine sounds: VehicleSoundData soundHandle[2] at +0x48, the idle engine at
// +0x50.
void ReadVehicle(std::uint8_t* rdram, std::uint32_t states, std::uint32_t sound_data,
                 EmitterKind kind, const Vec3& position, std::uint32_t handles,
                 Snapshot& snapshot) {
    constexpr std::uint32_t kHandleFields[] = {0x48, 0x4C, 0x50};
    for (std::uint32_t index = 0; index < handles; ++index) {
        Emitter emitter;
        emitter.kind = kind;
        emitter.owner = sound_data;
        emitter.position = position;
        if (ReadSound(rdram, states, sound_data + kHandleFields[index], emitter)) {
            Add(snapshot, emitter);
        }
    }
}

void ReadEngines(std::uint8_t* rdram, const Globals& globals, std::uint32_t states,
                 Snapshot& snapshot) {
    // AI engines (gBackgroundRacerSounds) play soundHandle[0] only, from
    // racerPos (+0x78), which racer_sound_update copies from the racer.
    for (std::uint32_t slot = 0; slot < 2; ++slot) {
        const std::optional<std::uint32_t> sound_data =
            guest_word(rdram, globals.background_racer_sounds + slot * 4);
        if (!sound_data || *sound_data == 0) continue;
        const std::optional<Vec3> position = GuestVec3(rdram, *sound_data + 0x78);
        if (position) {
            ReadVehicle(rdram, states, *sound_data, EmitterKind::AiEngine, *position, 1, snapshot);
        }
    }
    // Players' engines: gRacersByPort[i] (Object*) -> racer (+0x64) ->
    // vehicleSound (+0x118); the object's position is at +0x0C.
    const std::optional<std::uint32_t> racers = guest_word(rdram, globals.racers_by_port);
    const std::optional<std::uint32_t> num_racers = guest_word(rdram, globals.num_racers);
    if (!racers || *racers == 0 || !num_racers) return;
    const std::uint32_t players = std::min<std::uint32_t>(snapshot.cameras, *num_racers);
    for (std::uint32_t player = 0; player < players && player < 4; ++player) {
        const std::optional<std::uint32_t> object = guest_word(rdram, *racers + player * 4);
        if (!object || *object == 0) continue;
        const std::optional<Vec3> position = GuestVec3(rdram, *object + 0x0C);
        const std::optional<std::uint32_t> racer = guest_word(rdram, *object + 0x64);
        if (!position || !racer || *racer == 0) continue;
        if (player == 0) {
            snapshot.has_player_kart = true;
            snapshot.player_kart = *position;
        }
        const std::optional<std::uint32_t> sound_data = guest_word(rdram, *racer + 0x118);
        if (sound_data && *sound_data != 0) {
            ReadVehicle(rdram, states, *sound_data, EmitterKind::PlayerEngine, *position, 3, snapshot);
        }
    }
}

bool MirroredTracks(std::uint8_t* rdram, recomp_context* context) {
    const GamePayload* payload = active_payload();
    if (payload == nullptr || payload->filtered_cheats == nullptr || context == nullptr) {
        Report(1U << 9, "get_filtered_cheats unavailable; Mirrored Tracks assumed off");
        return false;
    }
    recomp_context call = *context;
    payload->filtered_cheats(rdram, &call);
    return (static_cast<std::uint32_t>(call.r2) & kMirroredTracks) != 0;
}

} // namespace

void capture(std::uint8_t* rdram, recomp_context* context) {
    if (!netplay::external_side_effects_allowed()) return;
    Snapshot snapshot;
    snapshot.tick = ++g_tick;
    const std::optional<Globals> globals = ResolveGlobals();
    if (!globals) {
        Report(1U << 0, "game symbols missing");
        publish_snapshot(snapshot);
        return;
    }
    const std::optional<std::int16_t> pan_mode = guest_half(rdram, globals->stereo_pan_mode);
    const std::optional<std::uint32_t> num_cameras = guest_word(rdram, globals->num_cameras);
    const std::optional<std::int8_t> cutscene = guest_byte(rdram, globals->cutscene_camera);
    const std::optional<std::uint32_t> states = guest_word(rdram, globals->sound_player + 0x40);
    if (!pan_mode || !num_cameras || !cutscene) {
        Report(1U << 1, "pan mode or camera count unreadable");
        publish_snapshot(snapshot);
        return;
    }
    if (*num_cameras < 1 || *num_cameras > 4) {
        Report(1U << 2, "gNumCameras outside 1-4");
        publish_snapshot(snapshot);
        return;
    }
    if (!states || *states == 0) {
        Report(1U << 3, "sound player state array unreadable");
        publish_snapshot(snapshot);
        return;
    }
    // cam_get_cameras: gCameras[4] while a cutscene camera is active.
    const std::uint32_t camera =
        globals->cameras + (*cutscene != 0 ? kCutsceneCameraBase : 0) * kCameraSize;
    const std::optional<std::int16_t> yaw = guest_half(rdram, camera);
    const std::optional<Vec3> position = GuestVec3(rdram, camera + 0x0C);
    if (!yaw || !position) {
        Report(1U << 4, "listener camera unreadable");
        publish_snapshot(snapshot);
        return;
    }
    snapshot.headphones = *pan_mode == kHeadphones;
    snapshot.online = netplay::session().active();
    snapshot.cameras = static_cast<std::uint8_t>(*num_cameras);
    snapshot.listener.position = *position;
    snapshot.listener.yaw = *yaw;
    // get_filtered_cheats reads game state only; its calls use guest stack
    // below the hook's frame. Called only when the result can matter.
    snapshot.listener.mirrored =
        snapshot.headphones && snapshot.cameras == 1 && MirroredTracks(rdram, context);
    ReadPoints(rdram, *globals, *states, snapshot);
    ReadLines(rdram, *globals, *states, snapshot);
    ReadEngines(rdram, *globals, *states, snapshot);
    snapshot.valid = true;
    publish_snapshot(snapshot);
}

} // namespace dkr::runtime::spatial_audio
