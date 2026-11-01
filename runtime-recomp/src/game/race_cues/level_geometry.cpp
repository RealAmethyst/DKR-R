#include "race_cues/level_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace dkr::race_cues {
namespace {

// TriangleBatchInfo.flags (RenderFlags, textures_sprites.h).
constexpr std::uint32_t kRenderHidden = 1U << 8;
constexpr std::uint32_t kRenderNoCollision = 1U << 9;
// Triangle.flags: TRI_FLAG_80 triangles get no collision plane.
constexpr std::uint8_t kTriangleNoCollision = 0x80;
// resolve_collisions: below this normal Y a surface is a wall.
constexpr float kWallNormalY = 0.45F;
// get_level_segment_waves fills at most 20 entries before the segment's own.
constexpr std::size_t kMaxWaveEntries = 20;

// gSurfaceTractionTable (racer.c).
constexpr std::array<float, kSurfaceCount> kTraction{
    0.004F, 0.007F, 0.01F,  0.004F, 0.01F,  0.01F,  0.01F,  0.01F,  0.01F,  0.01F,
    0.004F, 0.004F, 0.004F, 0.004F, 0.004F, 0.004F, 0.004F, 0.004F, 0.004F,
};

bool InsideXz(const std::array<std::int32_t, 6>& bounds, std::int32_t x, std::int32_t z, std::int32_t margin) {
    return x < bounds[3] + margin && bounds[0] - margin < x && z < bounds[5] + margin && bounds[2] - margin < z;
}

bool IsWater(std::int32_t surface) {
    return surface == kSurfaceWaterCalm || surface == kSurfaceWaterWavy;
}

// Moller-Trumbore, both faces, t within [0, 1].
bool SegmentHits(const Vec3& p, const Vec3& q, const Triangle& triangle) {
    const auto vertex = [&](std::size_t index) {
        return Vec3{static_cast<float>(triangle.vertices[index][0]), static_cast<float>(triangle.vertices[index][1]),
                    static_cast<float>(triangle.vertices[index][2])};
    };
    const Vec3 v0 = vertex(0);
    const Vec3 v1 = vertex(1);
    const Vec3 v2 = vertex(2);
    const Vec3 e1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
    const Vec3 e2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
    const Vec3 dir{q.x - p.x, q.y - p.y, q.z - p.z};
    const Vec3 h{dir.y * e2.z - dir.z * e2.y, dir.z * e2.x - dir.x * e2.z, dir.x * e2.y - dir.y * e2.x};
    const float det = e1.x * h.x + e1.y * h.y + e1.z * h.z;
    if (std::fabs(det) < 1e-6F) return false;
    const float inverse = 1.0F / det;
    const Vec3 s{p.x - v0.x, p.y - v0.y, p.z - v0.z};
    const float u = inverse * (s.x * h.x + s.y * h.y + s.z * h.z);
    if (u < 0.0F || u > 1.0F) return false;
    const Vec3 k{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
    const float v = inverse * (dir.x * k.x + dir.y * k.y + dir.z * k.z);
    if (v < 0.0F || u + v > 1.0F) return false;
    const float t = inverse * (e2.x * k.x + e2.y * k.y + e2.z * k.z);
    return t >= 0.0F && t <= 1.0F;
}

} // namespace

bool Triangle::contains(std::int32_t x, std::int32_t z) const {
    if (x < min_x || x > max_x || z < min_z || z > max_z) return false;
    const std::int64_t x1 = vertices[0][0], z1 = vertices[0][2];
    const std::int64_t x2 = vertices[1][0], z2 = vertices[1][2];
    const std::int64_t x3 = vertices[2][0], z3 = vertices[2][2];
    const bool side1 = ((x - x2) * (z3 - z2)) - ((x3 - x2) * (z - z2)) >= 0;
    const bool side2 = ((x - x1) * (z2 - z1)) - ((x2 - x1) * (z - z1)) >= 0;
    const bool side3 = ((x - x1) * (z3 - z1)) - ((x3 - x1) * (z - z1)) >= 0;
    return side1 == side2 && side2 != side3;
}

Triangle MakeTriangle(const std::array<std::array<std::int32_t, 3>, 3>& vertices, std::int32_t surface) {
    Triangle triangle;
    triangle.vertices = vertices;
    triangle.surface = surface;
    const float x1 = static_cast<float>(vertices[0][0]), y1 = static_cast<float>(vertices[0][1]),
                z1 = static_cast<float>(vertices[0][2]);
    const float x2 = static_cast<float>(vertices[1][0]), y2 = static_cast<float>(vertices[1][1]),
                z2 = static_cast<float>(vertices[1][2]);
    const float x3 = static_cast<float>(vertices[2][0]), y3 = static_cast<float>(vertices[2][1]),
                z3 = static_cast<float>(vertices[2][2]);
    // As track_init_collision.
    float nx = y1 * (z2 - z3) + y2 * (z3 - z1) + y3 * (z1 - z2);
    float ny = z1 * (x2 - x3) + z2 * (x3 - x1) + z3 * (x1 - x2);
    float nz = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2);
    const float magnitude = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (magnitude > 0.0F) {
        nx /= magnitude;
        ny /= magnitude;
        nz /= magnitude;
    }
    triangle.a = nx;
    triangle.b = ny;
    triangle.c = nz;
    triangle.d = -(x1 * nx + y1 * ny + z1 * nz);
    triangle.min_x = std::min({vertices[0][0], vertices[1][0], vertices[2][0]});
    triangle.max_x = std::max({vertices[0][0], vertices[1][0], vertices[2][0]});
    triangle.min_z = std::min({vertices[0][2], vertices[1][2], vertices[2][2]});
    triangle.max_z = std::max({vertices[0][2], vertices[1][2], vertices[2][2]});
    return triangle;
}

