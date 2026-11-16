#pragma once

#include <cmath>
#include <cstdint>

namespace gte {

// Owner and name of the channel that publishes the shared wind, once per view, before any opaque pass.
inline constexpr const char* kWindStateOwner = "VolumetricCloud";
inline constexpr const char* kWindStateChannel = "Wind";

// The wind the clouds drift with. Direction 0 = +X, 90 = +Z (same as the cloud setting).
struct WindState {
    // Unit vector on the ground plane: where the air goes.
    float directionX = 0.7071068f;
    float directionZ = 0.7071068f;
    float speed = 4.0f; // m/s.
    // Distance the pattern has drifted, in metres. Moves AGAINST the wind: a noise read at (position + offset)
    // then travels along the wind. Never wrapped.
    double offsetX = 0.0;
    double offsetZ = 0.0;
};

// A record a consumer may use: finite, unit direction, speed in 0..500 m/s. Anything else gives the default wind.
inline WindState SanitizeWindState(const WindState& in) noexcept
{
    const double length = std::sqrt(static_cast<double>(in.directionX) * in.directionX
        + static_cast<double>(in.directionZ) * in.directionZ);
    const bool ok = std::isfinite(in.directionX) && std::isfinite(in.directionZ) && std::isfinite(in.speed)
        && std::isfinite(in.offsetX) && std::isfinite(in.offsetZ) && length > 1e-6;
    if (!ok) {
        return WindState{};
    }
    WindState out = in;
    out.directionX = static_cast<float>(in.directionX / length);
    out.directionZ = static_cast<float>(in.directionZ / length);
    out.speed = in.speed < 0.0f ? 0.0f : (in.speed > 500.0f ? 500.0f : in.speed);
    return out;
}

} // namespace gte
