#include "Animation/VertexSkinning.h"

#include <gtest/gtest.h>

using namespace gte;

TEST(VertexSkinningTests, NoSkinWeightsAtAllLeavesVerticesAtBindPose)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 2.0f, 3.0f } };
    const std::vector<Vec3> normals = { Vec3::Up() };
    const std::vector<VertexSkinWeights> weights; // Empty - "no skinning data for this mesh at all".
    const std::vector<Mat4> matrices = { Mat4::Translation(Vec3{ 5.0f, 0.0f, 0.0f }) };

    std::vector<Vec3> outPositions;
    std::vector<Vec3> outNormals;
    SkinVertices(positions, normals, weights, matrices, outPositions, outNormals);

    ASSERT_EQ(outPositions.size(), 1u);
    EXPECT_TRUE(ApproximatelyEqual(outPositions[0], positions[0]));
    EXPECT_TRUE(ApproximatelyEqual(outNormals[0], normals[0]));
}

TEST(VertexSkinningTests, Bdef1SingleBoneAppliesThatBonesFullTransform)
{
    const std::vector<Vec3> positions = { Vec3{ 0.0f, 0.0f, 0.0f } };
    const std::vector<Vec3> normals = { Vec3::Up() };

    VertexSkinWeights w;
    w.type = VertexWeightType::BDEF1;
    w.boneIndices[0] = 0;
    w.boneWeights[0] = 1.0f;
    const std::vector<VertexSkinWeights> weights = { w };

    const std::vector<Mat4> matrices = { Mat4::Translation(Vec3{ 1.0f, 0.0f, 0.0f }) };

    std::vector<Vec3> outPositions;
    std::vector<Vec3> outNormals;
    SkinVertices(positions, normals, weights, matrices, outPositions, outNormals);

    EXPECT_TRUE(ApproximatelyEqual(outPositions[0], Vec3{ 1.0f, 0.0f, 0.0f }));
}

TEST(VertexSkinningTests, Bdef2BlendsTwoBonesByWeight)
{
    const std::vector<Vec3> positions = { Vec3{ 0.0f, 0.0f, 0.0f } };
    const std::vector<Vec3> normals = { Vec3::Up() };

    VertexSkinWeights w;
    w.type = VertexWeightType::BDEF2;
    w.boneIndices[0] = 0;
    w.boneWeights[0] = 0.5f;
    w.boneIndices[1] = 1;
    w.boneWeights[1] = 0.5f;
    const std::vector<VertexSkinWeights> weights = { w };

    const std::vector<Mat4> matrices = { Mat4::Translation(Vec3::Zero()), Mat4::Translation(Vec3{ 10.0f, 0.0f, 0.0f }) };

    std::vector<Vec3> outPositions;
    std::vector<Vec3> outNormals;
    SkinVertices(positions, normals, weights, matrices, outPositions, outNormals);

    EXPECT_TRUE(ApproximatelyEqual(outPositions[0], Vec3{ 5.0f, 0.0f, 0.0f }));
}

TEST(VertexSkinningTests, UnusedInfluenceSlotsAreIgnoredRegardlessOfWeightType)
{
    const std::vector<Vec3> positions = { Vec3{ 2.0f, 2.0f, 2.0f } };
    const std::vector<Vec3> normals = { Vec3::Up() };

    // BDEF1-shaped: only slot 0 is meaningful - the other 3 stay at their
    // documented default (boneIndex == -1, weight == 0.0), same as a real
    // decoded VertexSkinWeights would leave them.
    VertexSkinWeights w;
    w.boneIndices[0] = 0;
    w.boneWeights[0] = 1.0f;
    const std::vector<VertexSkinWeights> weights = { w };
    const std::vector<Mat4> matrices = { Mat4::Identity() };

    std::vector<Vec3> outPositions;
    std::vector<Vec3> outNormals;
    SkinVertices(positions, normals, weights, matrices, outPositions, outNormals);

    EXPECT_TRUE(ApproximatelyEqual(outPositions[0], positions[0]));
}

TEST(VertexSkinningTests, NoValidInfluenceDegradesToBindPoseRatherThanTheOrigin)
{
    const std::vector<Vec3> positions = { Vec3{ 3.0f, 4.0f, 5.0f } };
    const std::vector<Vec3> normals = { Vec3::Up() };

    const VertexSkinWeights w; // Every slot at its default -1/0.0 - no valid influence at all.
    const std::vector<VertexSkinWeights> weights = { w };
    const std::vector<Mat4> matrices = { Mat4::Translation(Vec3{ 100.0f, 0.0f, 0.0f }) };

    std::vector<Vec3> outPositions;
    std::vector<Vec3> outNormals;
    SkinVertices(positions, normals, weights, matrices, outPositions, outNormals);

    EXPECT_TRUE(ApproximatelyEqual(outPositions[0], positions[0]));
}
