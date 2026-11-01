#include "race_cues/cue_audio.hpp"
#include "race_cues/cue_settings.hpp"
#include "race_cues/hub_beacon.hpp"
#include "race_cues/hub_route.hpp"
#include "race_cues/level_geometry.hpp"
#include "race_cues/turn_tracker.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace dkr::race_cues;
using dkr::accessibility::Utterance;

namespace {

// The game's convention, independent of the code under test: +Y up, and
// facing +Z, +X is on the left (the course arrows on every track agree).
constexpr std::uint32_t kNoCollision = 1U << 9;

// A flat rectangle of floor from (x0, z0) to (x1, z1) at height y, as two
// triangles wound as the game's level models are (normal +Y).
void AddFloor(RawSegment& segment, int x0, int x1, int z0, int z1, int y, std::int32_t surface,
              std::uint32_t batch_flags = 0) {
    RawTriangle first;
    first.vertices = {{{x0, y, z0}, {x0, y, z1}, {x1, y, z1}}};
    first.surface = surface;
    first.batch_flags = batch_flags;
    RawTriangle second;
    second.vertices = {{{x0, y, z0}, {x1, y, z1}, {x1, y, z0}}};
    second.surface = surface;
    second.batch_flags = batch_flags;
    if (MakeTriangle(first.vertices, surface).b < 0.0F) {
        std::swap(first.vertices[1], first.vertices[2]);
        std::swap(second.vertices[1], second.vertices[2]);
    }
    segment.triangles.push_back(first);
    segment.triangles.push_back(second);
}

// A vertical wall along x = at, from z0 to z1, y0 to y1.
void AddWall(RawSegment& segment, int at, int z0, int z1, int y0, int y1) {
    RawTriangle first;
    first.vertices = {{{at, y0, z0}, {at, y1, z0}, {at, y1, z1}}};
    first.surface = kSurfaceDefault;
    RawTriangle second;
    second.vertices = {{{at, y0, z0}, {at, y1, z1}, {at, y0, z1}}};
    second.surface = kSurfaceDefault;
    segment.triangles.push_back(first);
    segment.triangles.push_back(second);
}

RawSegment Segment(int x0, int x1, int z0, int z1, int y0, int y1) {
    RawSegment segment;
    segment.bounds = {x0, y0, z0, x1, y1, z1};
    return segment;
}

bool Near(std::optional<float> value, float expected, float tolerance = 4.0F) {
    return value && std::fabs(*value - expected) <= tolerance;
}

const Vec3 kAheadZ{0.0F, 0.0F, 1.0F};
const Vec3 kKart{0.0F, 5.0F, 0.0F};

void road_ends_at_grass_on_each_side() {
    // Road from x = -30 to x = 70, grass beyond. Facing +Z, the left is +X:
    // left edge 70 away, right edge 30 away.
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 10);
    AddFloor(segment, -30, 70, -500, 500, 0, kSurfaceDefault);
    AddFloor(segment, 70, 300, -500, 500, 0, kSurfaceGrass);
    AddFloor(segment, -300, -30, -500, 500, 0, kSurfaceGrass);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(edges.known && !edges.past_edge);
    assert(Near(edges.left, 70.0F));
    assert(Near(edges.right, 30.0F));
}

void heading_turns_the_sides() {
    // Facing +X, the right is +Z (a quarter turn right from facing +Z).
    RawSegment segment = Segment(-300, 300, -300, 300, -10, 10);
    AddFloor(segment, -300, 300, -20, 90, 0, kSurfaceDefault);
    AddFloor(segment, -300, 300, 90, 300, 0, kSurfaceSand);
    AddFloor(segment, -300, 300, -300, -20, 0, kSurfaceSand);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, {1.0F, 0.0F, 0.0F});
    assert(Near(edges.right, 90.0F));
    assert(Near(edges.left, 20.0F));
}

void walls_and_drops_are_edges() {
    // A wall 40 to the right (x = -40); the road simply ends 60 to the left.
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 60);
    AddFloor(segment, -300, 60, -500, 500, 0, kSurfaceDefault);
    AddWall(segment, -40, -500, 500, -10, 60);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(Near(edges.right, 40.0F));
    assert(Near(edges.left, 60.0F));
}

void walls_stop_only_from_their_front() {
    // The same wall winding, facing +X: a wall to the right (x = -40) faces
    // the kart; one to the left (x = 40) shows it its back, which the game's
    // collision lets through (resolve_collisions).
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 60);
    AddFloor(segment, -300, 300, -500, 500, 0, kSurfaceDefault);
    AddWall(segment, -40, -500, 500, -10, 60);
    AddWall(segment, 40, -500, 500, -10, 60);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(Near(edges.right, 40.0F));
    assert(!edges.left);
}

void open_road_has_no_edge_in_reach() {
    RawSegment segment = Segment(-1000, 1000, -500, 500, -10, 10);
    AddFloor(segment, -1000, 1000, -500, 500, 0, kSurfaceDefault);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(edges.known && !edges.left && !edges.right);
}

void snow_is_road_and_stone_is_not() {
    // Snow is as fast as road for cars (gSurfaceTractionTable); stone is not.
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 10);
    AddFloor(segment, -20, 20, -500, 500, 0, kSurfaceDefault);
    AddFloor(segment, 20, 100, -500, 500, 0, 13);
    AddFloor(segment, 100, 300, -500, 500, 0, kSurfaceGrass);
    AddFloor(segment, -300, -20, -500, 500, 0, kSurfaceStone);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(Near(edges.left, 100.0F));
    assert(Near(edges.right, 20.0F));
    assert(full_speed(13, Vehicle::Car) && !full_speed(kSurfaceGrass, Vehicle::Car));
}

void hovercraft_ignore_verges_but_not_stone() {
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 10);
    AddFloor(segment, -20, 20, -500, 500, 0, kSurfaceDefault);
    AddFloor(segment, 20, 300, -500, 500, 0, kSurfaceGrass);
    AddFloor(segment, -60, -20, -500, 500, 0, kSurfaceSand);
    AddFloor(segment, -300, -60, -500, 500, 0, kSurfaceStone);
    const LevelGeometry geometry({segment}, Vehicle::Hovercraft);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(edges.known && !edges.left);
    assert(Near(edges.right, 60.0F));
}

void already_on_the_verge_is_past_the_edge() {
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 10);
    AddFloor(segment, -300, 300, -500, 500, 0, kSurfaceGrass);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(edges.known && edges.past_edge && !edges.left && !edges.right);
}

