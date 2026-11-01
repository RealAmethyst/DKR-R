#pragma once

#include "accessibility/localization.hpp"
#include "accessibility/menu_speech_policy.hpp"
#include "race_cues/level_geometry.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The hub beacon: the player picks a destination in a hub world (Timber's
// Island or one of the four world hubs) with the D-pad and a continuous sound
// leads along a drivable route to it, higher the closer it is. Game-independent: the reader in
// hub_reader.cpp supplies the destinations, the kart and the buttons.
// World axes as LevelGeometry: +Y up; facing +Z, +X is left.
namespace dkr::race_cues {

using accessibility::Utterance;

enum class DestinationKind {
    World,      // Timber's Island: the entrance to a world's hub
    Track,      // a world hub: the entrance to a race
    Boss,       // a world hub: the boss door's live entrance
    TrophyRace, // a world hub: the trophy cabinet
    Taj,
    TT,         // T.T., in the world hubs
    Balloon,
    Island,     // a world hub: the way back to Timber's Island
};

struct Destination {
    DestinationKind kind = DestinationKind::World;
    // World, Track, Boss and Island: the level the entrance leads to;
    // Balloon: its balloon id.
    std::int32_t id = 0;
    // As spoken: the game's own text for worlds, tracks, the boss door and
    // the trophy race, mod wording otherwise.
    std::string name;
    Vec3 position;
    // The game's own meeting with it has begun: Taj or T.T. turning to the
    // player, the trophy cabinet's menu.
    bool reached = false;

    bool same(const Destination& other) const { return kind == other.kind && id == other.id; }
};

// An entrance as the hub holds it (a BHV_EXIT with a gate or door in front).
struct Entrance {
    DestinationKind kind = DestinationKind::World;
    std::int32_t level = 0;
    // Lists are in this order: a world's id (enums.h) for world entrances,
    // the balloons its door first asked for (the game's unlock order) for
    // tracks.
    std::int32_t order = 0;
    // The game's level name for worlds and tracks, the boss door's game
    // text for the boss; empty when it could not be read.
    std::string name;
    Vec3 position;
    // False while a gate or door in front of it is closed.
    bool open = true;
};

struct HubBalloon {
    std::int32_t id = 0;
    Vec3 position;
};

struct HubContents {
    std::vector<Entrance> entrances;
    // Taj or T.T. where they are now; the reader leaves the name empty.
    std::optional<Destination> character;
    // The trophy cabinet, only while the trophy race is open, named with the
    // game's text.
    std::optional<Destination> trophy_race;
    std::vector<HubBalloon> balloons;
};

// The destinations to pick from, in order: open world or track entrances
// (in their order), the boss, the trophy race, Taj or T.T., the balloons
// (nearest to the kart first), and the way back to the island. Destinations
// whose game text is unknown are left out rather than spoken wrongly.
std::vector<Destination> destination_list(HubContents contents, const Vec3& kart);

// The beacon's playback rate for the distance left along the route: its own
// pitch from kBeaconFar on, rising evenly in pitch (the same interval for
// every equal step closer) to kBeaconOctaves higher at the destination. Set
// by ear.
constexpr float kBeaconFar = 4000.0F;
constexpr float kBeaconOctaves = 1.0F;
float beacon_pitch(float distance);

// The direction of `to` from a kart at `from` heading along `forward`, in
// eight 45-degree sectors centred on ahead, right, behind and left.
// `mirrored` (the Mirrored Tracks cheat) swaps left and right.
accessibility::text::Phrase bearing(const Vec3& from, const Vec3& forward, const Vec3& to, bool mirrored);

// A route as a polyline, followed forward only.
class RouteFollower {
public:
    void set(std::vector<Vec3> points);
    void clear() { set({}); }
    bool empty() const { return points_.empty(); }
    // Moves the kart's place on the route forward to its nearest point
    // within reach ahead; returns the kart's horizontal distance from the
    // route there.
    float advance(const Vec3& kart);
    // The point `ahead` units (horizontally) along the route from the
    // kart's place; the route's end beyond it.
    Vec3 aim(float ahead) const;
    // Horizontal length left from the kart's place to the end.
    float remaining() const;
    float progress() const { return progress_; }

private:
    std::vector<Vec3> points_;
    std::vector<float> along_;
    float progress_ = 0.0F;
};

// N64 controller buttons (os_cont.h).
constexpr std::uint16_t kButtonStart = 0x1000;
constexpr std::uint16_t kDpadUp = 0x0800;
constexpr std::uint16_t kDpadDown = 0x0400;
constexpr std::uint16_t kDpadLeft = 0x0200;
constexpr std::uint16_t kDpadRight = 0x0100;

struct BeaconInput {
    // Player 1's buttons newly pressed this frame, and held.
    std::uint16_t pressed = 0;
    std::uint16_t held = 0;
    Vec3 kart;
    Vec3 forward;
    // Units per second.
    float speed = 0.0F;
    bool mirrored = false;
    double seconds = 0.0;
    std::vector<Destination> destinations;
    // A balloon is reached once its course flag (0x10000 << its id) is set.
    std::uint32_t course_flags = 0;
};

struct BeaconOutput {
    std::vector<Utterance> speech;
    bool sounding = false;
    // Where the sound comes from, and its playback rate.
    Vec3 aim;
    float pitch = 1.0F;
};

// A route from the kart to a destination, or nothing when there is none.
using RoutePlanner = std::function<std::optional<std::vector<Vec3>>(const Vec3& from, const Vec3& to)>;

class HubBeacon {
public:
    BeaconOutput update(const BeaconInput& input, const RoutePlanner& plan);
    // Leaving the hub: nothing picked, nothing guided.
    void reset();
    bool guiding() const { return guided_.has_value(); }

private:
    std::optional<Destination> selected_;
    std::optional<Destination> guided_;
    RouteFollower route_;
    bool routed_ = false;
    // How far ahead the sound is, following the kart's speed smoothly.
    float ahead_ = 0.0F;
    double updated_at_ = 0.0;
    double planned_at_ = 0.0;
    Vec3 planned_to_;

    const Destination* find(const std::vector<Destination>& list, const std::optional<Destination>& wanted) const;
    void pick(const BeaconInput& input, int step, BeaconOutput& output);
    void toggle(const BeaconInput& input, const RoutePlanner& plan, BeaconOutput& output);
    bool replan(const BeaconInput& input, const Destination& target, const RoutePlanner& plan);
    void guide(const BeaconInput& input, const RoutePlanner& plan, BeaconOutput& output);
};

} // namespace dkr::race_cues
