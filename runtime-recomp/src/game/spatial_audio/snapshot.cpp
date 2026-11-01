#include "spatial_audio/snapshot.hpp"

#include <algorithm>
#include <cmath>

namespace dkr::runtime::spatial_audio {

Direction listener_direction(const Listener& listener, const Vec3& position) {
    // The game's camera faces (-sin yaw, 0, cos yaw) with its right at
    // (-cos yaw, 0, -sin yaw) (camera placement in racer.c; the same
    // left/right as audspat_calculate_spatial_pan).
    constexpr double kTurn = 2.0 * 3.14159265358979323846 / 65536.0;
    const double yaw = static_cast<double>(listener.yaw) * kTurn;
    const double cos_yaw = std::cos(yaw);
    const double sin_yaw = std::sin(yaw);
    const double dx = static_cast<double>(position.x) - listener.position.x;
    const double dy = static_cast<double>(position.y) - listener.position.y;
    const double dz = static_cast<double>(position.z) - listener.position.z;
    double right = -(dx * cos_yaw + dz * sin_yaw);
    if (listener.mirrored) right = -right;
    return Direction{static_cast<float>(right), static_cast<float>(dy),
                     static_cast<float>(dx * sin_yaw - dz * cos_yaw)};
}

Vec3 closest_point_on_segment(const Vec3& point, const Vec3& a, const Vec3& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float abz = b.z - a.z;
    const float length_squared = abx * abx + aby * aby + abz * abz;
    if (!(length_squared > 0.0F)) return a;
    const float along = ((point.x - a.x) * abx + (point.y - a.y) * aby + (point.z - a.z) * abz) /
                        length_squared;
    const float t = std::clamp(along, 0.0F, 1.0F);
    return Vec3{a.x + abx * t, a.y + aby * t, a.z + abz * t};
}

void SnapshotChannel::publish(const Snapshot& snapshot) {
    buffers_[write_] = snapshot;
    const std::uint8_t previous =
        middle_.exchange(static_cast<std::uint8_t>(write_ | kFresh), std::memory_order_acq_rel);
    write_ = previous & 3U;
}

bool SnapshotChannel::latest(Snapshot& out) {
    if ((middle_.load(std::memory_order_acquire) & kFresh) != 0) {
        const std::uint8_t previous = middle_.exchange(read_, std::memory_order_acq_rel);
        read_ = previous & 3U;
        published_ = true;
    }
    if (!published_) return false;
    out = buffers_[read_];
    return true;
}

} // namespace dkr::runtime::spatial_audio