void bridge_follows_the_kart_s_own_deck() {
    // A narrow upper deck (y 100, x -25 to 25) over a wide lower road. On the
    // deck the drop at its sides is the edge; below, the lower road is open.
    RawSegment segment = Segment(-500, 500, -500, 500, -10, 110);
    AddFloor(segment, -500, 500, -500, 500, 0, kSurfaceDefault);
    AddFloor(segment, -25, 25, -500, 500, 100, kSurfaceDefault);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Edges upper = find_edges(geometry, {0.0F, 105.0F, 0.0F}, kAheadZ);
    assert(Near(upper.left, 25.0F) && Near(upper.right, 25.0F));
    const Edges lower = find_edges(geometry, {300.0F, 5.0F, 0.0F}, kAheadZ);
    assert(lower.known && !lower.past_edge);
}

void water_stops_cars_but_carries_hovercraft() {
    // Road x -30 to 30; beyond, calm water at y 0 over a lake bed at y -30.
    // Calm water has no collision (generate_collision_candidates).
    RawSegment segment = Segment(-300, 300, -500, 500, -40, 10);
    AddFloor(segment, -30, 30, -500, 500, 0, kSurfaceDefault);
    AddFloor(segment, 30, 300, -500, 500, -30, kSurfaceDefault);
    AddFloor(segment, -300, -30, -500, 500, -30, kSurfaceDefault);
    AddFloor(segment, 30, 300, -500, 500, 0, kSurfaceWaterCalm);
    AddFloor(segment, -300, -30, -500, 500, 0, kSurfaceWaterCalm);
    const LevelGeometry car({segment}, Vehicle::Car);
    const Edges car_edges = find_edges(car, kKart, kAheadZ);
    assert(Near(car_edges.left, 30.0F) && Near(car_edges.right, 30.0F));
    const LevelGeometry hovercraft({segment}, Vehicle::Hovercraft);
    const Edges hover_edges = find_edges(hovercraft, kKart, kAheadZ);
    assert(hover_edges.known && !hover_edges.left && !hover_edges.right);
}

void no_collision_batches_are_not_walls() {
    // A see-through wall flagged RENDER_NO_COLLISION is not solid.
    RawSegment segment = Segment(-300, 300, -500, 500, -10, 60);
    AddFloor(segment, -300, 300, -500, 500, 0, kSurfaceDefault);
    RawSegment walled = segment;
    AddWall(walled, -40, -500, 500, -10, 60);
    for (RawTriangle& triangle : walled.triangles) {
        if (triangle.vertices[0][0] == -40 && triangle.vertices[1][0] == -40) triangle.batch_flags = kNoCollision;
    }
    const LevelGeometry geometry({walled}, Vehicle::Car);
    const Edges edges = find_edges(geometry, kKart, kAheadZ);
    assert(!edges.right);
}

std::vector<Gate> Gates(const std::vector<std::pair<Vec3, std::uint8_t>>& list) {
    std::vector<Gate> gates;
    for (const auto& [position, arrow] : list) gates.push_back({position, arrow});
    return gates;
}

void arrows_mark_bends_and_runs_are_one_bend() {
    const std::vector<Gate> gates = Gates({{{0, 0, 0}, 0},
                                           {{0, 0, 100}, 1},
                                           {{0, 0, 200}, 2},
                                           {{0, 0, 300}, 0},
                                           {{0, 0, 400}, 4},
                                           {{0, 0, 500}, 9}});
    const std::vector<Bend> bends = find_bends(gates);
    assert(bends.size() == 2);
    assert(bends[0].gate == 1 && bends[0].side == TurnSide::Left);
    assert(bends[1].gate == 4 && bends[1].side == TurnSide::Right);
    // A run across the lap's end starts at its first gate.
    const std::vector<Bend> wrapped = find_bends(Gates({{{0, 0, 0}, 5}, {{0, 0, 100}, 0}, {{0, 0, 200}, 4}}));
    assert(wrapped.size() == 1 && wrapped[0].gate == 2 && wrapped[0].side == TurnSide::Right);
}

void courses_without_arrows_use_their_shape() {
    // Up +Z, then towards -X: facing +Z, -X is on the right. Every corner of
    // this square is a right turn; the straight gates between are not.
    const std::vector<Gate> gates = Gates({{{0, 0, 0}, 0},
                                           {{0, 0, 100}, 0},
                                           {{0, 0, 200}, 0},
                                           {{-100, 0, 200}, 0},
                                           {{-200, 0, 200}, 0},
                                           {{-200, 0, 100}, 0},
                                           {{-200, 0, 0}, 0},
                                           {{-100, 0, 0}, 0}});
    const std::vector<Bend> bends = find_bends(gates);
    assert(bends.size() == 4);
    for (std::size_t index = 0; index < bends.size(); ++index) {
        assert(bends[index].gate == index * 2);
        assert(bends[index].side == TurnSide::Right);
    }
}

void the_signal_comes_once_per_bend_in_time() {
    // Gates 100 apart along +Z; a left bend at gate 5 of 8.
    std::vector<Gate> gates;
    for (int index = 0; index < 8; ++index) gates.push_back({{0.0F, 0.0F, index * 100.0F}, static_cast<std::uint8_t>(index == 5 ? 1 : 0)});
    TurnTracker tracker;
    tracker.set_course(gates);
    Progress progress;
    progress.speed = 100.0F;
    // 50 left to gate 3, then 100 + 100: 2.5 s away.
    progress.next_gate = 3;
    progress.fraction = 0.5F;
    assert(!tracker.update(progress, 2.0F));
    // 1.5 s away: the signal, from the bend's gate, once.
    progress.next_gate = 4;
    const std::optional<TurnSignal> signal = tracker.update(progress, 2.0F);
    assert(signal && signal->side == TurnSide::Left && signal->position.z == 500.0F);
    assert(!tracker.update(progress, 2.0F));
    // Quiet (wrong way, alternate route) holds it back.
    TurnTracker quiet;
    quiet.set_course(gates);
    progress.quiet = true;
    assert(!quiet.update(progress, 2.0F));
    progress.quiet = false;
    assert(quiet.update(progress, 2.0F));
    // The next lap signals the same bend again.
    progress.next_gate = 6;
    assert(!tracker.update(progress, 2.0F));
    progress.next_gate = 4;
    assert(tracker.update(progress, 2.0F));
}

