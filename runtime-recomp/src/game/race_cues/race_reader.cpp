// Reads the race for the race cues once per authored frame. Read-only: no
// guest writes and no game randomness; the one guest call
// (get_filtered_cheats) only reads. Layouts are from the matching decomp
// (structs.h, objects.h) and identical in both revisions; every global is
// looked up by name.
#include "race_cues/race_reader.hpp"

#include "accessibility/guest_text.hpp"
#include "accessibility/symbol_lookup.hpp"
#include "game_payload.hpp"
#include "race_cues/cue_audio.hpp"
#include "race_cues/cue_settings.hpp"
#include "race_cues/hub_reader.hpp"
#include "race_cues/level_geometry.hpp"
#include "race_cues/turn_tracker.hpp"
#include "revision_addresses.hpp"
#include "runtime_netplay.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <optional>

namespace dkr::race_cues {
namespace {

using accessibility::guest_byte;
using accessibility::guest_half;
using accessibility::guest_word;
namespace symbols = accessibility::symbols;
namespace netplay = runtime::netplay;
namespace revision_addresses = runtime::revision_addresses;
namespace rom = runtime::rom;
using runtime::GamePayload;
using runtime::active_payload;

// LevelHeader.race_type (+0x4C): RACETYPE_DEFAULT, HORSESHOE_GULCH, HUBWORLD
// and BOSS races (include/enums.h).
constexpr std::int8_t kRaceDefault = 0;
constexpr std::int8_t kRaceHorseshoeGulch = 3;
constexpr std::int8_t kRaceHubWorld = 5;
constexpr std::int8_t kRaceBoss = 8;
// LevelHeader.world (+0x00): of the hub world levels, Timber's Island is in
// WORLD_CENTRAL_AREA (0) and the four world hubs in worlds 1 to 4 (Dino
// Domain to Dragon Forest); Future Fun Land's hub is world 5 and the cutscene
// levels are in none (load_level checks the same field).
constexpr std::int8_t kWorldCentralArea = 0;
constexpr std::int8_t kWorldDragonForest = 4;
constexpr std::int16_t kHeadphones = 2;
constexpr std::uint32_t kMirroredTracks = 1U << 2;
constexpr std::int16_t kBehaviourRacer = 1;
constexpr std::uint32_t kMaxCheckpoints = 60;
constexpr std::uint32_t kCheckpointSize = 0x3C;
constexpr std::uint32_t kSegmentSize = 0x44;
constexpr std::int16_t kMaxSegments = 512;
constexpr std::int16_t kMaxBatches = 1024;
constexpr std::int16_t kMaxTriangles = 8192;
// While the kart is in the air, the last edges are kept this many frames.
constexpr int kHoldFrames = 20;
// Object velocity is per 60 Hz tick (moves scale it by the update rate).
constexpr float kTicksPerSecond = 60.0F;

struct Globals {
    rom::Revision revision{};
    std::uint32_t level_model = 0;
    std::uint32_t level_header = 0;
    std::uint32_t checkpoints = 0;
    std::uint32_t main_checkpoints = 0;
    std::uint32_t racers_by_port = 0;
    std::uint32_t num_racers = 0;
    std::uint32_t num_cameras = 0;
    std::uint32_t cutscene_camera = 0;
    std::uint32_t stereo_pan_mode = 0;
    std::uint32_t race_start_timer = 0;
};

std::uint32_t g_reported = 0;

// Each reason is logged once per run.
void Report(std::uint32_t bit, const char* reason) {
    if ((g_reported & bit) != 0) return;
    g_reported |= bit;
    std::fprintf(stderr, "[boot][race-cues] %s\n", reason);
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
        {&Globals::level_model, "gCurrentLevelModel", 4},
        {&Globals::level_header, "gCurrentLevelHeader", 4},
        {&Globals::checkpoints, "gTrackCheckpoints", 4},
        {&Globals::main_checkpoints, "gNumberOfMainCheckpoints", 4},
        {&Globals::racers_by_port, "gRacersByPort", 4},
        {&Globals::num_racers, "gNumRacers", 4},
        {&Globals::num_cameras, "gNumCameras", 4},
        {&Globals::cutscene_camera, "gCutsceneCameraActive", 1},
        {&Globals::stereo_pan_mode, "gStereoPanMode", 2},
        {&Globals::race_start_timer, "gRaceStartTimer", 4},
    };
    Globals globals;
    globals.revision = revision;
    for (const Want& want : kWants) {
        const std::optional<std::uint32_t> address = symbols::data(want.name, want.size);
        if (!address) {
            std::fprintf(stderr, "[boot][race-cues] off: symbol %s not found\n", want.name);
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
    const float value = std::bit_cast<float>(*word);
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<Vec3> GuestVec3(std::uint8_t* rdram, std::uint32_t address) {
    const std::optional<float> x = GuestFloat(rdram, address);
    const std::optional<float> y = GuestFloat(rdram, address + 4);
    const std::optional<float> z = GuestFloat(rdram, address + 8);
    if (!x || !y || !z) return std::nullopt;
    return Vec3{*x, *y, *z};
}

// Everything that identifies the loaded level model: its header fields and
// segment table. A new level can load at the same address.
std::optional<std::uint64_t> ModelFingerprint(std::uint8_t* rdram, std::uint32_t model, std::int16_t segments) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](std::uint32_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (std::uint32_t offset = 0; offset < 0x1C; offset += 4) {
        const std::optional<std::uint32_t> word = guest_word(rdram, model + offset);
        if (!word) return std::nullopt;
        mix(*word);
    }
    const std::optional<std::uint32_t> table = guest_word(rdram, model + 0x04);
    if (!table) return std::nullopt;
    for (std::uint32_t offset = 0; offset < static_cast<std::uint32_t>(segments) * kSegmentSize; offset += 4) {
        const std::optional<std::uint32_t> word = guest_word(rdram, *table + offset);
        if (!word) return std::nullopt;
        mix(*word);
    }
    return hash;
}

// The level model's segments (generate_track has turned its offsets into
// addresses): bounds, triangles with their batch flags and texture surface,
// and each segment's water level (func_8002C71C, unk38).
std::optional<std::vector<RawSegment>> ReadModel(std::uint8_t* rdram, std::uint32_t model) {
    const std::optional<std::uint32_t> textures = guest_word(rdram, model + 0x00);
    const std::optional<std::uint32_t> table = guest_word(rdram, model + 0x04);
    const std::optional<std::uint32_t> bounds = guest_word(rdram, model + 0x08);
    const std::optional<std::int16_t> texture_count = guest_half(rdram, model + 0x18);
    const std::optional<std::int16_t> segment_count = guest_half(rdram, model + 0x1A);
    if (!textures || !table || !bounds || !texture_count || !segment_count || *segment_count <= 0 ||
        *segment_count > kMaxSegments || *texture_count < 0) {
        Report(1U << 10, "level model header unreadable or out of range");
        return std::nullopt;
    }
    std::vector<RawSegment> segments(static_cast<std::size_t>(*segment_count));
    for (std::int16_t index = 0; index < *segment_count; ++index) {
        RawSegment& segment = segments[static_cast<std::size_t>(index)];
        const std::uint32_t box = *bounds + static_cast<std::uint32_t>(index) * 12;
        for (std::uint32_t field = 0; field < 6; ++field) {
            const std::optional<std::int16_t> value = guest_half(rdram, box + field * 2);
            if (!value) {
                Report(1U << 11, "segment bounding box unreadable");
                return std::nullopt;
            }
            segment.bounds[field] = *value;
        }
        const std::uint32_t header = *table + static_cast<std::uint32_t>(index) * kSegmentSize;
        const std::optional<std::uint32_t> vertices = guest_word(rdram, header + 0x00);
        const std::optional<std::uint32_t> triangles = guest_word(rdram, header + 0x04);
        const std::optional<std::uint32_t> batches = guest_word(rdram, header + 0x0C);
        const std::optional<std::int16_t> vertex_count = guest_half(rdram, header + 0x1C);
        const std::optional<std::int16_t> triangle_count = guest_half(rdram, header + 0x1E);
        const std::optional<std::int16_t> batch_count = guest_half(rdram, header + 0x20);
        const std::optional<std::int16_t> water = guest_half(rdram, header + 0x38);
        if (!vertices || !triangles || !batches || !vertex_count || !triangle_count || !batch_count || !water ||
            *batch_count < 0 || *batch_count > kMaxBatches || *triangle_count < 0 ||
            *triangle_count > kMaxTriangles || *vertex_count < 0) {
            Report(1U << 12, "level segment header unreadable or out of range");
            return std::nullopt;
        }
        segment.water_height = static_cast<float>(*water);
        // Batch b's triangles run up to batch b + 1's facesOffset.
        for (std::int16_t batch = 0; batch < *batch_count; ++batch) {
            const std::uint32_t info = *batches + static_cast<std::uint32_t>(batch) * 12;
            const std::optional<std::int8_t> texture = guest_byte(rdram, info + 0x00);
            const std::optional<std::int16_t> first_vertex = guest_half(rdram, info + 0x02);
            const std::optional<std::int16_t> first = guest_half(rdram, info + 0x04);
            const std::optional<std::int16_t> next = guest_half(rdram, info + 12 + 0x04);
            const std::optional<std::uint32_t> flags = guest_word(rdram, info + 0x08);
            if (!texture || !first_vertex || !first || !next || !flags || *first < 0 || *next < *first ||
                *next > *triangle_count || *first_vertex < 0) {
                Report(1U << 13, "triangle batch unreadable or out of range");
                return std::nullopt;
            }
            std::int32_t surface = -1;
            const auto texture_index = static_cast<std::uint8_t>(*texture);
            if (texture_index != 0xFF) {
                if (texture_index >= *texture_count) {
                    Report(1U << 14, "triangle batch texture out of range");
                    return std::nullopt;
                }
                const std::optional<std::int8_t> type = guest_byte(rdram, *textures + texture_index * 8U + 7U);
                if (!type) return std::nullopt;
                surface = *type;
            }
            for (std::int16_t face = *first; face < *next; ++face) {
                const std::uint32_t triangle = *triangles + static_cast<std::uint32_t>(face) * 16;
                RawTriangle raw;
                raw.batch_flags = *flags;
                raw.surface = surface;
                const std::optional<std::int8_t> triangle_flags = guest_byte(rdram, triangle);
                if (!triangle_flags) return std::nullopt;
                raw.flags = static_cast<std::uint8_t>(*triangle_flags);
                for (std::uint32_t corner = 0; corner < 3; ++corner) {
                    const std::optional<std::int8_t> vertex_index = guest_byte(rdram, triangle + 1 + corner);
                    if (!vertex_index) return std::nullopt;
                    const std::int32_t vertex = *first_vertex + static_cast<std::uint8_t>(*vertex_index);
                    if (vertex >= *vertex_count) {
                        Report(1U << 15, "triangle vertex out of range");
                        return std::nullopt;
                    }
                    const std::uint32_t at = *vertices + static_cast<std::uint32_t>(vertex) * 10;
                    for (std::uint32_t axis = 0; axis < 3; ++axis) {
                        const std::optional<std::int16_t> value = guest_half(rdram, at + axis * 2);
                        if (!value) return std::nullopt;
                        raw.vertices[corner][axis] = *value;
                    }
                }
                segment.triangles.push_back(raw);
            }
        }
    }
    return segments;
}

struct Cache {
    std::uint32_t model = 0;
    std::uint64_t fingerprint = 0;
    Vehicle vehicle = Vehicle::Car;
    // Empty when this model could not be read; not read again.
    std::optional<LevelGeometry> geometry;
    bool read = false;
};

Cache g_cache;
TurnTracker g_turns;
CueFrame g_last;
int g_held = 0;

const LevelGeometry* Geometry(std::uint8_t* rdram, std::uint32_t model, Vehicle vehicle) {
    const std::optional<std::int16_t> segments = guest_half(rdram, model + 0x1A);
    if (!segments || *segments <= 0 || *segments > kMaxSegments) {
        Report(1U << 16, "level segment count out of range");
        return nullptr;
    }
    const std::optional<std::uint64_t> fingerprint = ModelFingerprint(rdram, model, *segments);
    if (!fingerprint) {
        Report(1U << 17, "level model unreadable");
        return nullptr;
    }
    if (g_cache.read && g_cache.model == model && g_cache.fingerprint == *fingerprint &&
        g_cache.vehicle == vehicle) {
        return g_cache.geometry ? &*g_cache.geometry : nullptr;
    }
    g_cache = {};
    g_cache.model = model;
    g_cache.fingerprint = *fingerprint;
    g_cache.vehicle = vehicle;
    g_cache.read = true;
    std::optional<std::vector<RawSegment>> raw = ReadModel(rdram, model);
    if (!raw) return nullptr;
    g_cache.geometry.emplace(*raw, vehicle);
    std::fprintf(stderr, "[boot][race-cues] level geometry: %zu segments\n", raw->size());
    return &*g_cache.geometry;
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

std::optional<std::vector<Gate>> ReadGates(std::uint8_t* rdram, const Globals& globals) {
    const std::optional<std::uint32_t> array = guest_word(rdram, globals.checkpoints);
    const std::optional<std::uint32_t> count = guest_word(rdram, globals.main_checkpoints);
    if (!array || !count || *array == 0 || *count < 2 || *count > kMaxCheckpoints) return std::nullopt;
    std::vector<Gate> gates(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        const std::uint32_t node = *array + index * kCheckpointSize;
        const std::optional<Vec3> position = GuestVec3(rdram, node + 0x10);
        const std::optional<std::int8_t> arrow = guest_byte(rdram, node + 0x3B);
        if (!position || !arrow) return std::nullopt;
        gates[index] = {*position, static_cast<std::uint8_t>(*arrow)};
    }
    return gates;
}

// Keys the hub's route grid: the level model and the vehicle it was read for.
std::uint64_t GeometryKey() {
    return g_cache.fingerprint ^ (static_cast<std::uint64_t>(g_cache.vehicle) + 1U) * 0x9E3779B97F4A7C15ULL;
}

void Silence() {
    g_last = {};
    g_held = 0;
    publish(g_last);
    g_turns.reset();
    quiet_hub();
}

} // namespace

void capture(std::uint8_t* rdram, recomp_context* context) {
    if (!netplay::external_side_effects_allowed()) return;
    if (netplay::session().active()) {
        Silence();
        return;
    }
    const std::optional<Globals> globals = ResolveGlobals();
    if (!globals) {
        Report(1U << 0, "off: game symbols missing");
        Silence();
        return;
    }
    // Only between races and menus is there no level or racer; those, split
    // screen and cutscenes are quiet without a log line.
    const std::optional<std::uint32_t> header = guest_word(rdram, globals->level_header);
    const std::optional<std::uint32_t> model = guest_word(rdram, globals->level_model);
    const std::optional<std::uint32_t> racers = guest_word(rdram, globals->racers_by_port);
    const std::optional<std::uint32_t> num_racers = guest_word(rdram, globals->num_racers);
    const std::optional<std::uint32_t> cameras = guest_word(rdram, globals->num_cameras);
    const std::optional<std::int16_t> pan_mode = guest_half(rdram, globals->stereo_pan_mode);
    const std::optional<std::int8_t> cutscene = guest_byte(rdram, globals->cutscene_camera);
    if (!header || !model || !racers || !num_racers || !cameras || !pan_mode || !cutscene || *header == 0 ||
        *model == 0 || *racers == 0 || *num_racers == 0 || *cameras != 1 || *cutscene != 0) {
        Silence();
        return;
    }
    const std::optional<std::int8_t> race_type = guest_byte(rdram, *header + 0x4C);
    if (!race_type) {
        Report(1U << 1, "level header unreadable");
        Silence();
        return;
    }
    const bool race = *race_type == kRaceDefault || *race_type == kRaceHorseshoeGulch || *race_type == kRaceBoss;
    const std::optional<std::int8_t> world = guest_byte(rdram, *header + 0x00);
    const bool hub = *race_type == kRaceHubWorld && world && *world >= kWorldCentralArea &&
                     *world <= kWorldDragonForest;
    if (!hub) leave_hub();
    if (!race && *race_type != kRaceHubWorld) {
        Silence();
        return;
    }
    // Player 1's kart: gRacersByPort[0] -> Object (behaviorId +0x48, racer
    // +0x64) -> Object_Racer (playerIndex +0x000, raceFinished +0x1D8).
    const std::optional<std::uint32_t> object = guest_word(rdram, *racers);
    if (!object || *object == 0) {
        Silence();
        return;
    }
    const std::optional<std::int16_t> behaviour = guest_half(rdram, *object + 0x48);
    const std::optional<std::uint32_t> racer = guest_word(rdram, *object + 0x64);
    if (!behaviour || *behaviour != kBehaviourRacer || !racer || *racer == 0) {
        Report(1U << 2, "player 1 object is not a racer");
        Silence();
        return;
    }
    const std::optional<std::int16_t> player = guest_half(rdram, *racer + 0x000);
    const std::optional<std::int8_t> finished = guest_byte(rdram, *racer + 0x1D8);
    const std::optional<std::int8_t> vehicle_id = guest_byte(rdram, *racer + 0x1D6);
    const std::optional<Vec3> position = GuestVec3(rdram, *object + 0x0C);
    const std::optional<Vec3> velocity = GuestVec3(rdram, *object + 0x1C);
    // ox1/oz1 (+0x38, +0x40): the kart's +Z axis; it drives along its
    // negative (racer->velocity is negative going forwards).
    const std::optional<float> axis_x = GuestFloat(rdram, *racer + 0x38);
    const std::optional<float> axis_z = GuestFloat(rdram, *racer + 0x40);
    const std::optional<std::int8_t> grounded = guest_byte(rdram, *racer + 0x1E2);
    if (!player || !finished || !vehicle_id || !position || !velocity || !axis_x || !axis_z || !grounded) {
        Report(1U << 3, "player 1 racer unreadable");
        Silence();
        return;
    }
    if (*player != 0 || *finished != 0 || *vehicle_id < 0 || *vehicle_id > 2) {
        Silence();
        return;
    }
    const auto vehicle = static_cast<Vehicle>(*vehicle_id);
    const Vec3 forward{-*axis_x, 0.0F, -*axis_z};
    const bool mirrored = MirroredTracks(rdram, context);
    const CueSettings current = settings();

    // Track edge.
    CueFrame frame;
    frame.headphones = *pan_mode == kHeadphones;
    if (current.edge_enabled) {
        const bool airborne = vehicle == Vehicle::Car && *grounded == 0;
        const LevelGeometry* geometry = airborne ? nullptr : Geometry(rdram, *model, vehicle);
        EdgeParams params;
        params.range = edge_range(current.edge_distance);
        const Edges edges = geometry != nullptr ? find_edges(*geometry, *position, forward, params) : Edges{};
        if (edges.known) {
            frame.edge_active = !edges.past_edge;
            // Mirrored Tracks flips the screen and steering, so the player's
            // left is the world's right.
            frame.edge_left = mirrored ? edges.right : edges.left;
            frame.edge_right = mirrored ? edges.left : edges.right;
            g_held = 0;
        } else if (g_held < kHoldFrames) {
            ++g_held;
            frame.edge_active = g_last.edge_active;
            frame.edge_left = g_last.edge_left;
            frame.edge_right = g_last.edge_right;
        }
    }
    g_last = frame;
    publish(frame);

    // The hub beacon: Timber's Island and the four world hubs.
    if (hub) {
        HubKart kart;
        kart.position = *position;
        kart.forward = forward;
        kart.speed = std::sqrt(velocity->x * velocity->x + velocity->z * velocity->z) * kTicksPerSecond;
        kart.vehicle = vehicle;
        kart.mirrored = mirrored;
        kart.world = *world;
        const LevelGeometry* geometry = vehicle == Vehicle::Plane ? nullptr : Geometry(rdram, *model, vehicle);
        capture_hub(rdram, kart, geometry, geometry != nullptr ? GeometryKey() : 0U);
    }

    // Turn signal: races only.
    if (!race || !current.turn_enabled) {
        g_turns.reset();
        return;
    }
    const std::optional<std::vector<Gate>> gates = ReadGates(rdram, *globals);
    if (!gates) {
        g_turns.reset();
        return;
    }
    if (*gates != g_turns.gates()) g_turns.set_course(*gates);
    const std::optional<std::int8_t> next = guest_byte(rdram, *racer + 0x192);
    const std::optional<float> fraction = GuestFloat(rdram, *racer + 0x0A8);
    const std::optional<std::int8_t> alternate = guest_byte(rdram, *racer + 0x1C8);
    const std::optional<std::int8_t> wrong_way = guest_byte(rdram, *racer + 0x1FC);
    const std::optional<std::uint32_t> start_timer = guest_word(rdram, globals->race_start_timer);
    if (!next || !fraction || !alternate || !wrong_way || !start_timer || *next < 0) {
        Report(1U << 4, "player 1 race progress unreadable");
        g_turns.reset();
        return;
    }
    Progress progress;
    progress.next_gate = static_cast<std::size_t>(*next);
    progress.fraction = *fraction;
    progress.speed = std::sqrt(velocity->x * velocity->x + velocity->z * velocity->z) * kTicksPerSecond;
    progress.quiet = *alternate != 0 || *wrong_way != 0 || *start_timer != 0;
    const std::optional<TurnSignal> signal =
        g_turns.update(progress, static_cast<float>(current.turn_warning_tenths) / 10.0F);
    if (!signal) return;
    // The listener frame: +x right, +y up, -z ahead.
    Direction direction;
    if (current.turn_direction == TurnDirection::ToTheSide) {
        direction = {static_cast<float>(signal->side), 0.0F, 0.0F};
    } else {
        const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        const float fx = length > 0.0F ? forward.x / length : 0.0F;
        const float fz = length > 0.0F ? forward.z / length : 1.0F;
        const float dx = signal->position.x - position->x;
        const float dz = signal->position.z - position->z;
        // Facing +Z, +X is left: right of (fx, fz) is (-fz, fx).
        direction = {dx * -fz + dz * fx, signal->position.y - position->y, -(dx * fx + dz * fz)};
    }
    if (mirrored) direction.x = -direction.x;
    signal_turn(direction);
}

} // namespace dkr::race_cues
