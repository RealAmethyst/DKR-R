#include "race_cues/turn_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace dkr::race_cues {
namespace {

// A change of heading at a gate from this angle on counts as a bend when the
// course has no arrows.
constexpr float kBendDegrees = 30.0F;
// Slower than this, the time to a bend means nothing (stopped, reversing).
constexpr float kMinimumSpeed = 30.0F;

std::optional<TurnSide> ArrowSide(std::uint8_t arrow) {
    if (arrow >= 1 && arrow <= 3) return TurnSide::Left;
    if (arrow >= 4 && arrow <= 6) return TurnSide::Right;
    return std::nullopt;
}

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A run of gates turning the same way is one bend, at its first gate.
std::vector<Bend> Runs(const std::vector<std::optional<TurnSide>>& sides) {
    std::vector<Bend> bends;
    const std::size_t count = sides.size();
    for (std::size_t gate = 0; gate < count; ++gate) {
        const std::optional<TurnSide>& previous = sides[(gate + count - 1) % count];
        if (sides[gate] && previous != sides[gate]) bends.push_back({gate, *sides[gate]});
    }
    // Every gate turning the same way: one bend.
    if (bends.empty() && count != 0 && sides[0]) bends.push_back({0, *sides[0]});
    return bends;
}

} // namespace

std::vector<Bend> find_bends(const std::vector<Gate>& gates) {
    const std::size_t count = gates.size();
    std::vector<std::optional<TurnSide>> sides(count);
    bool arrows = false;
    for (std::size_t gate = 0; gate < count; ++gate) {
        sides[gate] = ArrowSide(gates[gate].arrow);
        arrows = arrows || sides[gate].has_value();
    }
    if (!arrows && count >= 3) {
        // The heading from gate k-1 to k against k to k+1. Facing +Z, +X is
        // left, so a positive cross product (t1.x t2.z - t1.z t2.x) turns right.
        const float threshold = std::sin(kBendDegrees * 3.14159265F / 180.0F);
        for (std::size_t gate = 0; gate < count; ++gate) {
            const Gate& before = gates[(gate + count - 1) % count];
            const Gate& here = gates[gate];
            const Gate& after = gates[(gate + 1) % count];
            float ax = here.position.x - before.position.x, az = here.position.z - before.position.z;
            float bx = after.position.x - here.position.x, bz = after.position.z - here.position.z;
            const float la = std::sqrt(ax * ax + az * az), lb = std::sqrt(bx * bx + bz * bz);
            if (la <= 0.0F || lb <= 0.0F) continue;
            ax /= la, az /= la, bx /= lb, bz /= lb;
            const float cross = ax * bz - az * bx;
            const float dot = ax * bx + az * bz;
            if (dot < 0.0F || std::fabs(cross) >= threshold) {
                sides[gate] = cross >= 0.0F ? TurnSide::Right : TurnSide::Left;
            }
        }
    }
    return Runs(sides);
}

void TurnTracker::set_course(std::vector<Gate> gates) {
    gates_ = std::move(gates);
    bends_ = find_bends(gates_);
    reset();
}

void TurnTracker::reset() {
    upcoming_.reset();
    gates_ahead_ = 0;
    signalled_ = false;
}

std::optional<TurnSignal> TurnTracker::update(const Progress& progress, float lead_seconds) {
    const std::size_t count = gates_.size();
    if (count < 2 || bends_.empty() || progress.next_gate >= count) {
        reset();
        return std::nullopt;
    }
    // The first bend at or after the next gate.
    std::size_t ahead = 0;
    std::size_t gates_ahead = count;
    for (std::size_t index = 0; index < bends_.size(); ++index) {
        const std::size_t offset = (bends_[index].gate + count - progress.next_gate) % count;
        if (offset < gates_ahead) {
            gates_ahead = offset;
            ahead = index;
        }
    }
    // A new bend ahead, or the same one a lap later (the only bend, passed).
    if (upcoming_ != ahead || gates_ahead > gates_ahead_) signalled_ = false;
    upcoming_ = ahead;
    gates_ahead_ = gates_ahead;
    if (signalled_ || progress.quiet || progress.speed < kMinimumSpeed) return std::nullopt;

    // What is left of the current stretch, then whole stretches to the bend.
    const std::size_t previous = (progress.next_gate + count - 1) % count;
    float distance = std::clamp(progress.fraction, 0.0F, 1.0F) *
                     Distance(gates_[previous].position, gates_[progress.next_gate].position);
    for (std::size_t step = 0; step < gates_ahead; ++step) {
        const std::size_t from = (progress.next_gate + step) % count;
        distance += Distance(gates_[from].position, gates_[(from + 1) % count].position);
    }
    if (distance / progress.speed > lead_seconds) return std::nullopt;
    signalled_ = true;
    const Bend& bend = bends_[ahead];
    return TurnSignal{bend.side, gates_[bend.gate].position};
}

} // namespace dkr::race_cues