std::vector<std::uint8_t> Wav(const std::vector<std::int16_t>& samples, std::uint16_t channels, std::uint32_t rate) {
    std::vector<std::uint8_t> bytes;
    const auto put16 = [&](std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    const auto put32 = [&](std::uint32_t value) {
        put16(static_cast<std::uint16_t>(value & 0xFFFF));
        put16(static_cast<std::uint16_t>(value >> 16));
    };
    const auto tag = [&](const char* text) { bytes.insert(bytes.end(), text, text + 4); };
    const auto data = static_cast<std::uint32_t>(samples.size() * 2);
    tag("RIFF");
    put32(36 + data);
    tag("WAVE");
    tag("fmt ");
    put32(16);
    put16(1);
    put16(channels);
    put32(rate);
    put32(rate * channels * 2);
    put16(static_cast<std::uint16_t>(channels * 2));
    put16(16);
    tag("data");
    put32(data);
    for (const std::int16_t sample : samples) put16(static_cast<std::uint16_t>(sample));
    return bytes;
}

void wav_files_are_read_and_mixed_down() {
    const auto stereo = parse_wav(Wav({1000, 3000, -2000, -4000}, 2, 22050));
    assert(stereo && stereo->rate == 22050 && stereo->samples.size() == 2);
    assert(stereo->samples[0] == 2000.0F && stereo->samples[1] == -3000.0F);
    std::vector<std::uint8_t> eight_bit = Wav({0, 0}, 1, 22050);
    eight_bit[34] = 8;
    assert(!parse_wav(eight_bit));
    assert(!parse_wav(std::vector<std::uint8_t>{1, 2, 3}));
}

struct Energy {
    double left = 0.0;
    double right = 0.0;
};

Energy Mix(std::size_t frames) {
    std::vector<float> stereo(frames * 2, 0.0F);
    begin_block(false);
    mix_panned(stereo.data(), frames, 22050);
    Energy energy;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        energy.left += stereo[frame * 2] * stereo[frame * 2];
        energy.right += stereo[frame * 2 + 1] * stereo[frame * 2 + 1];
    }
    return energy;
}

void cues_are_panned_to_their_side() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "dkr-race-cues-tests";
    std::filesystem::create_directories(folder);
    std::vector<std::int16_t> tone(44100);
    for (std::size_t index = 0; index < tone.size(); ++index) {
        tone[index] = static_cast<std::int16_t>(8000.0 * std::sin(index * 0.05));
    }
    std::vector<std::int16_t> beep(4000, 8000);
    const auto write = [](const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    write(folder / "edge.wav", Wav(tone, 1, 44100));
    write(folder / "turn.wav", Wav(beep, 1, 44100));
    write(folder / "beacon.wav", Wav(tone, 1, 44100));
    load_sounds(folder / "edge.wav", folder / "turn.wav", folder / "beacon.wav");

    // Nothing published: silence.
    Energy energy = Mix(4410);
    assert(energy.left == 0.0 && energy.right == 0.0);

    // An edge close on the left: heard on the left only.
    CueFrame frame;
    frame.edge_active = true;
    frame.edge_left = kEdgeLoudest;
    publish(frame);
    for (int block = 0; block < 4; ++block) Mix(2205);
    energy = Mix(2205);
    assert(energy.left > 0.0 && energy.right < energy.left * 1e-6);

    // Further away is quieter; out of reach is silent.
    frame.edge_left = edge_range(settings().edge_distance) * 0.75F;
    publish(frame);
    for (int block = 0; block < 4; ++block) Mix(2205);
    const Energy far = Mix(2205);
    assert(far.left > 0.0 && far.left < energy.left * 0.25);
    // TRACK EDGE DISTANCE 1 does not reach that far; 10 does, more loudly.
    CueSettings near = settings();
    near.edge_distance = 1;
    set_settings(near);
    for (int block = 0; block < 8; ++block) Mix(2205);
    energy = Mix(2205);
    assert(energy.left < 1.0 && energy.right < 1.0);
    near.edge_distance = 10;
    set_settings(near);
    for (int block = 0; block < 8; ++block) Mix(2205);
    assert(Mix(2205).left > far.left);
    set_settings(CueSettings{});
    frame.edge_left.reset();
    publish(frame);
    for (int block = 0; block < 8; ++block) Mix(2205);
    energy = Mix(2205);
    assert(energy.left < 1.0 && energy.right < 1.0);

    // The setting switches it off.
    CueSettings off = settings();
    off.edge_enabled = false;
    set_settings(off);
    frame.edge_right = kEdgeLoudest;
    publish(frame);
    for (int block = 0; block < 8; ++block) Mix(2205);
    energy = Mix(2205);
    assert(energy.left < 1.0 && energy.right < 1.0);
    set_settings(CueSettings{});
    publish(CueFrame{});

    // A turn to the right: three beeps on the right.
    signal_turn({1.0F, 0.0F, 0.0F});
    std::vector<float> stereo(22050 * 2, 0.0F);
    begin_block(false);
    mix_panned(stereo.data(), 22050, 22050);
    int onsets = 0;
    bool sounding = false;
    double left = 0.0;
    double right = 0.0;
    for (std::size_t frame_index = 0; frame_index < 22050; ++frame_index) {
        const bool now = std::fabs(stereo[frame_index * 2 + 1]) > 1.0F;
        if (now && !sounding) ++onsets;
        sounding = now;
        left += std::fabs(stereo[frame_index * 2]);
        right += std::fabs(stereo[frame_index * 2 + 1]);
    }
    assert(onsets == 3);
    assert(right > 0.0 && left < right * 1e-6);
    std::filesystem::remove_all(folder);
}

void settings_survive_the_settings_file() {
    CueSettings changed;
    changed.edge_enabled = false;
    changed.edge_volume = 35;
    changed.edge_distance = 8;
    changed.turn_volume = 70;
    changed.turn_direction = TurnDirection::FromTheTurn;
    changed.turn_warning_tenths = 35;
    changed.beacon_enabled = false;
    changed.beacon_volume = 45;
    set_settings(changed);
    std::ostringstream file;
    write_settings(file);
    set_settings(CueSettings{});
    std::istringstream lines(file.str());
    for (std::string line; std::getline(lines, line);) {
        const std::size_t equals = line.find('=');
        assert(load_setting(line.substr(0, equals), line.substr(equals + 1)));
    }
    const CueSettings loaded = settings();
    assert(!loaded.edge_enabled && loaded.edge_volume == 35 && loaded.edge_distance == 8 && loaded.turn_volume == 70);
    assert(loaded.turn_direction == TurnDirection::FromTheTurn && loaded.turn_warning_tenths == 35);
    assert(!loaded.beacon_enabled && loaded.beacon_volume == 45);
    // Out-of-range values are ignored; other keys are not ours.
    assert(load_setting("race_cue_turn_warning", "90") && settings().turn_warning_tenths == 35);
    assert(!load_setting("master_volume", "1"));
    // The beacon's fields sit above the first 32 bits of the packed settings
    // and must not disturb the others.
    CueSettings beacon_only;
    beacon_only.beacon_volume = 5;
    set_settings(beacon_only);
    assert(settings().beacon_enabled && settings().beacon_volume == 5 && settings().turn_warning_tenths == 20 &&
           settings().edge_volume == 100);
    assert(load_setting("race_cue_beacon_volume", "101") && settings().beacon_volume == 5);
    set_settings(CueSettings{});
}

