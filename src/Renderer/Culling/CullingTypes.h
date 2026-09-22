#pragma once

// ============================================================================
// GPU-Driven Frustum Culling + Indirect Draw (render-pass-5) - PHASE1:
// Foundations: Bounds, Indirect Types, and Render-Graph Vocabulary
// ============================================================================
// See task_manager/render-pass-5/PHASE0_MASTER_STRATEGY.md for the full
// campaign map and
// task_manager/render-pass-5/PHASE1_FOUNDATIONS_BOUNDS_INDIRECT_TYPES_AND_VOCABULARY.md
// for the full design reasoning behind every type/function below. This file
// defines the PURE, Vulkan-device-free bounding-volume + frustum-culling math
// a future compute culling kernel (PHASE3) and the CPU-side per-frame
// instance-packing step (PHASE4) both depend on. It creates NO Vulkan
// buffers/descriptor sets/pipelines itself and dispatches NOTHING.
//
// Deliberately Vulkan-header-free (no <volk.h>/<vulkan/vulkan.h> anywhere in
// this file or its .cpp) - mirrors src/Renderer/GpuSkinning/GpuSkinningTypes.h's
// own precedent exactly, so every type/function here stays trivially
// Tier-1-testable with no live VkDevice at all (see
// tests/Renderer/Culling/CullingTypesTests.cpp). The GPU-facing
// IndirectDrawCommand mirror of VkDrawIndexedIndirectCommand lives in a
// SEPARATE file (src/Renderer/IndirectDrawTypes.h) since it has nothing to do
// with culling specifically, only with indirect draws generally, and
// legitimately needs <volk.h> for its own cross-check static_assert.
//
// IMPORTANT - Math/Mat4.h storage convention (verified directly against
// Mat4.h/.cpp before writing ExtractFrustumPlanes()/PackCullingInstanceInput()
// below, per this phase's own explicit instruction - see
// PHASE1_COMPLETION_REPORT.md for the full write-up): Mat4 is COLUMN-MAJOR
// storage (`columns[4]`, each a Vec4), and Mat4::Data() already returns a
// contiguous column-major float[16] - BIT-IDENTICAL to what a GLSL std430
// `mat4` expects. PackCullingInstanceInput() below is therefore a straight
// memcpy of Data() into GpuCullingInstanceInput::worldMatrix - NO transpose
// is needed. Mat4::operator()(row, col) reads `columns[col][row]`, i.e. it
// ALWAYS returns the conventional mathematical row/col entry regardless of
// physical column-major storage - so the standard Gribb/Hartmann row-based
// plane-extraction formulas below can be written directly in terms of
// `m(row, col)` with no adjustment for storage order either.

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte {

// Local-space (object-space) axis-aligned bounding box - describes GEOMETRY,
// shared by every instance drawn with a given Mesh, never per-entity.
struct AABB {
    Vec3 min{ 0.0f, 0.0f, 0.0f };
    Vec3 max{ 0.0f, 0.0f, 0.0f };
};

// Computes the tightest AABB enclosing every position in `positions` - pure,
// no GPU/Renderer dependency. Returns AABB{} (a degenerate, zero-sized box at
// the origin) for an empty `positions` - never divides by zero, never reads
// out of bounds.
AABB ComputeLocalAABB(const std::vector<Vec3>& positions);

// A single frustum clip plane, in the form `dot(normal, p) + distance >= 0`
// == "p is on the INSIDE of this plane". Pure data, no Vulkan dependency.
// `normal` is always unit-length (ExtractFrustumPlanes() below normalizes
// each plane) - this only matters for producing a genuinely meaningful
// signed distance; AABBIntersectsFrustum()'s own boolean inside/outside test
// is invariant to a positive rescale of a plane's (normal, distance) pair
// either way.
struct Plane {
    Vec3 normal{ 0.0f, 0.0f, 1.0f };
    float distance = 0.0f;
};

// Extracts the 6 frustum clip planes (left/right/bottom/top/near/far) from a
// combined view-projection matrix, via the standard Gribb/Hartmann row
// extraction method - order in the returned array is fixed:
// [0]=Left, [1]=Right, [2]=Bottom, [3]=Top, [4]=Near, [5]=Far. Uses
// Mat4::operator()(row, col), which always returns the conventional
// mathematical entry regardless of Mat4's own column-major physical storage
// (see this file's own header comment) - no transpose needed. The Near/Far
// pair matches this engine's VULKAN ZERO-TO-ONE depth range (see
// Mat4::PerspectiveFovLH_ZO/OrthographicLH_ZO) - Near tests `z >= 0`, NOT
// OpenGL's `z >= -w`. Hand-verified against a known, simple orthographic
// case in tests/Renderer/Culling/CullingTypesTests.cpp.
std::array<Plane, 6> ExtractFrustumPlanes(const Mat4& viewProjection);

// True if `worldAABB` intersects (or is fully inside) every one of the 6
// `planes` - the standard "positive vertex" (p-vertex) test: for each plane,
// pick the AABB corner furthest along that plane's normal; if even that
// corner is outside, the whole box is outside. A CONSERVATIVE test (never
// FALSE-culls a box that's actually at least partially visible; may accept a
// box that's actually fully outside in rare corner cases near a plane edge -
// the industry-standard, acceptable trade-off for real-time frustum culling).
bool AABBIntersectsFrustum(const AABB& worldAABB, const std::array<Plane, 6>& planes);

// GPU-side mirror of VkDrawIndexedIndirectCommand lives in
// Renderer/IndirectDrawTypes.h (see this file's own header comment) - kept
// in a SEPARATE file since it has nothing to do with culling specifically,
// only with indirect draws generally.

// GPU-side per-instance input the culling compute shader reads AND the new
// instanced vertex shader ALSO reads (for its own model matrix) - std430,
// 16-byte-aligned blocks throughout (mirrors GpuBindPoseVertex's own padding
// discipline, src/Renderer/GpuSkinning/GpuSkinningTypes.h).
struct GpuCullingInstanceInput {
    float worldMatrix[16] = { 0.0f };
    float aabbMinX = 0.0f, aabbMinY = 0.0f, aabbMinZ = 0.0f, _padMin = 0.0f;
    float aabbMaxX = 0.0f, aabbMaxY = 0.0f, aabbMaxZ = 0.0f, _padMax = 0.0f;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t _pad = 0;
};
static_assert(sizeof(GpuCullingInstanceInput) == 112,
    "GpuCullingInstanceInput must match a std430 mat4 + vec4 + vec4 + uvec4 layout exactly");
// NOTE (worth knowing, not urgent, flagged during the general campaign
// double-check): firstIndex/indexCount/vertexOffset are identical for every
// instance in one batch (they describe the shared Mesh, not the per-entity
// Transform) - storing them per-instance here is a deliberate, ACCEPTED
// redundancy (112 bytes/instance instead of a smaller shared value), not a
// correctness bug. A future bandwidth-sensitive revision could hoist these
// three fields to a per-batch push constant/uniform instead of duplicating
// them per instance - not attempted in this campaign, since it would touch
// this already-locked struct shape PHASE3's shader and PHASE4's packing code
// both depend on; a genuine follow-up item only.

// Packs one instance's world matrix + world-space AABB (ALREADY TRANSFORMED
// by the caller - see PHASE4's own per-frame packing step, which transforms
// each Mesh's own LOCAL AABB by that entity's world matrix once per frame on
// the CPU, exactly as cheap as the existing per-frame Transform resolution
// already is) + this batch's shared mesh geometry offsets into the GPU
// layout above. Pure function, no GPU/Renderer dependency.
GpuCullingInstanceInput PackCullingInstanceInput(const Mat4& worldMatrix, const AABB& worldAABB,
    std::uint32_t firstIndex, std::uint32_t indexCount, std::int32_t vertexOffset);

// Transforms a LOCAL-space AABB by `worldMatrix` into a new, axis-aligned
// WORLD-space AABB (the standard "transform all 8 corners, take the min/max"
// approach - simple, always correct, conservative under non-uniform
// scale/rotation). Pure function.
AABB TransformAABB(const AABB& localAABB, const Mat4& worldMatrix);

} // namespace gte
