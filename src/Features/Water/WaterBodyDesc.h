#pragma once

#include "../../Math/Vec2.h"

#include <cstdint>
#include <vector>

namespace gte {

// Most bodies, and the shared point pool, one frame can carry.
inline constexpr std::uint32_t kWaterMaxBodies = 16;
inline constexpr std::uint32_t kWaterMaxPoints = 256;
inline constexpr std::uint32_t kWaterMaxLakePoints = 32;
inline constexpr std::uint32_t kWaterMaxRiverPoints = 64;

enum class WaterBodyKind : std::uint8_t {
    Ocean,       // Infinite plane at `level`.
    LakeBox,     // Axis-aligned rectangle (`boxMin`, `boxMax`) at `level`.
    LakePolygon, // Closed polygon of 3..32 points at `level`.
    River,       // Polyline of 2..64 points; each point has its own surface level and half width.
};

// One point of a lake outline or a river centre line, in world metres.
struct WaterBodyPoint {
    Vec2 xz;
    float level = 0.0f;     // River: surface height at this point. Lakes ignore it.
    float halfWidth = 0.0f; // River: half width at this point. Lakes ignore it.
};

// One water body as the editor layer describes it. Plain data, no GPU types.
struct WaterBodyDesc {
    WaterBodyKind kind = WaterBodyKind::Ocean;
    float level = 0.0f;       // World Y of the surface (river: used only for the visibility test).
    float waveScale = 1.0f;   // 0..2, multiplier of the wave height on this body.
    float flowSpeed = 0.0f;   // River: metres per second along the polyline.
    Vec2 boxMin;
    Vec2 boxMax;
    std::vector<WaterBodyPoint> points;
};

} // namespace gte