// A wall along x = at that stops movement both ways (the game's walls stop
// only from their front, so a solid wall has a face on each side).
void AddSolidWall(RawSegment& segment, int at, int z0, int z1, int y0, int y1) {
    AddWall(segment, at, z0, z1, y0, y1);
    RawTriangle first;
    first.vertices = {{{at, y0, z0}, {at, y1, z1}, {at, y1, z0}}};
    first.surface = kSurfaceDefault;
    RawTriangle second;
    second.vertices = {{{at, y0, z0}, {at, y0, z1}, {at, y1, z1}}};
    second.surface = kSurfaceDefault;
    segment.triangles.push_back(first);
    segment.triangles.push_back(second);
}

// The grid's driving rules alone: no clearance cost, so a route's shape
// shows the rules and nothing else.
RouteParams Rules() {
    RouteParams params;
    params.near_cost = 0.0F;
    return params;
}

float PathLength(const std::vector<Vec3>& points) {
    float length = 0.0F;
    for (std::size_t index = 1; index < points.size(); ++index) {
        length += std::hypot(points[index].x - points[index - 1].x, points[index].z - points[index - 1].z);
    }
    return length;
}

void routes_go_around_walls() {
    // A floor 800 square with a wall across it at x = 400 from z = 0 to 600;
    // the way past is the gap from z = 600 to 800.
    RawSegment open = Segment(0, 800, 0, 800, -10, 60);
    AddFloor(open, 0, 800, 0, 800, 0, kSurfaceDefault);
    RawSegment walled = open;
    AddSolidWall(walled, 400, 0, 600, -10, 60);
    const Vec3 from{200.0F, 5.0F, 100.0F};
    const Vec3 to{600.0F, 5.0F, 100.0F};
    // Control: without the wall the route is nearly straight.
    const RouteGraph straight(LevelGeometry({open}, Vehicle::Car), Rules());
    const auto direct = straight.route(from, to, 120.0F, 120.0F);
    assert(direct && PathLength(*direct) < 450.0F);
    const RouteGraph graph(LevelGeometry({walled}, Vehicle::Car), Rules());
    const auto around = graph.route(from, to, 120.0F, 120.0F);
    assert(around && PathLength(*around) > 1000.0F);
    bool through_gap = false;
    for (std::size_t index = 1; index < around->size(); ++index) {
        const Vec3& a = (*around)[index - 1];
        const Vec3& b = (*around)[index];
        if ((a.x < 400.0F) != (b.x < 400.0F)) through_gap = std::min(a.z, b.z) > 600.0F;
    }
    assert(through_gap);
    // A wall seen from its back does not stop the kart (resolve_collisions),
    // so a one-sided wall leaves the way open in one direction only.
    RawSegment one_sided = open;
    AddWall(one_sided, 400, 0, 800, -10, 60);
    const RouteGraph one_way(LevelGeometry({one_sided}, Vehicle::Car), Rules());
    assert(one_way.route(from, to, 120.0F, 120.0F).has_value() != one_way.route(to, from, 120.0F, 120.0F).has_value());
}

void no_route_across_a_gap() {
    // Two floors with 200 units of nothing between them.
    RawSegment segment = Segment(0, 800, 0, 400, -10, 60);
    AddFloor(segment, 0, 300, 0, 400, 0, kSurfaceDefault);
    AddFloor(segment, 500, 800, 0, 400, 0, kSurfaceDefault);
    const Vec3 from{100.0F, 5.0F, 200.0F};
    const Vec3 to{700.0F, 5.0F, 200.0F};
    const RouteGraph graph(LevelGeometry({segment}, Vehicle::Car), Rules());
    assert(!graph.route(from, to, 120.0F, 120.0F));
    // Control: a bridge over the gap joins them.
    RawSegment bridged = segment;
    AddFloor(bridged, 300, 500, 160, 240, 0, kSurfaceDefault);
    const RouteGraph joined(LevelGeometry({bridged}, Vehicle::Car), Rules());
    assert(joined.route(from, to, 120.0F, 120.0F));
    // A cliff too high to climb is a gap too.
    RawSegment cliff = Segment(0, 800, 0, 400, -10, 400);
    AddFloor(cliff, 0, 400, 0, 400, 0, kSurfaceDefault);
    AddFloor(cliff, 400, 800, 0, 400, 300, kSurfaceDefault);
    const RouteGraph steep(LevelGeometry({cliff}, Vehicle::Car), Rules());
    assert(!steep.route(from, {700.0F, 305.0F, 200.0F}, 120.0F, 120.0F));
}

void water_carries_hovercraft_and_slows_cars() {
    // Land on both sides of a strip of calm water at y 0; under it a bed at
    // y -30 (a river) on one row, and nothing (the sea) elsewhere.
    RawSegment segment = Segment(0, 900, 0, 900, -40, 10);
    AddFloor(segment, 0, 300, 0, 900, 0, kSurfaceDefault);
    AddFloor(segment, 600, 900, 0, 900, 0, kSurfaceDefault);
    AddFloor(segment, 300, 600, 0, 900, 0, kSurfaceWaterCalm);
    RawSegment river = segment;
    AddFloor(river, 300, 600, 700, 900, -30, kSurfaceDefault);
    const Vec3 from{100.0F, 5.0F, 100.0F};
    const Vec3 to{800.0F, 5.0F, 100.0F};
    // Cars float across a river with a bed, the long way round rather than
    // nowhere; with no bed (the sea) there is no route for them.
    const RouteGraph cars(LevelGeometry({river}, Vehicle::Car), Rules());
    const auto ford = cars.route(from, to, 120.0F, 120.0F);
    assert(ford);
    for (const Vec3& point : *ford) {
        if (point.x > 300.0F && point.x < 600.0F) assert(point.z > 700.0F);
    }
    const RouteGraph sea(LevelGeometry({segment}, Vehicle::Car), Rules());
    assert(!sea.route(from, to, 120.0F, 120.0F));
    // Hovercraft cross the open water directly.
    const RouteGraph hovercraft(LevelGeometry({segment}, Vehicle::Hovercraft), Rules());
    const auto crossing = hovercraft.route(from, to, 120.0F, 120.0F);
    assert(crossing && PathLength(*crossing) < 800.0F);
}

float Heading(const Vec3& a, const Vec3& b) {
    return std::atan2(b.z - a.z, b.x - a.x) * 180.0F / 3.14159265F;
}

// The largest change of heading between successive legs, in degrees.
float SharpestTurn(const std::vector<Vec3>& points) {
    float sharpest = 0.0F;
    for (std::size_t index = 2; index < points.size(); ++index) {
        float turn = std::fabs(Heading(points[index - 1], points[index]) - Heading(points[index - 2], points[index - 1]));
        if (turn > 180.0F) turn = 360.0F - turn;
        sharpest = std::max(sharpest, turn);
    }
    return sharpest;
}

