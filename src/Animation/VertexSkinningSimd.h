#pragma once

#include "../Math/Vec2.h"
#include "../Math/Vec3.h"
#include "../Math/Mat4.h"
#include "../Assets/MeshData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// Bind-pose vertex in the same 8-float order as the renderer's textured vertex (position, normal, uv).
struct alignas(16) CompactBindVertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(CompactBindVertex) == 32);

// First bone index of a vertex that has no usable influence: the kernel copies the bind vertex unchanged.
inline constexpr std::uint32_t kCompactNoInfluence = 0xFFFFFFFFu;

// Valid influences of one vertex, packed to the front. Weights already sum to 1; unused slots are bone 0, weight 0.
struct alignas(16) CompactSkinVertex {
    std::uint32_t boneIndices[4];
    float weights[4];
};
static_assert(sizeof(CompactSkinVertex) == 32);

// Per-model data for the SIMD skin+pack kernels. Built once from the reference arrays; never changes afterwards.
struct CompactSkinData {
    std::vector<CompactBindVertex> bind;
    std::vector<CompactSkinVertex> skin;

    // Matrix count the indices were validated against; the kernels refuse any other count.
    std::size_t boneCount = 0;

    std::size_t VertexCount() const noexcept { return skin.size(); }
};

// Applies the same rules as SkinVertexRange: a slot is dropped when its bone is out of range or its weight is 0, the
// rest are divided by their sum, and a vertex with a sum <= 0 (or no weight array) keeps its bind position and normal.
// Missing normals become Up(), missing uvs become zero, like PackMeshVertexUvRange.
CompactSkinData BuildCompactSkinData(const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<Vec2>& uvs, const std::vector<VertexSkinWeights>& skinWeights, std::size_t boneCount);

// Skins vertices [beginIndex, endIndex) and writes them as 8 floats each (position, normal, uv) to
// `outVertices[0 .. 8 * VertexCount())`. Does nothing when `matrixCount != data.boneCount`.
// Pure and allocation free: batches over disjoint ranges may run on different threads.
void SkinPackTexturedRange(std::uint32_t beginIndex, std::uint32_t endIndex, const CompactSkinData& data,
    const Mat4* skinningMatrices, std::size_t matrixCount, float* outVertices);

// Same, but writes 6 floats per vertex (position, normal), tightly packed.
void SkinPackUntexturedRange(std::uint32_t beginIndex, std::uint32_t endIndex, const CompactSkinData& data,
    const Mat4* skinningMatrices, std::size_t matrixCount, float* outVertices);

} // namespace gte
