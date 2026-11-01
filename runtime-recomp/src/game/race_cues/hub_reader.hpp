#pragma once

#include "race_cues/level_geometry.hpp"

#include <cstdint>

// Reads a hub world for the hub beacon (hub_beacon.hpp) and plays it: the
// destinations from the game's objects, player 1's buttons, speech and the
// beacon sound. Game thread, called from race_reader's capture. Read-only.
namespace dkr::race_cues {

struct HubKart {
    Vec3 position;
    // The direction the kart drives (only X and Z are used).
    Vec3 forward;
    // Units per second.
    float speed = 0.0F;
    Vehicle vehicle = Vehicle::Car;
    bool mirrored = false;
    // The hub's world: 0 for Timber's Island, 1 to 4 for the world hubs.
    std::int32_t world = 0;
};

// Once per frame in a hub world. `geometry` (built for the kart's
// vehicle) and `geometry_key`, which changes with the level model or the
// vehicle, may be null and 0 when the geometry is not available.
void capture_hub(std::uint8_t* rdram, const HubKart& kart, const LevelGeometry* geometry,
                 std::uint64_t geometry_key);
// The beacon falls silent for now (a cutscene, a frame without the kart);
// what is picked and guided stays.
void quiet_hub();
// Out of the hub world: nothing picked, nothing guided.
void leave_hub();

} // namespace dkr::race_cues
