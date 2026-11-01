#include "race_cues/hub_route.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace dkr::race_cues {
namespace {

constexpr std::array<std::array<std::int32_t, 2>, 8> kDirections{
    {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
// A kart's centre above the floor it stands on, for the water rules.
constexpr float kRide = 10.0F;
// The racer floats once its height less 5 is below the water (racer.c).
constexpr float kFloatDepth = 5.0F;
// Water with a floor this little above it is covered, not open: a racer
// cannot be there.
constexpr float kHeadroom = 60.0F;
// Keeps a grid within reason for corrupt bounds.
constexpr std::int64_t kMaxCells = 4000000;
// A straight line replaces at most this many grid steps, which bounds the
// work of straightening a route.
constexpr std::size_t kMaxStraight = 32;
// Traction while floating (racer.c, a buoyant racer's traction 0.02) and on
// the fastest ground (gSurfaceTractionTable).
constexpr float kFloatingTraction = 0.02F;
constexpr float kFullSpeedTraction = 0.004F;

bool IsWater(std::int32_t surface) {
    return surface == kSurfaceWaterCalm || surface == kSurfaceWaterWavy;
}

struct Sample {
    float y;
    bool wet;
    // Time per unit against full-speed ground.
    float pace;
};

// A car's speed settles where drag (velocity squared times the traction,
// racer.c) meets its thrust, so its top speed goes as one over the square
// root of the traction. Hovercraft are slowed by stone alone.
float Pace(float surface_traction) {
    return std::sqrt(surface_traction / kFullSpeedTraction);
}

float FloorPace(std::int32_t surface, Vehicle vehicle) {
    if (full_speed(surface, vehicle)) return 1.0F;
    return Pace(vehicle == Vehicle::Car ? traction(surface) : traction(kSurfaceStone));
}

// The heights a vehicle can be at above (x, z), lowest first.
std::vector<Sample> Samples(const LevelGeometry& geometry, float x, float z, float merge) {
    std::vector<Sample> samples;
    const Vehicle vehicle = geometry.vehicle();
    const bool hovercraft = vehicle == Vehicle::Hovercraft;
    const std::vector<std::pair<float, const Triangle*>> floors = geometry.floors_at(x, z);
    for (const auto& [y, triangle] : floors) {
        const std::optional<float> water = geometry.water_at(x, y + kRide, z);
        if (hovercraft && water && *water >= y) {
            samples.push_back({*water, true, 1.0F});
        } else if (water && y + kRide - kFloatDepth < *water) {
            // A car that floats is lifted towards the water's surface
            // (buoyancy, racer.c), so it meets walls and banks from there.
            samples.push_back({*water, true, Pace(kFloatingTraction)});
        } else {
            samples.push_back({y, false, FloorPace(triangle->surface, vehicle)});
        }
    }
    if (hovercraft) {
        // Water with no floor under it, such as the sea: the segment's water
        // level and the water triangles at the point.
        const auto ix = static_cast<std::int32_t>(x);
        const auto iz = static_cast<std::int32_t>(z);
        std::vector<float> candidates;
        for (const Segment& segment : geometry.segments()) {
            if (ix < segment.bounds[0] || ix > segment.bounds[3] || iz < segment.bounds[2] || iz > segment.bounds[5]) {
                continue;
            }
            if (segment.water_height > -10000.0F) candidates.push_back(segment.water_height);
            for (const Triangle& triangle : segment.waves) {
                if (IsWater(triangle.surface) && triangle.contains(ix, iz)) {
                    candidates.push_back(triangle.height(x, z));
                }
            }
        }
        for (const float height : candidates) {
            const bool covered = std::any_of(floors.begin(), floors.end(), [&](const auto& floor) {
                return floor.first >= height && floor.first < height + kHeadroom;
            });
            if (covered) continue;
            const std::optional<float> water = geometry.water_at(x, height + 1.0F, z);
            if (water && std::fabs(*water - height) < 1.0F) samples.push_back({height, true, 1.0F});
        }
    }
    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) { return a.y < b.y; });
    std::vector<Sample> merged;
    for (const Sample& sample : samples) {
        if (!merged.empty() && sample.y - merged.back().y < merge) continue;
        merged.push_back(sample);
    }
    return merged;
}

// Whether a kart can drive straight from a to b, a cell or less apart: a
// rise or fall it can take, something to drive on at the midpoint (a floor
// within reach of both, or water under the higher when one floats), and no
// wall in the way.
bool Passable(const LevelGeometry& geometry, const RouteParams& params, const Vec3& a, const Vec3& b, bool wet) {
    const float across = std::hypot(b.x - a.x, b.z - a.z);
    const float rise = b.y - a.y;
    if (rise > across * params.slope + params.step_up || -rise > across * params.slope + params.step_down) {
        return false;
    }
    const float low = std::min(a.y, b.y) - params.step_down - across * params.slope * 0.5F;
    const float high = std::max(a.y, b.y) + params.step_up;
    const float x = (a.x + b.x) * 0.5F;
    const float z = (a.z + b.z) * 0.5F;
    bool supported = false;
    for (const auto& [y, triangle] : geometry.floors_at(x, z)) {
        if (y >= low && y <= high) {
            supported = true;
            break;
        }
    }
    if (!supported && wet) {
        const std::optional<float> water = geometry.water_at(x, std::max(a.y, b.y) + 1.0F, z);
        supported = water && *water >= low;
    }
    if (!supported) return false;
    return !geometry.wall_between({a.x, a.y + params.probe, a.z}, {b.x, b.y + params.probe, b.z});
}

} // namespace

