#pragma once

// Bounding volumes and frustum culling math. Pure and Vulkan-free: no device is needed to test it.
// Mat4 is column-major (Data() matches a std430 mat4 bit for bit); m(row, col) is the mathematical entry.

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte {

// Local-space axis-aligned bounding box; describes geometry shared by every instance of a mesh.
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

// The 6 clip planes of a view-projection matrix (Gribb/Hartmann), in the order
// [0]=Left, [1]=Right, [2]=Bottom, [3]=Top, [4]=Near, [5]=Far.
// Near tests z >= 0 (zero-to-one depth range), not z >= -w.
std::array<Plane, 6> ExtractFrustumPlanes(const Mat4& viewProjection);

// True if `worldAABB` touches or lies inside all 6 planes (p-vertex test).
// Conservative: never culls a visible box, may keep a box that is just outside near a plane edge.
bool AABBIntersectsFrustum(const AABB& worldAABB, const std::array<Plane, 6>& planes);

// Per-instance input read by the culling compute shader and the instanced vertex shader.
// std430, 16-byte aligned blocks.
struct GpuCullingInstanceInput {
    float worldMatrix[16] = { 0.0f };
    float aabbMinX = 0.0f, aabbMinY = 0.0f, aabbMinZ = 0.0f, _padMin = 0.0f;
    float aabbMaxX = 0.0f, aabbMaxY = 0.0f, aabbMaxZ = 0.0f, _padMax = 0.0f;
    // The three mesh offsets are the same for every instance of a batch; kept per instance for simplicity.
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t _pad = 0;
};
static_assert(sizeof(GpuCullingInstanceInput) == 112,
    "GpuCullingInstanceInput must match a std430 mat4 + vec4 + vec4 + uvec4 layout exactly");

// Packs a world matrix, an already world-space AABB and the batch's mesh offsets into the GPU layout.
GpuCullingInstanceInput PackCullingInstanceInput(const Mat4& worldMatrix, const AABB& worldAABB,
    std::uint32_t firstIndex, std::uint32_t indexCount, std::int32_t vertexOffset);

// World-space AABB of a local AABB: transforms all 8 corners and takes the min/max.
AABB TransformAABB(const AABB& localAABB, const Mat4& worldMatrix);

} // namespace gte