LevelGeometry::LevelGeometry(const std::vector<RawSegment>& segments, Vehicle vehicle) : vehicle_(vehicle) {
    segments_.reserve(segments.size());
    for (const RawSegment& raw : segments) {
        Segment segment;
        segment.bounds = raw.bounds;
        segment.water_height = raw.water_height;
        for (const RawTriangle& source : raw.triangles) {
            if ((source.flags & kTriangleNoCollision) != 0 || source.surface < 0) continue;
            const std::int32_t surface = source.surface;
            const Triangle triangle = MakeTriangle(source.vertices, surface);
            // generate_collision_candidates: what the vehicle collides with.
            const bool solid = (source.batch_flags & kRenderNoCollision) == 0 && surface != kSurfaceWaterCalm &&
                               !(vehicle == Vehicle::Plane && surface == kSurfaceInvisWall) &&
                               !(vehicle != Vehicle::Car && surface == kSurfaceUnk12);
            if (solid && triangle.b >= kWallNormalY) segment.floors.push_back(triangle);
            if (solid && std::fabs(triangle.b) < kWallNormalY) segment.walls.push_back(triangle);
            // get_level_segment_waves: water batches, and every visible solid batch.
            const bool water_batch = surface == kSurfaceWaterCalm || surface == kSurfaceWaterUnkF;
            if ((water_batch || (source.batch_flags & (kRenderHidden | kRenderNoCollision)) == 0) &&
                triangle.b != 0.0F) {
                segment.waves.push_back(triangle);
            }
        }
        segments_.push_back(std::move(segment));
    }
}

std::optional<std::pair<float, const Triangle*>> LevelGeometry::floor_near(float x, float z, float below, float above,
                                                                           float prefer) const {
    const auto ix = static_cast<std::int32_t>(x);
    const auto iz = static_cast<std::int32_t>(z);
    std::optional<std::pair<float, const Triangle*>> best;
    for (const Segment& segment : segments_) {
        if (!InsideXz(segment.bounds, ix, iz, 4)) continue;
        for (const Triangle& triangle : segment.floors) {
            if (!triangle.contains(ix, iz)) continue;
            const float y = triangle.height(x, z);
            if (y < below || y > above) continue;
            if (!best || std::fabs(y - prefer) < std::fabs(best->first - prefer)) best = {{y, &triangle}};
        }
    }
    return best;
}