void open_ground_gives_a_straight_route() {
    // Open floor: the quickest way is the straight line, 721 units.
    RawSegment segment = Segment(0, 1200, 0, 1200, -10, 10);
    AddFloor(segment, 0, 1200, 0, 1200, 0, kSurfaceDefault);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Vec3 from{300.0F, 5.0F, 300.0F};
    const Vec3 to{900.0F, 5.0F, 700.0F};
    const float line = std::hypot(600.0F, 400.0F);
    const auto route = RouteGraph(geometry, Rules()).route(from, to, 120.0F, 120.0F);
    assert(route && route->size() <= 3 && PathLength(*route) < line * 1.02F && SharpestTurn(*route) < 20.0F);
    // Control: the grid's own steps (straight and diagonal only) zigzag and
    // run longer.
    RouteParams grid = Rules();
    grid.straighten = false;
    const auto steps = RouteGraph(geometry, grid).route(from, to, 120.0F, 120.0F);
    assert(steps && PathLength(*steps) > line * 1.04F && SharpestTurn(*steps) >= 45.0F);
}

void routes_keep_clear_of_walls_where_there_is_room() {
    // A corridor 400 wide between solid walls at x = 0 and x = 400, from a
    // start and an end 60 from the left wall: there is room to keep 120 or
    // more from both walls along the middle.
    RawSegment segment = Segment(-200, 600, 0, 1400, -10, 60);
    AddFloor(segment, -200, 600, 0, 1400, 0, kSurfaceDefault);
    AddSolidWall(segment, 0, 0, 1400, -10, 60);
    AddSolidWall(segment, 400, 0, 1400, -10, 60);
    const LevelGeometry geometry({segment}, Vehicle::Car);
    const Vec3 from{60.0F, 5.0F, 100.0F};
    const Vec3 to{60.0F, 5.0F, 1300.0F};
    const auto middle_x = [](const std::vector<Vec3>& route) {
        // Where the route crosses z = 700, half way.
        for (std::size_t index = 1; index < route.size(); ++index) {
            const Vec3& a = route[index - 1];
            const Vec3& b = route[index];
            if ((a.z - 700.0F) * (b.z - 700.0F) <= 0.0F && a.z != b.z) return a.x + (b.x - a.x) * (700.0F - a.z) / (b.z - a.z);
        }
        return -1.0F;
    };
    const auto clear = RouteGraph(geometry).route(from, to, 120.0F, 120.0F);
    assert(clear);
    const float x = middle_x(*clear);
    assert(x >= 120.0F && x <= 280.0F);
    // Control: without the clearance cost the route hugs the wall.
    const auto hugging = RouteGraph(geometry, Rules()).route(from, to, 120.0F, 120.0F);
    assert(hugging && middle_x(*hugging) < 100.0F);
}

void routes_prefer_full_speed_ground() {
    // Road around a patch of grass 800 long. A car's top speed goes as one
    // over the square root of the traction (drag is velocity squared times
    // gSurfaceTractionTable: 0.004 road, 0.007 grass), so crossing the grass
    // takes 800 x 1.32 + 800 road = 1,858 road units. Round a patch 300 wide
    // the road is 1,654: the road is quicker. Round one 1,000 wide it is
    // 2,081: the grass is quicker.
    const auto field = [](int half, std::int32_t patch) {
        RawSegment segment = Segment(0, 2000, 0, 2000, -10, 10);
        AddFloor(segment, 0, 600, 0, 2000, 0, kSurfaceDefault);
        AddFloor(segment, 1400, 2000, 0, 2000, 0, kSurfaceDefault);
        AddFloor(segment, 600, 1400, 0, 1000 - half, 0, kSurfaceDefault);
        AddFloor(segment, 600, 1400, 1000 + half, 2000, 0, kSurfaceDefault);
        AddFloor(segment, 600, 1400, 1000 - half, 1000 + half, 0, patch);
        return segment;
    };
    const Vec3 from{200.0F, 5.0F, 1000.0F};
    const Vec3 to{1800.0F, 5.0F, 1000.0F};
    const auto crosses = [](const std::vector<Vec3>& route, int half) {
        // Any point of the route inside the patch.
        for (std::size_t index = 1; index < route.size(); ++index) {
            for (float t = 0.0F; t <= 1.0F; t += 0.01F) {
                const float x = route[index - 1].x + (route[index].x - route[index - 1].x) * t;
                const float z = route[index - 1].z + (route[index].z - route[index - 1].z) * t;
                if (x > 610.0F && x < 1390.0F && std::fabs(z - 1000.0F) < static_cast<float>(half) - 10.0F) return true;
            }
        }
        return false;
    };
    const LevelGeometry narrow({field(150, kSurfaceGrass)}, Vehicle::Car);
    const auto around = RouteGraph(narrow, Rules()).route(from, to, 120.0F, 120.0F);
    assert(around && !crosses(*around, 150));
    const LevelGeometry wide({field(500, kSurfaceGrass)}, Vehicle::Car);
    const auto across = RouteGraph(wide, Rules()).route(from, to, 120.0F, 120.0F);
    assert(across && crosses(*across, 500));
    // Control: the same patch as road is crossed straight.
    const LevelGeometry road({field(150, kSurfaceDefault)}, Vehicle::Car);
    const auto straight = RouteGraph(road, Rules()).route(from, to, 120.0F, 120.0F);
    assert(straight && crosses(*straight, 150));
    // Hovercraft are not slowed by grass (only stone): straight across.
    const LevelGeometry hover({field(150, kSurfaceGrass)}, Vehicle::Hovercraft);
    const auto skim = RouteGraph(hover, Rules()).route(from, to, 120.0F, 120.0F);
    assert(skim && crosses(*skim, 150));
}

Destination Character(DestinationKind kind, const Vec3& position) {
    Destination character;
    character.kind = kind;
    character.position = position;
    return character;
}

void destinations_list_open_worlds_taj_and_nearest_balloons() {
    HubContents hub;
    hub.entrances = {
        {DestinationKind::World, 24, 3, "Snowflake Mountain", {-3828.0F, 405.0F, -3653.0F}, true},
        {DestinationKind::World, 12, 1, "Dino Domain", {-4015.0F, 383.0F, 2683.0F}, true},
        {DestinationKind::World, 14, 2, "Sherbet Island", {5086.0F, -123.0F, -1187.0F}, false},
        {DestinationKind::World, 2, 4, "", {609.0F, 592.0F, 4137.0F}, true},
    };
    hub.character = Character(DestinationKind::Taj, {-197.0F, 255.0F, 193.0F});
    hub.balloons = {{7, {1000.0F, 0.0F, 0.0F}}, {3, {100.0F, 0.0F, 0.0F}}, {9, {0.0F, 0.0F, -500.0F}}};
    const std::vector<Destination> list = destination_list(hub, {0.0F, 0.0F, 0.0F});
    // Open worlds in the game's world order (a closed gate and an unreadable
    // name leave a world out), then Taj, then balloons nearest first.
    assert(list.size() == 6);
    assert(list[0].name == "Dino Domain" && list[0].id == 12 && list[0].kind == DestinationKind::World);
    assert(list[1].name == "Snowflake Mountain");
    assert(list[2].kind == DestinationKind::Taj && list[2].name == "TAJ");
    assert(list[3].kind == DestinationKind::Balloon && list[3].id == 3 && list[3].name == "BALLOON");
    assert(list[4].id == 9 && list[5].id == 7);
}

