#pragma once

// Screen-space pick of a camera by its apex or frustum edges.

#include "CameraFrustumGizmo.h"

#include "../Math/Mat4.h"
#include "../Math/Vec2.h"

#include <cstdint>
#include <optional>
#include <span>

namespace gte {

inline constexpr float kCameraPickApexPixels = 14.0f;
inline constexpr float kCameraPickEdgePixels = 6.0f;

struct CameraPickCandidate {
    std::uint64_t id = 0;
    FrustumInput frustum;
};

struct CameraPickHit {
    std::uint64_t id = 0;
    float distancePixels = 0.0f;
    bool onApex = false;
};

// Candidate whose apex (< 14 px) or frustum edge (< 6 px) is nearest to `cursor`.
// An apex hit beats an edge hit; equal distances prefer the nearer apex in view depth.
// Invalid frustums are skipped; an empty rect or list gives nullopt.
std::optional<CameraPickHit> PickCamera(std::span<const CameraPickCandidate> candidates, const Mat4& viewProjection,
    Vec2 rectMin, Vec2 rectMax, Vec2 cursor);

} // namespace gte