std::vector<std::pair<float, const Triangle*>> LevelGeometry::floors_at(float x, float z) const {
    const auto ix = static_cast<std::int32_t>(x);
    const auto iz = static_cast<std::int32_t>(z);
    std::vector<std::pair<float, const Triangle*>> found;
    for (const Segment& segment : segments_) {
        if (!InsideXz(segment.bounds, ix, iz, 4)) continue;
        for (const Triangle& triangle : segment.floors) {
            if (triangle.contains(ix, iz)) found.emplace_back(triangle.height(x, z), &triangle);
        }
    }
    return found;
}

bool LevelGeometry::wall_between(const Vec3& p, const Vec3& q) const {
    const float min_x = std::min(p.x, q.x), max_x = std::max(p.x, q.x);
    const float min_z = std::min(p.z, q.z), max_z = std::max(p.z, q.z);
    for (const Segment& segment : segments_) {
        if (min_x > static_cast<float>(segment.bounds[3] + 5) || max_x < static_cast<float>(segment.bounds[0] - 5) ||
            min_z > static_cast<float>(segment.bounds[5] + 5) || max_z < static_cast<float>(segment.bounds[2] - 5)) {
            continue;
        }
        for (const Triangle& wall : segment.walls) {
            if (static_cast<float>(wall.max_x) < min_x || static_cast<float>(wall.min_x) > max_x ||
                static_cast<float>(wall.max_z) < min_z || static_cast<float>(wall.min_z) > max_z) {
                continue;
            }
            // resolve_collisions only stops movement from a wall's front to
            // its back.
            if (wall.a * (q.x - p.x) + wall.b * (q.y - p.y) + wall.c * (q.z - p.z) >= 0.0F) continue;
            if (SegmentHits(p, q, wall)) return true;
        }
    }
    return false;
}

std::optional<float> LevelGeometry::water_at(float x, float y, float z) const {
    struct Entry {
        std::int32_t type;
        float height;
        float normal_y;
    };
    const auto ix = static_cast<std::int32_t>(x);
    const auto iz = static_cast<std::int32_t>(z);
    // get_inside_segment_count_xz; none, or 8 and more, give no entries.
    std::size_t inside = 0;
    for (const Segment& segment : segments_) {
        if (InsideXz(segment.bounds, ix, iz, 4)) ++inside;
    }
    if (inside == 0 || inside >= 8) return std::nullopt;
    std::vector<Entry> entries;
    for (const Segment& segment : segments_) {
        if (!InsideXz(segment.bounds, ix, iz, 4)) continue;
        for (const Triangle& triangle : segment.waves) {
            if (entries.size() >= kMaxWaveEntries) break;
            if (triangle.contains(ix, iz)) entries.push_back({triangle.surface, triangle.height(x, z), triangle.b});
        }
    }
    // The racer's own segment (get_level_segment_index_from_position) adds its
    // water level as a wavy entry.
    const Segment* own = nullptr;
    std::int32_t closest = 1000000;
    const auto iy = static_cast<std::int32_t>(y);
    for (const Segment& segment : segments_) {
        if (!InsideXz(segment.bounds, ix, iz, 0)) continue;
        const std::int32_t difference = std::abs(iy - ((segment.bounds[4] + segment.bounds[1]) >> 1));
        if (difference < closest) {
            closest = difference;
            own = &segment;
        }
    }
    if (own != nullptr) entries.push_back({kSurfaceWaterWavy, own->water_height, 1.0F});
    // get_wave_properties.
    const auto water = static_cast<std::size_t>(
        std::count_if(entries.begin(), entries.end(), [](const Entry& entry) { return IsWater(entry.type); }));
    if (water == 0) return std::nullopt;
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& first, const Entry& second) { return first.height > second.height; });
    std::optional<std::size_t> index;
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
        const float height = entries[entry].height;
        if (IsWater(entries[entry].type)) {
            if (y < height + 25.0F && (entries[entry].normal_y > 0.5F || water == 1)) index = entry;
        } else if (index && water >= 2 && y < height - 20.0F) {
            index.reset();
        }
    }
    if (!index) return std::nullopt;
    return entries[*index].height;
}

