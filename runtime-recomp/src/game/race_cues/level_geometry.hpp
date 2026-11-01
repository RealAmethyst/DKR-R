#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// A host copy of the level's collision geometry and the track edge search for
// the track edge sound. Layouts and rules follow the matching decomp:
// generate_track, track_init_collision (tracks.c), generate_collision_candidates
// and resolve_collisions (hasm/collision.c), get_level_segment_waves and
// get_wave_properties (tracks.c). World axes: +Y up; facing +Z, +X is left.
namespace dkr::race_cues {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    bool operator==(const Vec3&) const = default;
};

// SurfaceTypes (include/enums.h).
enum Surface : std::int32_t {
    kSurfaceDefault = 0,
    kSurfaceGrass = 1,
    kSurfaceSand = 2,
    kSurfaceStone = 4,
    kSurfaceWaterCalm = 11,
    kSurfaceWaterWavy = 14,
    kSurfaceWaterUnkF = 15,
    kSurfaceInvisWall = 17,
    kSurfaceUnk12 = 18,
    kSurfaceCount = 19,
};

// Vehicles (include/enums.h).
enum class Vehicle : std::int32_t { Car = 0, Hovercraft = 1, Plane = 2 };

struct Triangle {
    // Vertices in the level's s16 units.
    std::array<std::array<std::int32_t, 3>, 3> vertices{};
    // Plane a*x + b*y + c*z + d = 0 with a unit normal, as track_init_collision.
    float a = 0.0F;
    float b = 0.0F;
    float c = 0.0F;
    float d = 0.0F;
    std::int32_t surface = kSurfaceDefault;
    std::int32_t min_x = 0;
    std::int32_t max_x = 0;
    std::int32_t min_z = 0;
    std::int32_t max_z = 0;

    // The game's own inside test (get_level_segment_waves), on whole units.
    bool contains(std::int32_t x, std::int32_t z) const;
    // Height of the plane at (x, z); the plane must not be vertical.
    float height(float x, float z) const { return -(a * x + c * z + d) / b; }
};

// A triangle as the level model stores it, before classification.
struct RawTriangle {
    std::array<std::array<std::int32_t, 3>, 3> vertices{};
    std::uint8_t flags = 0;       // Triangle.flags; 0x80 has no collision
    std::uint32_t batch_flags = 0; // TriangleBatchInfo.flags
    std::int32_t surface = -1;     // the texture's surfaceType; -1 without a texture
};

struct Segment {
    // LevelModelSegmentBoundingBox: x1, y1, z1, x2, y2, z2.
    std::array<std::int32_t, 6> bounds{};
    // Solid for the vehicle the geometry was built for.
    std::vector<Triangle> floors;
    std::vector<Triangle> walls;
    // The entries get_level_segment_waves sorts by height.
    std::vector<Triangle> waves;
    // LevelModelSegment.unk38: highest water batch vertex, -10000 without.
    float water_height = -10000.0F;
};

struct RawSegment {
    std::array<std::int32_t, 6> bounds{};
    std::vector<RawTriangle> triangles;
    float water_height = -10000.0F;
};

Triangle MakeTriangle(const std::array<std::array<std::int32_t, 3>, 3>& vertices, std::int32_t surface);

class LevelGeometry {
public:
    LevelGeometry() = default;
    LevelGeometry(const std::vector<RawSegment>& segments, Vehicle vehicle);

    Vehicle vehicle() const { return vehicle_; }
    const std::vector<Segment>& segments() const { return segments_; }

    // The floor (normal Y at least 0.45) at (x, z) whose height lies within
    // [below, above]; the one closest to `prefer` when several do.
    std::optional<std::pair<float, const Triangle*>> floor_near(float x, float z, float below, float above,
                                                                float prefer) const;
    // Every floor at (x, z), at any height.
    std::vector<std::pair<float, const Triangle*>> floors_at(float x, float z) const;
    // A wall (|normal Y| below 0.45) crossed front to back by the segment
    // from p to q.
    bool wall_between(const Vec3& p, const Vec3& q) const;
    // The water surface height a racer at `y` above (x, z) would float in, as
    // get_level_segment_waves and get_wave_properties decide it.
    std::optional<float> water_at(float x, float y, float z) const;

private:
    Vehicle vehicle_ = Vehicle::Car;
    std::vector<Segment> segments_;
};

// Full speed for the vehicle, by the game's physics: for cars the traction
// table (gSurfaceTractionTable 0.004) without stone's speed cap, for
// hovercraft every surface except stone.
bool full_speed(std::int32_t surface, Vehicle vehicle);
// A car's gSurfaceTractionTable entry; 0.01, the slowest, for an unknown
// surface.
float traction(std::int32_t surface);

struct EdgeParams {
    float range = 160.0F;      // how far the search reaches to each side
    float step = 4.0F;         // spacing of the samples
    float step_up = 24.0F;     // a rise the kart can still drive up
    float step_down = 48.0F;   // a fall that is still the same road
    float wall_probe = 12.0F;  // height above the road where walls are sought
};

struct Edges {
    // False when the kart's own ground cannot be found (airborne, or no
    // geometry): nothing is known.
    bool known = false;
    // The kart is already off the full-speed surface.
    bool past_edge = false;
    // Distance from the kart's centre to the edge on each side of its
    // heading, measured in the world (not mirrored); empty beyond the range.
    std::optional<float> left;
    std::optional<float> right;
};

// The track edge around a kart at `position` heading along `forward`
// (any length; only X and Z are used).
Edges find_edges(const LevelGeometry& geometry, const Vec3& position, const Vec3& forward,
                 const EdgeParams& params = {});

} // namespace dkr::race_cues