void world_hubs_list_tracks_boss_trophy_tt_and_the_way_back() {
    // Dino Domain's hub as the game holds it: four race doors asking for 1,
    // 2, 3 and 5 balloons (the track select's order too), the boss door, the
    // trophy cabinet, T.T. and the gate back to the island.
    HubContents hub;
    hub.entrances = {
        {DestinationKind::Island, 0, 0x7FFF, "", {-1524.0F, -1.0F, -657.0F}, true},
        {DestinationKind::Track, 7, 5, "Hot Top Volcano", {-410.0F, -3.0F, -1015.0F}, false},
        {DestinationKind::Boss, 38, 4, "BOSS DOOR TEXT", {-771.0F, -4.0F, 1777.0F}, true},
        {DestinationKind::Track, 29, 3, "Jungle Falls", {392.0F, 0.0F, 1032.0F}, true},
        {DestinationKind::Track, 3, 2, "Fossil Canyon", {1038.0F, -3.0F, -416.0F}, true},
        {DestinationKind::Track, 5, 1, "Ancient Lake", {1008.0F, 0.0F, 448.0F}, true},
    };
    hub.character = Character(DestinationKind::TT, {-9.0F, -52.0F, -561.0F});
    hub.trophy_race = Destination{DestinationKind::TrophyRace, 0, "TROPHY RACE", {933.0F, -6.0F, -2172.0F}};
    const std::vector<Destination> list = destination_list(hub, {0.0F, 0.0F, 0.0F});
    // Hot Top Volcano's door is still closed.
    assert(list.size() == 7);
    assert(list[0].name == "Ancient Lake" && list[1].name == "Fossil Canyon" && list[2].name == "Jungle Falls");
    // The boss door is named with its game text as the reader found it.
    assert(list[3].kind == DestinationKind::Boss && list[3].name == "BOSS DOOR TEXT" && list[3].id == 38);
    assert(list[4].kind == DestinationKind::TrophyRace && list[4].name == "TROPHY RACE");
    assert(list[5].kind == DestinationKind::TT && list[5].name == "T.T.");
    assert(list[6].kind == DestinationKind::Island && list[6].name == "TIMBER'S ISLAND");
    // The trophy race without its game text is left out, not named by the mod.
    hub.trophy_race->name.clear();
    assert(destination_list(hub, {0.0F, 0.0F, 0.0F}).size() == 6);
    // So is the boss door.
    hub.entrances[2].name.clear();
    const std::vector<Destination> unnamed = destination_list(hub, {0.0F, 0.0F, 0.0F});
    assert(unnamed.size() == 5);
    for (const Destination& destination : unnamed) assert(destination.kind != DestinationKind::Boss);
}

void pitch_rises_as_the_destination_nears() {
    // Its own pitch from kBeaconFar on, an octave higher at the destination,
    // the same musical step for every equal distance closer.
    assert(std::fabs(beacon_pitch(kBeaconFar) - 1.0F) < 1e-5F);
    assert(std::fabs(beacon_pitch(kBeaconFar * 3.0F) - 1.0F) < 1e-5F);
    assert(std::fabs(beacon_pitch(0.0F) - 2.0F) < 1e-5F);
    assert(std::fabs(beacon_pitch(kBeaconFar / 2.0F) - std::sqrt(2.0F)) < 1e-5F);
    assert(std::fabs(beacon_pitch(kBeaconFar / 4.0F) / beacon_pitch(kBeaconFar / 2.0F) -
                     beacon_pitch(kBeaconFar / 2.0F) / beacon_pitch(kBeaconFar * 3.0F / 4.0F)) < 1e-5F);
    assert(beacon_pitch(500.0F) > beacon_pitch(1500.0F));
}

void the_aim_moves_forward_only() {
    // Along +Z for 1000, then along +X for 1000.
    RouteFollower route;
    route.set({{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1000.0F}, {1000.0F, 0.0F, 1000.0F}});
    assert(route.advance({10.0F, 0.0F, 200.0F}) < 11.0F);
    assert(std::fabs(route.progress() - 200.0F) < 0.5F);
    const Vec3 aim = route.aim(300.0F);
    assert(std::fabs(aim.x) < 0.5F && std::fabs(aim.z - 500.0F) < 0.5F);
    // Past the corner the aim turns with the route.
    route.advance({0.0F, 0.0F, 900.0F});
    const Vec3 round = route.aim(300.0F);
    assert(std::fabs(round.x - 200.0F) < 0.5F && std::fabs(round.z - 1000.0F) < 0.5F);
    // Backing up does not move the kart's place back.
    route.advance({0.0F, 0.0F, 400.0F});
    assert(std::fabs(route.progress() - 900.0F) < 0.5F);
    // The aim stops at the end; what is left shrinks as the kart goes on.
    assert(std::fabs(route.aim(5000.0F).x - 1000.0F) < 0.5F);
    assert(std::fabs(route.remaining() - 1100.0F) < 0.5F);
}

void bearings_follow_the_game_axes() {
    // Facing +Z, +X is on the left (the course arrows on every track agree).
    using Bearing = dkr::accessibility::text::Phrase;
    const Vec3 origin{0.0F, 0.0F, 0.0F};
    const Vec3 north{0.0F, 0.0F, 1.0F};
    assert(bearing(origin, north, {0.0F, 0.0F, 100.0F}, false) == Bearing::Ahead);
    assert(bearing(origin, north, {100.0F, 0.0F, 0.0F}, false) == Bearing::Left);
    assert(bearing(origin, north, {-100.0F, 0.0F, 0.0F}, false) == Bearing::Right);
    assert(bearing(origin, north, {0.0F, 0.0F, -100.0F}, false) == Bearing::Behind);
    assert(bearing(origin, north, {100.0F, 0.0F, 100.0F}, false) == Bearing::AheadLeft);
    assert(bearing(origin, north, {-100.0F, 0.0F, 100.0F}, false) == Bearing::AheadRight);
    assert(bearing(origin, north, {-100.0F, 0.0F, -100.0F}, false) == Bearing::BehindRight);
    assert(bearing(origin, north, {100.0F, 0.0F, -100.0F}, false) == Bearing::BehindLeft);
    // Facing +X (a quarter turn left of +Z), +Z is on the right.
    assert(bearing(origin, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F}, false) == Bearing::Right);
    // Within 22.5 degrees of ahead is ahead; height does not matter.
    assert(bearing(origin, north, {30.0F, 500.0F, 100.0F}, false) == Bearing::Ahead);
    // Mirrored Tracks swaps the player's left and right.
    assert(bearing(origin, north, {100.0F, 0.0F, 0.0F}, true) == Bearing::Right);
}