float traction(std::int32_t surface) {
    if (surface < 0 || surface >= kSurfaceCount) return 0.01F;
    return kTraction[static_cast<std::size_t>(surface)];
}

bool full_speed(std::int32_t surface, Vehicle vehicle) {
    if (surface < 0 || surface >= kSurfaceCount) return false;
    if (vehicle == Vehicle::Hovercraft) return surface != kSurfaceStone;
    return kTraction[static_cast<std::size_t>(surface)] <= 0.004F && surface != kSurfaceStone;
}

namespace {

struct Ground {
    float y;
    bool fast;
};

// The road under (x, z) continuous with `previous`, with water for the
// vehicle: hovercraft float on it at full speed; cars in it are past the edge.
std::optional<Ground> GroundAt(const LevelGeometry& geometry, float x, float z, float previous, float ride,
                               float below, float above) {
    const auto floor = geometry.floor_near(x, z, previous - below, previous + above, previous);
    if (geometry.vehicle() == Vehicle::Hovercraft) {
        const std::optional<float> water = geometry.water_at(x, previous + ride, z);
        if (water && (!floor || *water >= floor->first)) return Ground{*water, true};
    }
    if (!floor) return std::nullopt;
    bool fast = full_speed(floor->second->surface, geometry.vehicle());
    if (fast && geometry.vehicle() == Vehicle::Car) {
        // The racer floats once its height less 5 is below the water (racer.c).
        const float racer_y = floor->first + ride;
        const std::optional<float> water = geometry.water_at(x, racer_y, z);
        if (water && racer_y - 5.0F < *water) fast = false;
    }
    return Ground{floor->first, fast};
}

} // namespace

Edges find_edges(const LevelGeometry& geometry, const Vec3& position, const Vec3& forward, const EdgeParams& params) {
    Edges edges;
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (length <= 0.0F || geometry.vehicle() == Vehicle::Plane) return edges;
    const float fx = forward.x / length;
    const float fz = forward.z / length;
    // Facing +Z, +X is left: the right of (fx, fz) is (-fz, fx).
    const float right_x = -fz;
    const float right_z = fx;

    // The kart's own ground: the floor just below it.
    const auto own = geometry.floor_near(position.x, position.z, position.y - 40.0F, position.y + 8.0F, position.y);
    float ground_y = 0.0F;
    bool fast = false;
    if (geometry.vehicle() == Vehicle::Hovercraft) {
        const std::optional<float> water = geometry.water_at(position.x, position.y, position.z);
        if (water && (!own || *water >= own->first)) {
            ground_y = *water;
            fast = true;
        } else if (own) {
            ground_y = own->first;
            fast = full_speed(own->second->surface, geometry.vehicle());
        } else {
            return edges;
        }
    } else {
        if (!own) return edges;
        ground_y = own->first;
        const auto ground = GroundAt(geometry, position.x, position.z, ground_y, position.y - ground_y, 1.0F, 1.0F);
        fast = ground && ground->fast;
    }
    edges.known = true;
    if (!fast) {
        edges.past_edge = true;
        return edges;
    }
    const float ride = std::clamp(position.y - ground_y, 0.0F, 40.0F);

    const auto search = [&](float side) -> std::optional<float> {
        float previous = ground_y;
        Vec3 previous_point{position.x, ground_y + params.wall_probe, position.z};
        for (float distance = params.step; distance <= params.range; distance += params.step) {
            const float x = position.x + side * right_x * distance;
            const float z = position.z + side * right_z * distance;
            const auto ground = GroundAt(geometry, x, z, previous, ride, params.step_down, params.step_up);
            if (!ground) return distance - params.step * 0.5F;
            const Vec3 point{x, ground->y + params.wall_probe, z};
            if (geometry.wall_between(previous_point, point) || !ground->fast) {
                return distance - params.step * 0.5F;
            }
            previous = ground->y;
            previous_point = point;
        }
        return std::nullopt;
    };
    edges.right = search(1.0F);
    edges.left = search(-1.0F);
    return edges;
}

} // namespace dkr::race_cues
