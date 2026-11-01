#pragma once

#include "race_cues/level_geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Drivable routes over a level's collision geometry for the hub beacon: a
// 2.5D grid of the floors (one node per floor height in each cell), joined
// where a kart can drive from one cell to the next. The rules follow
// LevelGeometry, which mirrors the game's collision: floors are normal Y 0.45
// and up, walls stop movement only from their front, and water is decided as
// get_level_segment_waves and get_wave_properties do. Planes are not routed.
//
// Routes are the quickest by the game's own traction, keep away from walls,
// drops and slow ground where there is room (a clearance cost from a
// distance transform over the grid), and are straightened where a straight
// line keeps to the same rules.
namespace dkr::race_cues {

struct RouteParams {
    // Grid spacing in world units.
    float cell = 40.0F;
    // Height above the floor at which walls are sought between two points.
    float probe = 14.0F;
    // Rise per unit across: resolve_collisions drives on normals of Y 0.707
    // and up, which is 45 degrees.
    float slope = 1.0F;
    // A step up or down beyond the slope that is still drivable (EdgeParams).
    float step_up = 24.0F;
    float step_down = 48.0F;
    // Floors this close in one cell are one floor.
    float merge = 8.0F;
    // Ground nearer than `clearance` to a wall, a drop, a gap or slow ground
    // costs up to 1 + near_cost times as much, the nearer the more.
    float clearance = 160.0F;
    float near_cost = 2.0F;
    // Straighten routes where a straight line keeps to the rules.
    bool straighten = true;
};

class RouteGraph {
public:
    RouteGraph() = default;
    // Builds the grid for the geometry's vehicle (car or hovercraft).
    RouteGraph(const LevelGeometry& geometry, const RouteParams& params = {});

    std::size_t nodes() const { return nodes_.size(); }
    std::size_t memory_bytes() const;
    const RouteParams& params() const { return params_; }

    // A route from `from` to `to`, starting at `from`'s node and ending at
    // the node nearest `to`; empty when either has no node within reach or
    // the two are not connected. `start_reach` and `end_reach` are horizontal
    // distances in world units.
    std::optional<std::vector<Vec3>> route(const Vec3& from, const Vec3& to, float start_reach,
                                           float end_reach) const;

private:
    struct Node {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        bool wet = false;
        // Time per unit against full-speed ground (1 or more); slow ground
        // is more than 1.
        float pace = 1.0F;
        // Distance along the grid to the nearest wall, drop, gap or slow
        // ground, up to RouteParams::clearance.
        float clearance = 0.0F;
        // The node reached in each of the eight directions, or -1.
        std::array<std::int32_t, 8> next{};
    };

    std::optional<std::uint32_t> nearest(const Vec3& point, float reach, float height_weight) const;
    std::optional<std::size_t> cell_index(std::int32_t column, std::int32_t row) const;
    const Node* node_at(float x, float y, float z) const;
    float cost(const Node& node) const;
    bool straight(const std::vector<std::uint32_t>& path, std::size_t from, std::size_t to) const;

    // A copy, for straightening routes.
    LevelGeometry geometry_;
    RouteParams params_;
    float origin_x_ = 0.0F;
    float origin_z_ = 0.0F;
    std::int32_t columns_ = 0;
    std::int32_t rows_ = 0;
    // Nodes in cell order; a cell's nodes are [first_[cell], first_[cell + 1]).
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> first_;
};

} // namespace dkr::race_cues
