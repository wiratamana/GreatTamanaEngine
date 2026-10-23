#pragma once

// Pure projection helpers for 2D overlays drawn over a rendered image.
// Matrices are the engine's flipY = true view-projection, so ndc.y = -1 is the top row.

#include "CameraFrustumGizmo.h"

#include "../Math/Mat4.h"
#include "../Math/Vec2.h"
#include "../Math/Vec3.h"
#include "../Math/Vec4.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gte {

// Smallest clip-space w that still counts as in front of the camera.
inline constexpr float kMinClipW = 1.0e-4f;

// Pixel position of a clip-space point inside [rectMin, rectMax).
Vec2 ClipToScreen(const Vec4& clip, Vec2 rectMin, Vec2 rectMax) noexcept;

// Projects a point into [rectMin, rectMax). False (out untouched) behind the camera or for non-finite input.
bool ProjectToScreen(const Vec3& point, const Mat4& viewProj, Vec2 rectMin, Vec2 rectMax, Vec2& out) noexcept;

// Transforms both ends to clip space and clips the segment against w > kMinClipW.
// False when it is fully behind the camera or non-finite.
bool ClipSegmentToView(const Mat4& viewProj, Vec3 a, Vec3 b, Vec4& ca, Vec4& cb) noexcept;

float PointToSegmentDistance(Vec2 p, Vec2 a, Vec2 b) noexcept;

struct OverlaySegment2D {
    Vec2 a;
    Vec2 b;
    std::array<std::uint8_t, 4> rgba{ 255, 255, 255, 255 };
    float width = 1.0f;
};

// Projects and clips a segment list into pixels. Appends to `out`; skipped segments add nothing.
void ProjectSegments(std::span<const FrustumSegment> segments, const Mat4& viewProj, Vec2 rectMin, Vec2 rectMax,
    std::array<std::uint8_t, 4> rgba, float width, std::vector<OverlaySegment2D>& out);

} // namespace gte
