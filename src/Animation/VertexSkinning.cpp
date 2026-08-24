#include "VertexSkinning.h"

#include <cstdint>

namespace gte {

void SkinVertices(const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<VertexSkinWeights>& skinWeights, const std::vector<Mat4>& skinningMatrices,
    std::vector<Vec3>& outPositions, std::vector<Vec3>& outNormals)
{
    const std::size_t vertexCount = bindPositions.size();
    outPositions.resize(vertexCount);
    outNormals.resize(vertexCount);

    const bool hasNormals = bindNormals.size() == vertexCount;
    const bool hasWeights = skinWeights.size() == vertexCount;
    const std::size_t boneCount = skinningMatrices.size();

    for (std::size_t i = 0; i < vertexCount; ++i) {
        const Vec3& bindPosition = bindPositions[i];
        const Vec3 bindNormal = hasNormals ? bindNormals[i] : Vec3::Up();

        if (!hasWeights) {
            // No skinning data at all for this vertex - leave it exactly at
            // its bind pose (identity deformation).
            outPositions[i] = bindPosition;
            outNormals[i] = bindNormal;
            continue;
        }

        const VertexSkinWeights& weights = skinWeights[i];

        Vec3 skinnedPosition = Vec3::Zero();
        Vec3 skinnedNormal = Vec3::Zero();
        float totalWeight = 0.0f;

        for (int slot = 0; slot < 4; ++slot) {
            const std::int32_t boneIndex = weights.boneIndices[slot];
            const float weight = weights.boneWeights[slot];
            if (boneIndex < 0 || weight == 0.0f || static_cast<std::size_t>(boneIndex) >= boneCount) {
                continue;
            }

            const Mat4& skinMatrix = skinningMatrices[static_cast<std::size_t>(boneIndex)];
            skinnedPosition += skinMatrix.TransformPoint(bindPosition) * weight;
            skinnedNormal += skinMatrix.TransformVector(bindNormal) * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0f) {
            // BDEF4 weights aren't guaranteed to sum to 1.0 by the format
            // itself (see VertexWeightType's own doc comment) - normalize
            // by the actual total rather than assuming it.
            outPositions[i] = skinnedPosition / totalWeight;
            outNormals[i] = Normalize(skinnedNormal);
        } else {
            // No valid influence at all (e.g. every slot's boneIndex was
            // -1/out of range) - degrade to the bind pose rather than
            // collapsing to the origin.
            outPositions[i] = bindPosition;
            outNormals[i] = bindNormal;
        }
    }
}

} // namespace gte
