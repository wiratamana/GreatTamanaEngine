#include "Renderer/GpuSkinning/GpuSkinningTypes.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GpuSkinningTypes, PackBindPoseVerticesProducesExpectedPaddedLayout)
{
    const std::vector<Vec3> positions = {
        Vec3(1.0f, 2.0f, 3.0f),
        Vec3(-4.0f, 5.0f, -6.0f),
    };
    const std::vector<Vec3> normals = {
        Vec3(0.0f, 1.0f, 0.0f),
        Vec3(1.0f, 0.0f, 0.0f),
    };

    const std::vector<GpuBindPoseVertex> packed = PackBindPoseVertices(positions, normals);

    ASSERT_EQ(packed.size(), 2u);

    EXPECT_FLOAT_EQ(packed[0].positionX, 1.0f);
    EXPECT_FLOAT_EQ(packed[0].positionY, 2.0f);
    EXPECT_FLOAT_EQ(packed[0].positionZ, 3.0f);
    EXPECT_FLOAT_EQ(packed[0].normalX, 0.0f);
    EXPECT_FLOAT_EQ(packed[0].normalY, 1.0f);
    EXPECT_FLOAT_EQ(packed[0].normalZ, 0.0f);

    EXPECT_FLOAT_EQ(packed[1].positionX, -4.0f);
    EXPECT_FLOAT_EQ(packed[1].positionY, 5.0f);
    EXPECT_FLOAT_EQ(packed[1].positionZ, -6.0f);
    EXPECT_FLOAT_EQ(packed[1].normalX, 1.0f);
    EXPECT_FLOAT_EQ(packed[1].normalY, 0.0f);
    EXPECT_FLOAT_EQ(packed[1].normalZ, 0.0f);
}

TEST(GpuSkinningTypes, PackBindPoseVerticesFallsBackToUpWhenNormalsMismatch)
{
    const std::vector<Vec3> positions = {
        Vec3(1.0f, 2.0f, 3.0f),
    };
    const std::vector<Vec3> emptyNormals; // mismatched count on purpose

    const std::vector<GpuBindPoseVertex> packed = PackBindPoseVertices(positions, emptyNormals);

    ASSERT_EQ(packed.size(), 1u);
    // Mirrors Animation/VertexSkinning.cpp's SkinVertexRange() own
    // `hasNormals ? bindNormals[i] : Vec3::Up()` fallback exactly.
    EXPECT_FLOAT_EQ(packed[0].normalX, 0.0f);
    EXPECT_FLOAT_EQ(packed[0].normalY, 1.0f);
    EXPECT_FLOAT_EQ(packed[0].normalZ, 0.0f);
}

TEST(GpuSkinningTypes, PackUvsCopiesValuesUnmodified)
{
    const std::vector<Vec2> uvs = {
        Vec2(0.25f, 0.75f),
        Vec2(1.0f, 0.0f),
    };

    const std::vector<GpuUv> packed = PackUvs(uvs);

    ASSERT_EQ(packed.size(), 2u);
    EXPECT_FLOAT_EQ(packed[0].u, 0.25f);
    EXPECT_FLOAT_EQ(packed[0].v, 0.75f);
    EXPECT_FLOAT_EQ(packed[1].u, 1.0f);
    EXPECT_FLOAT_EQ(packed[1].v, 0.0f);
}

TEST(GpuSkinningTypes, PackUvsHandlesEmptyInput)
{
    const std::vector<Vec2> uvs;
    const std::vector<GpuUv> packed = PackUvs(uvs);
    EXPECT_TRUE(packed.empty());
}

TEST(GpuSkinningTypes, PackSkinWeightsPreservesRealInfluences)
{
    VertexSkinWeights weights;
    weights.type = VertexWeightType::BDEF2;
    weights.boneIndices[0] = 3;
    weights.boneWeights[0] = 0.75f;
    weights.boneIndices[1] = 7;
    weights.boneWeights[1] = 0.25f;
    // slots 2/3 stay at the default "unused" invariant: boneIndex == -1, weight == 0.0

    const std::vector<VertexSkinWeights> input = { weights };
    const std::vector<GpuSkinWeights> packed = PackSkinWeights(input);

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_EQ(packed[0].boneIndices[0], 3u);
    EXPECT_FLOAT_EQ(packed[0].weights[0], 0.75f);
    EXPECT_EQ(packed[0].boneIndices[1], 7u);
    EXPECT_FLOAT_EQ(packed[0].weights[1], 0.25f);
}

TEST(GpuSkinningTypes, PackSkinWeightsTranslatesUnusedSlotsToBoneZeroZeroWeight)
{
    VertexSkinWeights weights;
    weights.type = VertexWeightType::BDEF1;
    weights.boneIndices[0] = 5;
    weights.boneWeights[0] = 1.0f;
    // slots 1-3 are the default "unused" invariant (boneIndex == -1, weight == 0.0).

    const std::vector<VertexSkinWeights> input = { weights };
    const std::vector<GpuSkinWeights> packed = PackSkinWeights(input);

    ASSERT_EQ(packed.size(), 1u);
    for (int slot = 1; slot < 4; ++slot) {
        // Never a raw bit-reinterpreted -1 (which would alias a huge,
        // out-of-bounds std::uint32_t index) - always bone 0, weight 0.0.
        EXPECT_EQ(packed[0].boneIndices[slot], 0u) << "slot " << slot;
        EXPECT_FLOAT_EQ(packed[0].weights[slot], 0.0f) << "slot " << slot;
    }
}

TEST(GpuSkinningTypes, PackSkinWeightsHandlesEmptyInput)
{
    const std::vector<VertexSkinWeights> input;
    const std::vector<GpuSkinWeights> packed = PackSkinWeights(input);
    EXPECT_TRUE(packed.empty());
}

// Compile-time-only checks: these two static_asserts already exist directly
// in GpuSkinningTypes.h and fail to compile this whole test file if ever
// violated - referencing the types here (via sizeof) is what actually
// forces this translation unit to instantiate/see them, serving as the
// "compiling at all is the test" case GPU_SKINNING_PHASE1's own strategy
// document calls for.
TEST(GpuSkinningTypes, OutputVertexLayoutsMatchDocumentedByteSizes)
{
    EXPECT_EQ(sizeof(GpuSkinnedVertexPositionNormal), 24u);
    EXPECT_EQ(sizeof(GpuSkinnedVertexPositionNormalUv), 32u);
    EXPECT_EQ(sizeof(GpuBindPoseVertex), 32u);
    EXPECT_EQ(sizeof(GpuUv), 8u);
    EXPECT_EQ(sizeof(GpuSkinWeights), 32u);
}

} // namespace
} // namespace gte
