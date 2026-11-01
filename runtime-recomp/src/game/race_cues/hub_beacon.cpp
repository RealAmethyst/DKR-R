#include "race_cues/hub_beacon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace dkr::race_cues {
namespace {

namespace text = accessibility::text;
using text::Phrase;

// T.T.'s Character id (include/enums.h), for his name.
constexpr std::uint32_t kCharacterTT = 8;

// How far ahead along the route the sound comes from: the distance the kart
// covers in kAimSeconds, within these bounds, following changes of speed
// over kAimFollowSeconds so bumps and hops do not swing it.
constexpr float kAimSeconds = 0.4F;
constexpr float kAimMin = 120.0F;
constexpr float kAimMax = 400.0F;
constexpr double kAimFollowSeconds = 0.5;
// Off the route by more than this, or the destination moved this far since
// the route was planned (Taj walks about): plan again, at most once per
// kReplanSeconds.
constexpr float kOffRoute = 120.0F;
constexpr float kMovedReplan = 160.0F;
constexpr double kReplanSeconds = 1.0;
// How far ahead of its place the kart is looked for on the route.
constexpr float kFollowReach = 300.0F;
constexpr float kPi = 3.14159265F;

float Across(const Vec3& a, const Vec3& b) {
    return std::hypot(b.x - a.x, b.z - a.z);
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

} // namespace

namespace {

// Where a kind comes in the list.
int Rank(DestinationKind kind) {
    switch (kind) {
        case DestinationKind::World:
        case DestinationKind::Track: return 0;
        case DestinationKind::Boss: return 1;
        case DestinationKind::TrophyRace: return 2;
        case DestinationKind::Taj:
        case DestinationKind::TT: return 3;
        case DestinationKind::Balloon: return 4;
        case DestinationKind::Island: return 5;
    }
    return 5;
}

// Mod wording for what the game has no text for; empty for the rest.
std::string ModName(DestinationKind kind) {
    switch (kind) {
        case DestinationKind::Taj: return text::phrase(Phrase::Taj);
        case DestinationKind::TT: return text::character_name(kCharacterTT);
        case DestinationKind::Balloon: return text::phrase(Phrase::Balloon);
        case DestinationKind::Island: return text::phrase(Phrase::TimbersIsland);
        default: return {};
    }
}

} // namespace

std::vector<Destination> destination_list(HubContents contents, const Vec3& kart) {
    std::vector<Destination> list;
    std::stable_sort(contents.entrances.begin(), contents.entrances.end(), [](const Entrance& a, const Entrance& b) {
        if (Rank(a.kind) != Rank(b.kind)) return Rank(a.kind) < Rank(b.kind);
        return a.order != b.order ? a.order < b.order : a.level < b.level;
    });
    const auto add = [&](Destination destination) {
        const std::string mod = ModName(destination.kind);
        if (!mod.empty()) destination.name = mod;
        if (!destination.name.empty()) list.push_back(std::move(destination));
    };
    for (const Entrance& entrance : contents.entrances) {
        if (!entrance.open || Rank(entrance.kind) > Rank(DestinationKind::Boss)) continue;
        add({entrance.kind, entrance.level, entrance.name, entrance.position});
    }
    if (contents.trophy_race) add(*contents.trophy_race);
    if (contents.character) add(*contents.character);
    const auto distance = [&](const HubBalloon& balloon) {
        const float dx = balloon.position.x - kart.x, dy = balloon.position.y - kart.y,
                    dz = balloon.position.z - kart.z;
        return dx * dx + dy * dy + dz * dz;
    };
    std::stable_sort(contents.balloons.begin(), contents.balloons.end(), [&](const HubBalloon& a, const HubBalloon& b) {
        const float first = distance(a), second = distance(b);
        return first != second ? first < second : a.id < b.id;
    });
    for (const HubBalloon& balloon : contents.balloons) {
        add({DestinationKind::Balloon, balloon.id, {}, balloon.position});
    }
    for (const Entrance& entrance : contents.entrances) {
        if (entrance.open && entrance.kind == DestinationKind::Island) {
            add({entrance.kind, entrance.level, {}, entrance.position});
        }
    }
    return list;
}

float beacon_pitch(float distance) {
    const float closeness = 1.0F - std::clamp(distance / kBeaconFar, 0.0F, 1.0F);
    return std::exp2(kBeaconOctaves * closeness);
}

Phrase bearing(const Vec3& from, const Vec3& forward, const Vec3& to, bool mirrored) {
    constexpr std::array<Phrase, 8> kSectors{Phrase::Ahead,  Phrase::AheadRight, Phrase::Right, Phrase::BehindRight,
                                             Phrase::Behind, Phrase::BehindLeft, Phrase::Left,  Phrase::AheadLeft};
    const float length = std::hypot(forward.x, forward.z);
    const float fx = length > 0.0F ? forward.x / length : 0.0F;
    const float fz = length > 0.0F ? forward.z / length : 1.0F;
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    // Facing +Z, +X is left: the right of (fx, fz) is (-fz, fx).
    float right = dx * -fz + dz * fx;
    const float ahead = dx * fx + dz * fz;
    if (mirrored) right = -right;
    if (right == 0.0F && ahead == 0.0F) return Phrase::Ahead;
    const float degrees = std::atan2(right, ahead) * 180.0F / kPi;
    const auto sector = static_cast<int>(std::lround(degrees / 45.0F));
    return kSectors[static_cast<std::size_t>((sector % 8 + 8) % 8)];
}

void RouteFollower::set(std::vector<Vec3> points) {
    points_ = std::move(points);
    along_.assign(points_.size(), 0.0F);
    for (std::size_t index = 1; index < points_.size(); ++index) {
        along_[index] = along_[index - 1] + Across(points_[index - 1], points_[index]);
    }
    progress_ = 0.0F;
}

float RouteFollower::advance(const Vec3& kart) {
    if (points_.empty()) return std::numeric_limits<float>::infinity();
    if (points_.size() == 1) return Across(kart, points_[0]);
    float best_distance = std::numeric_limits<float>::infinity();
    float best_along = progress_;
    for (std::size_t index = 0; index + 1 < points_.size(); ++index) {
        if (along_[index + 1] < progress_) continue;
        if (along_[index] > progress_ + kFollowReach) break;
        const Vec3& a = points_[index];
        const Vec3& b = points_[index + 1];
        const float length = along_[index + 1] - along_[index];
        float t = 0.0F;
        if (length > 0.0F) {
            t = ((kart.x - a.x) * (b.x - a.x) + (kart.z - a.z) * (b.z - a.z)) / (length * length);
            t = std::clamp(t, 0.0F, 1.0F);
        }
        // Forward only: never behind the kart's place so far.
        const float along = std::max(along_[index] + t * length, progress_);
        const float within = length > 0.0F ? (along - along_[index]) / length : 0.0F;
        const float distance = Across(kart, Lerp(a, b, std::clamp(within, 0.0F, 1.0F)));
        if (distance < best_distance) {
            best_distance = distance;
            best_along = along;
        }
    }
    progress_ = std::min(best_along, along_.back());
    return best_distance;
}

Vec3 RouteFollower::aim(float ahead) const {
    if (points_.empty()) return {};
    const float target = std::min(progress_ + std::max(ahead, 0.0F), along_.back());
    for (std::size_t index = 0; index + 1 < points_.size(); ++index) {
        if (along_[index + 1] < target) continue;
        const float length = along_[index + 1] - along_[index];
        return Lerp(points_[index], points_[index + 1], length > 0.0F ? (target - along_[index]) / length : 1.0F);
    }
    return points_.back();
}

float RouteFollower::remaining() const {
    return points_.empty() ? 0.0F : along_.back() - progress_;
}

const Destination* HubBeacon::find(const std::vector<Destination>& list,
                                   const std::optional<Destination>& wanted) const {
    if (!wanted) return nullptr;
    const auto found =
        std::find_if(list.begin(), list.end(), [&](const Destination& entry) { return entry.same(*wanted); });
    return found == list.end() ? nullptr : &*found;
}

void HubBeacon::reset() {
    selected_.reset();
    guided_.reset();
    route_.clear();
    routed_ = false;
    ahead_ = 0.0F;
}

void HubBeacon::pick(const BeaconInput& input, int step, BeaconOutput& output) {
    const std::vector<Destination>& list = input.destinations;
    if (list.empty()) return;
    const Destination* current = find(list, selected_);
    std::size_t index = 0;
    if (current != nullptr) {
        const auto at = static_cast<std::ptrdiff_t>(current - list.data());
        const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(at + step, 0, static_cast<std::ptrdiff_t>(list.size()) - 1);
        // The ends of the list stop the choice, as the menus do.
        if (next == at) return;
        index = static_cast<std::size_t>(next);
    }
    selected_ = list[index];
    output.speech.push_back({text::item(list[index].name, index + 1, list.size()), true});
}

bool HubBeacon::replan(const BeaconInput& input, const Destination& target, const RoutePlanner& plan) {
    planned_at_ = input.seconds;
    planned_to_ = target.position;
    std::optional<std::vector<Vec3>> path = plan ? plan(input.kart, target.position) : std::nullopt;
    if (!path || path->empty()) {
        route_.clear();
        return false;
    }
    // The route ends at the nearest drivable point; the destination itself
    // closes it.
    path->push_back(target.position);
    route_.set(std::move(*path));
    route_.advance(input.kart);
    return true;
}

void HubBeacon::toggle(const BeaconInput& input, const RoutePlanner& plan, BeaconOutput& output) {
    const std::vector<Destination>& list = input.destinations;
    const Destination* target = find(list, selected_);
    if (target == nullptr) {
        if (list.empty()) return;
        target = &list.front();
    }
    selected_ = *target;
    if (guided_ && guided_->same(*target)) {
        reset();
        selected_ = *target;
        output.speech.push_back({text::phrase(Phrase::GuidanceOff), true});
        return;
    }
    guided_ = *target;
    ahead_ = 0.0F;
    routed_ = replan(input, *target, plan);
    output.speech.push_back({routed_ ? text::guiding(target->name) : text::no_route(target->name), true});
}

void HubBeacon::guide(const BeaconInput& input, const RoutePlanner& plan, BeaconOutput& output) {
    if (!guided_) return;
    // Arrived only when the destination itself is reached: a balloon
    // collected, Taj or T.T. turning to the player, the trophy cabinet's
    // menu. An entrance needs no word: driving through it leaves the hub.
    const bool collected = guided_->kind == DestinationKind::Balloon && guided_->id >= 0 && guided_->id < 16 &&
                           (input.course_flags & (0x10000U << guided_->id)) != 0;
    const Destination* target = find(input.destinations, guided_);
    if (collected || (target != nullptr && target->reached)) {
        output.speech.push_back({text::arrived(guided_->name), false});
        guided_.reset();
        route_.clear();
        routed_ = false;
        return;
    }
    if (target == nullptr) {
        // Closed or gone: nothing left to lead to.
        guided_.reset();
        route_.clear();
        routed_ = false;
        return;
    }
    guided_ = *target;
    const float off = routed_ ? route_.advance(input.kart) : std::numeric_limits<float>::infinity();
    const bool stale = !routed_ || off > kOffRoute || Across(planned_to_, target->position) > kMovedReplan;
    if (stale && input.seconds - planned_at_ >= kReplanSeconds) {
        const bool had = routed_;
        routed_ = replan(input, *target, plan);
        if (had && !routed_) output.speech.push_back({text::no_route(target->name), false});
    }
    if (!routed_) return;
    const float wanted = std::clamp(input.speed * kAimSeconds, kAimMin, kAimMax);
    const double elapsed = std::clamp(input.seconds - updated_at_, 0.0, 1.0);
    ahead_ = ahead_ <= 0.0F ? wanted
                            : ahead_ + (wanted - ahead_) * static_cast<float>(1.0 - std::exp(-elapsed / kAimFollowSeconds));
    updated_at_ = input.seconds;
    output.sounding = true;
    output.pitch = beacon_pitch(route_.remaining());
    output.aim = route_.aim(ahead_);
}

BeaconOutput HubBeacon::update(const BeaconInput& input, const RoutePlanner& plan) {
    BeaconOutput output;
    if ((input.pressed & kDpadLeft) != 0) pick(input, -1, output);
    if ((input.pressed & kDpadRight) != 0) pick(input, 1, output);
    if ((input.pressed & kDpadUp) != 0) toggle(input, plan, output);
    // D-pad Down with START is Quick Restart.
    if ((input.pressed & kDpadDown) != 0 && (input.held & kButtonStart) == 0) {
        const Destination* target = find(input.destinations, selected_);
        if (target == nullptr && !input.destinations.empty()) target = &input.destinations.front();
        if (target != nullptr) {
            output.speech.push_back(
                {text::direction(target->name, bearing(input.kart, input.forward, target->position, input.mirrored)),
                 true});
        }
    }
    guide(input, plan, output);
    return output;
}

} // namespace dkr::race_cues
