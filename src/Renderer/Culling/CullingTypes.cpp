#include "CullingTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gte {

AABB ComputeLocalAABB(const std::vector<Vec3>& positions)
{
    if (positions.empty()) {
        return AABB{}; // degenerate, zero-sized box at the origin - never divides by zero, never reads out of bounds.
    }

    Vec3 minPoint = positions[0];
    Vec3 maxPoint = positions[0];
    for (std::size_t i = 1; i < positions.size(); ++i) {
        const Vec3& p = positions[i];
        minPoint.x = std::min(minPoint.x, p.x);
        minPoint.y = std::min(minPoint.y, p.y);
        minPoint.z = std::min(minPoint.z, p.z);
        maxPoint.x = std::max(maxPoint.x, p.x);
        maxPoint.y = std::max(maxPoint.y, p.y);
        maxPoint.z = std::max(maxPoint.z, p.z);
    }

    AABB result;
    result.min = minPoint;
    result.max = maxPoint;
    return result;
}

namespace {

// Normalizes a raw (a, b, c, d) plane row; a zero-length normal falls back to +Z instead of dividing by zero.
Plane MakeNormalizedPlane(float a, float b, float c, float d) noexcept
{
    const float length = std::sqrt(a * a + b * b + c * c);
    Plane plane;
    if (length > kEpsilon) {
        const float invLength = 1.0f / length;
        plane.normal = Vec3(a * invLength, b * invLength, c * invLength);
        plane.distance = d * invLength;
    } else {
        plane.normal = Vec3(0.0f, 0.0f, 1.0f);
        plane.distance = 0.0f;
    }
    return plane;
}

} // namespace

std::array<Plane, 6> ExtractFrustumPlanes(const Mat4& viewProjection)
{
    const Mat4& m = viewProjection;

    std::array<Plane, 6> planes;
    // Gribb/Hartmann rows; m(row, col) is storage-order independent.
    // Left:   row3 + row0  (x >= -w)
    planes[0] = MakeNormalizedPlane(
        m(3, 0) + m(0, 0), m(3, 1) + m(0, 1), m(3, 2) + m(0, 2), m(3, 3) + m(0, 3));
    // Right:  row3 - row0  (x <= w)
    planes[1] = MakeNormalizedPlane(
        m(3, 0) - m(0, 0), m(3, 1) - m(0, 1), m(3, 2) - m(0, 2), m(3, 3) - m(0, 3));
    // Bottom: row3 + row1  (y >= -w)
    planes[2] = MakeNormalizedPlane(
        m(3, 0) + m(1, 0), m(3, 1) + m(1, 1), m(3, 2) + m(1, 2), m(3, 3) + m(1, 3));
    // Top:    row3 - row1  (y <= w)
    planes[3] = MakeNormalizedPlane(
        m(3, 0) - m(1, 0), m(3, 1) - m(1, 1), m(3, 2) - m(1, 2), m(3, 3) - m(1, 3));
    // Near (VULKAN ZERO-TO-ONE depth range - z >= 0, NOT OpenGL's z >= -w): row2.
    planes[4] = MakeNormalizedPlane(m(2, 0), m(2, 1), m(2, 2), m(2, 3));
    // Far:    row3 - row2  (z <= w)
    planes[5] = MakeNormalizedPlane(
        m(3, 0) - m(2, 0), m(3, 1) - m(2, 1), m(3, 2) - m(2, 2), m(3, 3) - m(2, 3));

    return planes;
}

namespace {

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool IsAABBOutsideFrustum(const std::array<Plane, 6>& planes, const AABB& worldBounds) noexcept
{
    const Vec3& lo = worldBounds.min;
    const Vec3& hi = worldBounds.max;
    if (!IsFinite(lo) || !IsFinite(hi) || lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) {
        return false;
    }

    for (const Plane& plane : planes) {
        // The corner furthest along the normal is the last one to leave the plane's inside.
        const Vec3 p(plane.normal.x >= 0.0f ? hi.x : lo.x, plane.normal.y >= 0.0f ? hi.y : lo.y,
            plane.normal.z >= 0.0f ? hi.z : lo.z);
        if (plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z + plane.distance < 0.0f) {
            return true;
        }
    }
    return false;
}

AABB PadAABB(const AABB& box, float fractionOfLargestExtent) noexcept
{
    const float fraction = (std::isfinite(fractionOfLargestExtent) && fractionOfLargestExtent > 0.0f)
        ? fractionOfLargestExtent
        : 0.0f;
    const float largest =
        std::max({ box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, 0.0f });
    const float pad = largest * fraction;

    AABB result;
    result.min = Vec3(box.min.x - pad, box.min.y - pad, box.min.z - pad);
    result.max = Vec3(box.max.x + pad, box.max.y + pad, box.max.z + pad);
    return result;
}

GpuCullingInstanceInput PackCullingInstanceInput(const Mat4& worldMatrix, const AABB& worldAABB,
    std::uint32_t firstIndex, std::uint32_t indexCount, std::int32_t vertexOffset)
{
    GpuCullingInstanceInput input{};

    // Mat4 is column-major, bit-identical to a std430 mat4: copy without transposing.
    static_assert(sizeof(input.worldMatrix) == sizeof(float) * 16, "worldMatrix must be exactly 16 floats");
    std::memcpy(input.worldMatrix, worldMatrix.Data(), sizeof(input.worldMatrix));

    input.aabbMinX = worldAABB.min.x;
    input.aabbMinY = worldAABB.min.y;
    input.aabbMinZ = worldAABB.min.z;
    input.aabbMaxX = worldAABB.max.x;
    input.aabbMaxY = worldAABB.max.y;
    input.aabbMaxZ = worldAABB.max.z;

    input.firstIndex = firstIndex;
    input.indexCount = indexCount;
    input.vertexOffset = vertexOffset;

    return input;
}

AABB TransformAABB(const AABB& localAABB, const Mat4& worldMatrix)
{
    const Vec3 corners[8] = {
        Vec3(localAABB.min.x, localAABB.min.y, localAABB.min.z),
        Vec3(localAABB.max.x, localAABB.min.y, localAABB.min.z),
        Vec3(localAABB.min.x, localAABB.max.y, localAABB.min.z),
        Vec3(localAABB.max.x, localAABB.max.y, localAABB.min.z),
        Vec3(localAABB.min.x, localAABB.min.y, localAABB.max.z),
        Vec3(localAABB.max.x, localAABB.min.y, localAABB.max.z),
        Vec3(localAABB.min.x, localAABB.max.y, localAABB.max.z),
        Vec3(localAABB.max.x, localAABB.max.y, localAABB.max.z),
    };

    Vec3 worldMin = worldMatrix.TransformPoint(corners[0]);
    Vec3 worldMax = worldMin;
    for (int i = 1; i < 8; ++i) {
        const Vec3 worldCorner = worldMatrix.TransformPoint(corners[i]);
        worldMin.x = std::min(worldMin.x, worldCorner.x);
        worldMin.y = std::min(worldMin.y, worldCorner.y);
        worldMin.z = std::min(worldMin.z, worldCorner.z);
        worldMax.x = std::max(worldMax.x, worldCorner.x);
        worldMax.y = std::max(worldMax.y, worldCorner.y);
        worldMax.z = std::max(worldMax.z, worldCorner.z);
    }

    AABB result;
    result.min = worldMin;
    result.max = worldMax;
    return result;
}

} // namespace gte
