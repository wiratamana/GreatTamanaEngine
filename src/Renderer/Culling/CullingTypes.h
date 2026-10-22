#pragma once

// Bounding volumes and frustum culling math. Pure and Vulkan-free: no device is needed to test it.
// Mat4 is column-major (Data() matches a std430 mat4 bit for bit); m(row, col) is the mathematical entry.

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte {

// Axis-aligned bounding box; used for local-space mesh bounds and for world-space instance bounds.
struct AABB {
    Vec3 min{ 0.0f, 0.0f, 0.0f };
    Vec3 max{ 0.0f, 0.0f, 0.0f };
};

// Tightest AABB around `positions`; an empty input gives a zero-sized box at the origin.
AABB ComputeLocalAABB(const std::vector<Vec3>& positions);

// Frustum clip plane: a point p is inside when dot(normal, p) + distance >= 0. `normal` is unit length.
struct Plane {
    Vec3 normal{ 0.0f, 0.0f, 1.0f };
    float distance = 0.0f;
};

// The 6 clip planes of a view-projection matrix (Gribb/Hartmann): Left, Right, Bottom, Top, Near, Far.
// Near tests z >= 0 (zero-to-one depth), not z >= -w.
std::array<Plane, 6> ExtractFrustumPlanes(const Mat4& viewProjection);

// True when `worldBounds` lies fully behind one plane. Conservative: a touching box is never rejected,
// and a box with a non-finite value or min > max is never rejected.
bool IsAABBOutsideFrustum(const std::array<Plane, 6>& planes, const AABB& worldBounds) noexcept;

// Grows `box` on every side by `fractionOfLargestExtent` times its largest extent.
// A negative or non-finite fraction counts as 0.
AABB PadAABB(const AABB& box, float fractionOfLargestExtent) noexcept;

// World-space AABB of a local AABB: transforms all 8 corners and takes the min/max.
AABB TransformAABB(const AABB& localAABB, const Mat4& worldMatrix);

} // namespace gte