std::vector<Destination> HubList() {
    HubContents hub;
    hub.entrances = {{DestinationKind::World, 12, 1, "DINO DOMAIN", {0.0F, 0.0F, 3000.0F}, true}};
    hub.character = Character(DestinationKind::Taj, {2000.0F, 0.0F, 0.0F});
    hub.balloons = {{4, {-1000.0F, 0.0F, 0.0F}}};
    return destination_list(hub, {0.0F, 0.0F, 0.0F});
}

BeaconInput Input(std::uint16_t pressed, const Vec3& kart, double seconds) {
    BeaconInput input;
    input.pressed = pressed;
    input.kart = kart;
    input.forward = {0.0F, 0.0F, 1.0F};
    input.speed = 0.0F;
    input.seconds = seconds;
    input.destinations = HubList();
    return input;
}

// Straight routes, so where the sound comes from is easy to predict.
std::optional<std::vector<Vec3>> Straight(const Vec3& from, const Vec3&) {
    return std::vector<Vec3>{from};
}

using Speech = std::vector<Utterance>;

void picking_starting_and_stopping_speak() {
    HubBeacon beacon;
    const Vec3 start{0.0F, 0.0F, 0.0F};
    // The first pick is the first destination.
    BeaconOutput output = beacon.update(Input(kDpadRight, start, 0.0), Straight);
    assert((output.speech == Speech{{"DINO DOMAIN, 1 of 3", true}}) && !output.sounding);
    output = beacon.update(Input(kDpadRight, start, 0.1), Straight);
    assert((output.speech == Speech{{"TAJ, 2 of 3", true}}));
    output = beacon.update(Input(kDpadRight, start, 0.2), Straight);
    assert((output.speech == Speech{{"BALLOON, 3 of 3", true}}));
    // The ends stop the choice silently, as the menus do.
    output = beacon.update(Input(kDpadRight, start, 0.3), Straight);
    assert(output.speech.empty());
    output = beacon.update(Input(kDpadLeft, start, 0.4), Straight);
    assert((output.speech == Speech{{"TAJ, 2 of 3", true}}));
    // Down speaks where it is: Taj at +X, facing +Z, is on the left.
    output = beacon.update(Input(kDpadDown, start, 0.5), Straight);
    assert((output.speech == Speech{{"TAJ, LEFT", true}}));
    // D-pad Down with START held is Quick Restart: nothing is said.
    BeaconInput restart = Input(kDpadDown, start, 0.6);
    restart.held = kButtonStart;
    assert(beacon.update(restart, Straight).speech.empty());
    // Up starts guidance; the sound comes from the left.
    output = beacon.update(Input(kDpadUp, start, 0.7), Straight);
    assert((output.speech == Speech{{"GUIDING, TAJ", true}}));
    assert(output.sounding && output.aim.x > 0.0F);
    output = beacon.update(Input(0, start, 0.8), Straight);
    assert(output.sounding && output.speech.empty());
    // Up again stops it.
    output = beacon.update(Input(kDpadUp, start, 0.9), Straight);
    assert((output.speech == Speech{{"GUIDANCE OFF", true}}) && !output.sounding);
    // With no route the beacon stays silent and says so.
    const RoutePlanner nowhere = [](const Vec3&, const Vec3&) { return std::optional<std::vector<Vec3>>{}; };
    output = beacon.update(Input(kDpadUp, start, 1.0), nowhere);
    assert((output.speech == Speech{{"NO ROUTE, TAJ", true}}) && !output.sounding);
    assert(!beacon.update(Input(0, start, 3.0), nowhere).sounding);
}

void arrival_is_spoken_only_on_reaching_the_destination() {
    // A world entrance: closer is higher, and reaching it says nothing
    // (driving through it leaves the hub).
    HubBeacon beacon;
    beacon.update(Input(kDpadRight, {0.0F, 0.0F, 0.0F}, 0.0), Straight);
    BeaconOutput output = beacon.update(Input(kDpadUp, {0.0F, 0.0F, 0.0F}, 0.1), Straight);
    assert((output.speech == Speech{{"GUIDING, DINO DOMAIN", true}}) && output.sounding);
    const float far = output.pitch;
    output = beacon.update(Input(0, {0.0F, 0.0F, 2000.0F}, 2.0), Straight);
    assert(output.sounding && output.pitch > far);
    output = beacon.update(Input(0, {0.0F, 0.0F, 2990.0F}, 3.0), Straight);
    assert(output.speech.empty() && output.sounding);

    // A balloon: "ARRIVED" once it is collected, then guidance ends.
    HubBeacon balloon;
    for (int step = 0; step < 3; ++step) balloon.update(Input(kDpadRight, {0.0F, 0.0F, 0.0F}, 0.0), Straight);
    assert(balloon.update(Input(kDpadUp, {0.0F, 0.0F, 0.0F}, 0.1), Straight).sounding);
    output = balloon.update(Input(0, {-990.0F, 0.0F, 0.0F}, 0.2), Straight);
    assert(output.speech.empty() && output.sounding);
    BeaconInput collected = Input(0, {-1000.0F, 0.0F, 0.0F}, 0.3);
    collected.destinations.pop_back();
    collected.course_flags = 0x10000U << 4;
    output = balloon.update(collected, Straight);
    assert((output.speech == Speech{{"ARRIVED, BALLOON", false}}) && !output.sounding && !balloon.guiding());
    // Another balloon's flag is not this one.
    HubBeacon other;
    for (int step = 0; step < 3; ++step) other.update(Input(kDpadRight, {0.0F, 0.0F, 0.0F}, 0.0), Straight);
    other.update(Input(kDpadUp, {0.0F, 0.0F, 0.0F}, 0.1), Straight);
    BeaconInput elsewhere = Input(0, {0.0F, 0.0F, 0.0F}, 0.2);
    elsewhere.course_flags = 0x10000U << 5;
    output = other.update(elsewhere, Straight);
    assert(output.speech.empty() && output.sounding);
    // Gone without being reached: guidance ends without a word.
    elsewhere.destinations.pop_back();
    output = other.update(elsewhere, Straight);
    assert(output.speech.empty() && !output.sounding && !other.guiding());

    // Taj: being close is not enough; Taj turning to the player is.
    HubBeacon taj;
    for (int step = 0; step < 2; ++step) taj.update(Input(kDpadRight, {0.0F, 0.0F, 0.0F}, 0.0), Straight);
    assert((taj.update(Input(kDpadUp, {0.0F, 0.0F, 0.0F}, 0.1), Straight).speech ==
            Speech{{"GUIDING, TAJ", true}}));
    output = taj.update(Input(0, {1990.0F, 0.0F, 0.0F}, 0.2), Straight);
    assert(output.speech.empty() && output.sounding);
    BeaconInput met = Input(0, {1990.0F, 0.0F, 0.0F}, 0.3);
    met.destinations[1].reached = true;
    output = taj.update(met, Straight);
    assert((output.speech == Speech{{"ARRIVED, TAJ", false}}) && !output.sounding && !taj.guiding());
}

