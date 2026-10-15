#include "GpuSkinningTypes.h"

namespace gte {

std::vector<GpuBindPoseVertex> PackBindPoseVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals)
{
    std::vector<GpuBindPoseVertex> packed;
    packed.reserve(positions.size());

    const bool hasNormals = normals.size() == positions.size();

    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3& position = positions[i];
        // A missing or mismatched normal falls back to Up, never an out-of-bounds read.
        const Vec3 normal = hasNormals ? normals[i] : Vec3::Up();

        GpuBindPoseVertex vertex{};
        vertex.positionX = position.x;
        vertex.positionY = position.y;
        vertex.positionZ = position.z;
        vertex.normalX = normal.x;
        vertex.normalY = normal.y;
        vertex.normalZ = normal.z;
        packed.push_back(vertex);
    }

    return packed;
}

std::vector<GpuUv> PackUvs(const std::vector<Vec2>& uvs)
{
    std::vector<GpuUv> packed;
    packed.reserve(uvs.size());

    for (const Vec2& uv : uvs) {
        GpuUv gpuUv{};
        gpuUv.u = uv.x;
        gpuUv.v = uv.y;
        packed.push_back(gpuUv);
    }

    return packed;
}

std::vector<GpuSkinWeights> PackSkinWeights(const std::vector<VertexSkinWeights>& skinWeights)
{
    std::vector<GpuSkinWeights> packed;
    packed.reserve(skinWeights.size());

    for (const VertexSkinWeights& weights : skinWeights) {
        GpuSkinWeights gpu{};

        for (int slot = 0; slot < 4; ++slot) {
            const std::int32_t boneIndex = weights.boneIndices[slot];
            const float weight = weights.boneWeights[slot];

            if (boneIndex < 0) {
                // Unused slot: bone 0 / weight 0, never a bit-cast -1 (a huge out-of-bounds index).
                gpu.boneIndices[slot] = 0;
                gpu.weights[slot] = 0.0f;
            } else {
                gpu.boneIndices[slot] = static_cast<std::uint32_t>(boneIndex);
                gpu.weights[slot] = weight;
            }
        }

        packed.push_back(gpu);
    }

    return packed;
}

} // namespace gte