RouteGraph::RouteGraph(const LevelGeometry& geometry, const RouteParams& params)
    : geometry_(geometry), params_(params) {
    if (geometry.vehicle() == Vehicle::Plane || geometry.segments().empty() || params.cell <= 0.0F) return;
    std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
    std::int32_t min_z = min_x;
    std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
    std::int32_t max_z = max_x;
    for (const Segment& segment : geometry.segments()) {
        min_x = std::min(min_x, segment.bounds[0]);
        min_z = std::min(min_z, segment.bounds[2]);
        max_x = std::max(max_x, segment.bounds[3]);
        max_z = std::max(max_z, segment.bounds[5]);
    }
    if (max_x < min_x || max_z < min_z) return;
    const auto columns = static_cast<std::int64_t>(std::ceil(static_cast<float>(max_x - min_x) / params.cell)) + 1;
    const auto rows = static_cast<std::int64_t>(std::ceil(static_cast<float>(max_z - min_z) / params.cell)) + 1;
    if (columns * rows > kMaxCells) return;
    origin_x_ = static_cast<float>(min_x);
    origin_z_ = static_cast<float>(min_z);
    columns_ = static_cast<std::int32_t>(columns);
    rows_ = static_cast<std::int32_t>(rows);

    first_.assign(static_cast<std::size_t>(columns * rows) + 1, 0);
    for (std::int32_t row = 0; row < rows_; ++row) {
        for (std::int32_t column = 0; column < columns_; ++column) {
            const std::size_t cell = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) +
                                     static_cast<std::size_t>(column);
            first_[cell] = static_cast<std::uint32_t>(nodes_.size());
            const float x = origin_x_ + (static_cast<float>(column) + 0.5F) * params.cell;
            const float z = origin_z_ + (static_cast<float>(row) + 0.5F) * params.cell;
            for (const Sample& sample : Samples(geometry, x, z, params.merge)) {
                Node node;
                node.x = x;
                node.y = sample.y;
                node.z = z;
                node.wet = sample.wet;
                node.pace = sample.pace;
                node.next.fill(-1);
                nodes_.push_back(node);
            }
        }
    }
    first_.back() = static_cast<std::uint32_t>(nodes_.size());

    for (std::int32_t row = 0; row < rows_; ++row) {
        for (std::int32_t column = 0; column < columns_; ++column) {
            const std::size_t cell = *cell_index(column, row);
            for (std::uint32_t index = first_[cell]; index < first_[cell + 1]; ++index) {
                Node& node = nodes_[index];
                for (std::size_t direction = 0; direction < kDirections.size(); ++direction) {
                    const std::optional<std::size_t> other =
                        cell_index(column + kDirections[direction][0], row + kDirections[direction][1]);
                    if (!other) continue;
                    // The floor in the next cell nearest in height.
                    std::int32_t best = -1;
                    for (std::uint32_t candidate = first_[*other]; candidate < first_[*other + 1]; ++candidate) {
                        if (best < 0 || std::fabs(nodes_[candidate].y - node.y) <
                                            std::fabs(nodes_[static_cast<std::size_t>(best)].y - node.y)) {
                            best = static_cast<std::int32_t>(candidate);
                        }
                    }
                    if (best < 0) continue;
                    const Node& target = nodes_[static_cast<std::size_t>(best)];
                    if (Passable(geometry, params, {node.x, node.y, node.z}, {target.x, target.y, target.z},
                                 node.wet || target.wet)) {
                        node.next[direction] = best;
                    }
                }
            }
        }
    }

    // Clearance: a distance transform from every node that cannot go on in
    // some direction (a wall, a drop or a gap next to it) or is slow.
    std::vector<float> clearance(nodes_.size(), params.clearance);
    using Entry = std::pair<float, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    for (std::uint32_t index = 0; index < nodes_.size(); ++index) {
        const Node& node = nodes_[index];
        const bool edge = std::any_of(node.next.begin(), node.next.end(), [](std::int32_t next) { return next < 0; });
        if (edge || node.pace > 1.0F) {
            clearance[index] = 0.0F;
            open.push({0.0F, index});
        }
    }
    while (!open.empty()) {
        const auto [reached, index] = open.top();
        open.pop();
        if (reached > clearance[index]) continue;
        const Node& node = nodes_[index];
        for (const std::int32_t next : node.next) {
            if (next < 0) continue;
            const Node& target = nodes_[static_cast<std::size_t>(next)];
            const float total = reached + std::hypot(target.x - node.x, target.z - node.z);
            if (total >= clearance[static_cast<std::size_t>(next)]) continue;
            clearance[static_cast<std::size_t>(next)] = total;
            open.push({total, static_cast<std::uint32_t>(next)});
        }
    }
    for (std::size_t index = 0; index < nodes_.size(); ++index) nodes_[index].clearance = clearance[index];
}