void water_under_a_floor_is_not_open() {
    // Sea at y 0 (calm water over nothing) with a low island (floor at 24)
    // in the middle: a hovercraft crossing goes over the island, never on
    // water underneath it.
    RawSegment segment = Segment(0, 1200, 0, 400, -10, 40);
    AddFloor(segment, 0, 1200, 0, 400, 0, kSurfaceWaterCalm);
    AddFloor(segment, 400, 800, 0, 400, 24, kSurfaceDefault);
    const LevelGeometry geometry({segment}, Vehicle::Hovercraft);
    RouteParams grid = Rules();
    grid.straighten = false;
    const auto route = RouteGraph(geometry, grid).route({100.0F, 5.0F, 200.0F}, {1100.0F, 5.0F, 200.0F}, 120.0F, 120.0F);
    assert(route);
    for (const Vec3& point : *route) {
        if (point.x > 420.0F && point.x < 780.0F) assert(std::fabs(point.y - 24.0F) < 1.0F);
    }
}

void the_sound_leads_by_a_steady_distance() {
    // The sound sits ahead along the route by about the distance covered in
    // 0.4 s (120 to 400 units), following a change of speed gradually.
    HubBeacon beacon;
    BeaconInput input = Input(kDpadRight, {0.0F, 0.0F, 0.0F}, 0.0);
    beacon.update(input, Straight);
    input = Input(kDpadUp, {0.0F, 0.0F, 0.0F}, 0.1);
    input.speed = 300.0F;
    BeaconOutput output = beacon.update(input, Straight);
    assert(std::fabs(output.aim.z - 120.0F) < 1.0F);
    // Suddenly 1,000 units a second: not at once 400 ahead.
    input = Input(0, {0.0F, 0.0F, 0.0F}, 0.15);
    input.speed = 1000.0F;
    output = beacon.update(input, Straight);
    assert(output.aim.z > 120.0F && output.aim.z < 200.0F);
    for (int frame = 0; frame < 120; ++frame) {
        input = Input(0, {0.0F, 0.0F, 0.0F}, 0.15 + frame / 60.0);
        input.speed = 1000.0F;
        output = beacon.update(input, Straight);
    }
    assert(std::fabs(output.aim.z - 400.0F) < 10.0F);
}

void the_beacon_is_a_steady_loop_whose_pitch_rises() {
    // After cues_are_panned_to_their_side has loaded the sounds: the beacon is
    // a 351 Hz tone. Zero crossings per second give its pitch; 10 ms windows
    // show whether it ever stops.
    struct Heard {
        double left = 0.0;
        double right = 0.0;
        int crossings = 0;
        int silent_windows = 0;
    };
    const auto listen = [](std::size_t frames) {
        std::vector<float> stereo(frames * 2, 0.0F);
        begin_block(false);
        mix_panned(stereo.data(), frames, 22050);
        Heard heard;
        double window = 0.0;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const float sample = stereo[frame * 2] + stereo[frame * 2 + 1];
            if (frame > 0 && (sample >= 0.0F) != (stereo[frame * 2 - 2] + stereo[frame * 2 - 1] >= 0.0F)) {
                ++heard.crossings;
            }
            heard.left += std::fabs(stereo[frame * 2]);
            heard.right += std::fabs(stereo[frame * 2 + 1]);
            window += std::fabs(sample);
            if ((frame + 1) % 220 == 0) {
                if (window < 220.0) ++heard.silent_windows;
                window = 0.0;
            }
        }
        return heard;
    };
    BeaconFrame frame;
    frame.active = true;
    frame.direction = {1.0F, 0.0F, 0.0F};
    frame.pitch = 1.0F;
    publish_beacon(frame);
    listen(2205);
    // Continuous, from the right, at its own pitch (702 crossings a second).
    const Heard steady = listen(22050);
    assert(steady.silent_windows == 0);
    assert(steady.right > 0.0 && steady.left < steady.right * 1e-6);
    assert(steady.crossings > 680 && steady.crossings < 720);
    // An octave up glides there instead of jumping: the first 20 ms are still
    // well below it, and a second later it is there.
    frame.pitch = 2.0F;
    publish_beacon(frame);
    const Heard start = listen(441);
    assert(start.crossings * 50 < 1200);
    listen(22050);
    const Heard octave = listen(22050);
    assert(octave.crossings > 1370 && octave.crossings < 1440 && octave.silent_windows == 0);
    // The level does not change with the pitch.
    assert(std::fabs(octave.right - steady.right) < steady.right * 0.1);
    // Off in the settings, or inactive: it fades out and stays silent.
    CueSettings off = settings();
    off.beacon_enabled = false;
    set_settings(off);
    listen(22050);
    assert(listen(22050).right < 1.0);
    set_settings(CueSettings{});
    listen(2205);
    publish_beacon({});
    listen(22050);
    assert(listen(22050).right < 1.0);
}

} // namespace

int main() {
    road_ends_at_grass_on_each_side();
    heading_turns_the_sides();
    walls_and_drops_are_edges();
    walls_stop_only_from_their_front();
    open_road_has_no_edge_in_reach();
    snow_is_road_and_stone_is_not();
    hovercraft_ignore_verges_but_not_stone();
    already_on_the_verge_is_past_the_edge();
    bridge_follows_the_kart_s_own_deck();
    water_stops_cars_but_carries_hovercraft();
    no_collision_batches_are_not_walls();
    arrows_mark_bends_and_runs_are_one_bend();
    courses_without_arrows_use_their_shape();
    the_signal_comes_once_per_bend_in_time();
    wav_files_are_read_and_mixed_down();
    cues_are_panned_to_their_side();
    the_beacon_is_a_steady_loop_whose_pitch_rises();
    settings_survive_the_settings_file();
    routes_go_around_walls();
    no_route_across_a_gap();
    water_carries_hovercraft_and_slows_cars();
    destinations_list_open_worlds_taj_and_nearest_balloons();
    world_hubs_list_tracks_boss_trophy_tt_and_the_way_back();
    open_ground_gives_a_straight_route();
    routes_keep_clear_of_walls_where_there_is_room();
    routes_prefer_full_speed_ground();
    water_under_a_floor_is_not_open();
    the_sound_leads_by_a_steady_distance();
    pitch_rises_as_the_destination_nears();
    the_aim_moves_forward_only();
    bearings_follow_the_game_axes();
    picking_starting_and_stopping_speak();
    arrival_is_spoken_only_on_reaching_the_destination();
    std::puts("DKRRaceCues: PASS");
    return 0;
}
