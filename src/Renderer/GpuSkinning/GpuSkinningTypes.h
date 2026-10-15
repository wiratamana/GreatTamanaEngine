#pragma once

#include "../../Assets/MeshData.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace gte {

// GPU buffer layouts for compute vertex skinning (Vulkan-free). std430 aligns a vec3 array element to
// 16 bytes, so shader-read structs are padded to a vec4 multiple; only the Pack*() functions repack.

// Bindings: 0 bind-pose vertices, 1 skin weights, 2 bone matrices (readonly std430), 3 skinned output
// (tightly packed), 4 bind-pose UVs (UV variant only). Only the bone matrices change per frame.

// Bind-pose vertex: position and normal each padded to a vec4.
struct GpuBindPoseVertex {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float _padPosition = 0.0f;

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
    float _padNormal = 0.0f;
};
static_assert(sizeof(GpuBindPoseVertex) == 32,
    "GpuBindPoseVertex must match a std430 vec4+vec4 layout exactly (16-byte-aligned position, 16-byte-aligned normal)");

// Bind-pose UV: a std430 vec2 needs no padding.
struct GpuUv {
    float u = 0.0f;
    float v = 0.0f;
};
static_assert(sizeof(GpuUv) == 8, "GpuUv must match a std430 vec2 element exactly (no padding needed)");

// Per-vertex skin weights, without the SDEF correction terms (SDEF is skinned like BDEF2).
// Bone indices are unsigned; an unused slot is bone 0 with weight 0.
struct GpuSkinWeights {
    std::uint32_t boneIndices[4] = { 0, 0, 0, 0 };
    float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static_assert(sizeof(GpuSkinWeights) == 32,
    "GpuSkinWeights must match a std430 uvec4+vec4 layout exactly (16 bytes each, tightly packed, no extra padding needed)");

// Skinned output vertex for VertexLayout::PositionNormal. Read by the fixed-function vertex input,
// so it stays tightly packed and byte-identical to MeshVertex. This file cannot include MeshVertex.h
// (it pulls in volk.h), so the size is asserted here by hand.
struct GpuSkinnedVertexPositionNormal {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
};
static_assert(sizeof(GpuSkinnedVertexPositionNormal) == 24,
    "must stay byte-identical to MeshVertex (src/Renderer/MeshVertex.h) - the graphics pipeline reads this buffer via the SAME vertex input state");

// Skinned output vertex for VertexLayout::PositionNormalUv; same packing rule, matches MeshVertexUv.
struct GpuSkinnedVertexPositionNormalUv {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;

    float u = 0.0f;
    float v = 0.0f;
};
static_assert(sizeof(GpuSkinnedVertexPositionNormalUv) == 32,
    "must stay byte-identical to MeshVertexUv (src/Renderer/MeshVertex.h)");

// Pads CPU positions/normals into the GPU layout; missing or mismatched normals become Up.
std::vector<GpuBindPoseVertex> PackBindPoseVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals);

// Reshapes Vec2 UVs into GpuUv (a straight copy).
std::vector<GpuUv> PackUvs(const std::vector<Vec2>& uvs);

// Converts skin weights into the GPU layout. An unused slot (bone index < 0) becomes bone 0 with
// weight 0, which the kernel skips by its zero weight.
std::vector<GpuSkinWeights> PackSkinWeights(const std::vector<VertexSkinWeights>& skinWeights);

} // namespace gte