std::size_t RouteGraph::memory_bytes() const {
    return nodes_.capacity() * sizeof(Node) + first_.capacity() * sizeof(std::uint32_t);
}

std::optional<std::size_t> RouteGraph::cell_index(std::int32_t column, std::int32_t row) const {
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_) return std::nullopt;
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) + static_cast<std::size_t>(column);
}

const RouteGraph::Node* RouteGraph::node_at(float x, float y, float z) const {
    const std::optional<std::size_t> cell =
        cell_index(static_cast<std::int32_t>(std::floor((x - origin_x_) / params_.cell)),
                   static_cast<std::int32_t>(std::floor((z - origin_z_) / params_.cell)));
    if (!cell) return nullptr;
    const Node* best = nullptr;
    for (std::uint32_t index = first_[*cell]; index < first_[*cell + 1]; ++index) {
        if (best == nullptr || std::fabs(nodes_[index].y - y) < std::fabs(best->y - y)) best = &nodes_[index];
    }
    return best;
}

float RouteGraph::cost(const Node& node) const {
    const float near = params_.clearance > 0.0F ? 1.0F - std::min(node.clearance / params_.clearance, 1.0F) : 0.0F;
    return node.pace * (1.0F + params_.near_cost * near);
}

std::optional<std::uint32_t> RouteGraph::nearest(const Vec3& point, float reach, float height_weight) const {
    if (nodes_.empty()) return std::nullopt;
    const auto centre_column = static_cast<std::int32_t>(std::floor((point.x - origin_x_) / params_.cell));
    const auto centre_row = static_cast<std::int32_t>(std::floor((point.z - origin_z_) / params_.cell));
    const auto span = static_cast<std::int32_t>(std::ceil(reach / params_.cell));
    std::optional<std::uint32_t> best;
    float best_score = 0.0F;
    for (std::int32_t row = centre_row - span; row <= centre_row + span; ++row) {
        for (std::int32_t column = centre_column - span; column <= centre_column + span; ++column) {
            const std::optional<std::size_t> cell = cell_index(column, row);
            if (!cell) continue;
            for (std::uint32_t index = first_[*cell]; index < first_[*cell + 1]; ++index) {
                const Node& node = nodes_[index];
                const float across = std::hypot(node.x - point.x, node.z - point.z);
                if (across > reach) continue;
                const float score = across + height_weight * std::fabs(node.y - point.y);
                if (!best || score < best_score) {
                    best = index;
                    best_score = score;
                }
            }
        }
    }
    return best;
}

// Whether the straight line from path[from] to path[to] keeps to the grid's
// rules: every step of a cell or less passable (with the floor nearest in
// height at each point), at each point no nearer to a wall, drop or slow
// ground than the grid route about as far along it, and no slower or wet
// ground than the grid route crossed.
bool RouteGraph::straight(const std::vector<std::uint32_t>& path, std::size_t from, std::size_t to) const {
    float pace = 1.0F;
    bool wet = false;
    for (std::size_t index = from; index <= to; ++index) {
        const Node& node = nodes_[path[index]];
        pace = std::max(pace, node.pace);
        wet = wet || node.wet;
    }
    // The grid route's clearance around the point a fraction t along it.
    const auto grid_clearance = [&](float t) {
        const auto at = from + static_cast<std::size_t>(std::lround(t * static_cast<float>(to - from)));
        float clearance = nodes_[path[at]].clearance;
        if (at > from) clearance = std::min(clearance, nodes_[path[at - 1]].clearance);
        if (at < to) clearance = std::min(clearance, nodes_[path[at + 1]].clearance);
        return clearance;
    };
    const Node& start = nodes_[path[from]];
    const Node& end = nodes_[path[to]];
    const float length = std::hypot(end.x - start.x, end.z - start.z);
    const auto steps = static_cast<int>(std::ceil(length / params_.cell));
    Vec3 previous{start.x, start.y, start.z};
    bool previous_wet = start.wet;
    for (int step = 1; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        Vec3 point{start.x + (end.x - start.x) * t, end.y, start.z + (end.z - start.z) * t};
        bool point_wet = end.wet;
        float point_pace = end.pace;
        if (step < steps) {
            std::optional<Sample> best;
            for (const Sample& sample : Samples(geometry_, point.x, point.z, params_.merge)) {
                if (!best || std::fabs(sample.y - previous.y) < std::fabs(best->y - previous.y)) best = sample;
            }
            if (!best) return false;
            point.y = best->y;
            point_wet = best->wet;
            point_pace = best->pace;
        }
        if (point_pace > pace || (point_wet && !wet)) return false;
        if (!Passable(geometry_, params_, previous, point, previous_wet || point_wet)) return false;
        const Node* cell = node_at(point.x, point.y, point.z);
        if (cell == nullptr || cell->clearance + 0.5F < grid_clearance(t)) return false;
        previous = point;
        previous_wet = point_wet;
    }
    return true;
}

std::optional<std::vector<Vec3>> RouteGraph::route(const Vec3& from, const Vec3& to, float start_reach,
                                                   float end_reach) const {
    // The kart stands on its floor; a destination object may float well
    // above its own.
    const std::optional<std::uint32_t> start = nearest(from, start_reach, 2.0F);
    const std::optional<std::uint32_t> goal = nearest(to, end_reach, 1.0F);
    if (!start || !goal) return std::nullopt;
    const Node& end = nodes_[*goal];
    std::vector<float> spent(nodes_.size(), std::numeric_limits<float>::infinity());
    std::vector<std::int32_t> previous(nodes_.size(), -1);
    using Entry = std::pair<float, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    spent[*start] = 0.0F;
    open.push({std::hypot(end.x - nodes_[*start].x, end.z - nodes_[*start].z), *start});
    bool found = false;
    while (!open.empty()) {
        const auto [estimate, index] = open.top();
        open.pop();
        if (index == *goal) {
            found = true;
            break;
        }
        const Node& node = nodes_[index];
        if (estimate - std::hypot(end.x - node.x, end.z - node.z) > spent[index] + 0.01F) continue;
        for (const std::int32_t next : node.next) {
            if (next < 0) continue;
            const Node& target = nodes_[static_cast<std::size_t>(next)];
            // Every cost factor is 1 or more, so the straight distance left
            // never overestimates.
            const float total = spent[index] + std::hypot(target.x - node.x, target.z - node.z) * cost(target);
            if (total >= spent[static_cast<std::size_t>(next)]) continue;
            spent[static_cast<std::size_t>(next)] = total;
            previous[static_cast<std::size_t>(next)] = static_cast<std::int32_t>(index);
            open.push({total + std::hypot(end.x - target.x, end.z - target.z), static_cast<std::uint32_t>(next)});
        }
    }
    if (!found) return std::nullopt;
    std::vector<std::uint32_t> path;
    for (std::int32_t at = static_cast<std::int32_t>(*goal); at >= 0; at = previous[static_cast<std::size_t>(at)]) {
        path.push_back(static_cast<std::uint32_t>(at));
    }
    std::reverse(path.begin(), path.end());
    // From each kept point, the furthest point a straight line reaches.
    std::vector<std::size_t> kept{0};
    if (params_.straighten) {
        for (std::size_t at = 0; at + 1 < path.size();) {
            std::size_t reach = at + 1;
            for (std::size_t next = at + 2; next < path.size() && next - at <= kMaxStraight; ++next) {
                if (!straight(path, at, next)) break;
                reach = next;
            }
            kept.push_back(reach);
            at = reach;
        }
    } else {
        for (std::size_t index = 1; index < path.size(); ++index) kept.push_back(index);
    }
    std::vector<Vec3> points;
    points.reserve(kept.size());
    for (const std::size_t index : kept) {
        const Node& node = nodes_[path[index]];
        points.push_back({node.x, node.y, node.z});
    }
    return points;
}

} // namespace dkr::race_cues
