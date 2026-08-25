// Unit tests for MeshVertexPacking.h's PackMeshVertices()/PackMeshVertexUvs() -
// the two pure functions that replaced four hand-copied packing loops in
// Game.cpp (see GameInstantiationRefactorProposal.txt, Step 2.6/3.1).

#include "Game/Instantiation/MeshVertexPacking.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MeshVertexPackingTest, PackMeshVerticesCopiesPositionAndNormalExactly)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 2.0f, 3.0f }, Vec3{ 4.0f, 5.0f, 6.0f } };
    const std::vector<Vec3> normals = { Vec3{ 0.0f, 0.0f, 1.0f }, Vec3{ 1.0f, 0.0f, 0.0f } };

    const std::vector<MeshVertex> packed = PackMeshVertices(positions, normals);

    ASSERT_EQ(packed.size(), 2u);
    EXPECT_FLOAT_EQ(packed[0].position[0], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].position[1], 2.0f);
    EXPECT_FLOAT_EQ(packed[0].position[2], 3.0f);
    EXPECT_FLOAT_EQ(packed[0].normal[2], 1.0f);
    EXPECT_FLOAT_EQ(packed[1].position[0], 4.0f);
    EXPECT_FLOAT_EQ(packed[1].normal[0], 1.0f);
}

TEST(MeshVertexPackingTest, PackMeshVerticesFallsBackToUpWhenNormalsMissing)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 2.0f, 3.0f } };
    const std::vector<Vec3> normals; // empty - doesn't match positions.size()

    const std::vector<MeshVertex> packed = PackMeshVertices(positions, normals);

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_FLOAT_EQ(packed[0].normal[0], 0.0f);
    EXPECT_FLOAT_EQ(packed[0].normal[1], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].normal[2], 0.0f);
}

TEST(MeshVertexPackingTest, PackMeshVerticesFallsBackToUpWhenNormalsCountMismatched)
{
    const std::vector<Vec3> positions = { Vec3::Zero(), Vec3::Zero() };
    const std::vector<Vec3> normals = { Vec3{ 1.0f, 0.0f, 0.0f } }; // one entry, two positions

    const std::vector<MeshVertex> packed = PackMeshVertices(positions, normals);

    ASSERT_EQ(packed.size(), 2u);
    EXPECT_FLOAT_EQ(packed[0].normal[1], 1.0f);
    EXPECT_FLOAT_EQ(packed[1].normal[1], 1.0f);
}

TEST(MeshVertexPackingTest, PackMeshVerticesHandlesEmptyInput)
{
    const std::vector<MeshVertex> packed = PackMeshVertices({}, {});
    EXPECT_TRUE(packed.empty());
}

TEST(MeshVertexPackingTest, PackMeshVertexUvsCopiesEveryFieldExactly)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 2.0f, 3.0f } };
    const std::vector<Vec3> normals = { Vec3{ 0.0f, 1.0f, 0.0f } };
    const std::vector<Vec2> uvs = { Vec2{ 0.25f, 0.75f } };

    const std::vector<MeshVertexUv> packed = PackMeshVertexUvs(positions, normals, uvs);

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_FLOAT_EQ(packed[0].position[0], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].normal[1], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].uv[0], 0.25f);
    EXPECT_FLOAT_EQ(packed[0].uv[1], 0.75f);
}

TEST(MeshVertexPackingTest, PackMeshVertexUvsFallsBackToZeroWhenUvsMissing)
{
    const std::vector<Vec3> positions = { Vec3::Zero() };
    const std::vector<Vec3> normals = { Vec3::Up() };
    const std::vector<Vec2> uvs; // empty - doesn't match positions.size()

    const std::vector<MeshVertexUv> packed = PackMeshVertexUvs(positions, normals, uvs);

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_FLOAT_EQ(packed[0].uv[0], 0.0f);
    EXPECT_FLOAT_EQ(packed[0].uv[1], 0.0f);
}

TEST(MeshVertexPackingTest, PackMeshVertexUvsFallsBackToUpWhenNormalsMissing)
{
    const std::vector<Vec3> positions = { Vec3::Zero() };
    const std::vector<Vec3> normals; // empty
    const std::vector<Vec2> uvs = { Vec2{ 1.0f, 1.0f } };

    const std::vector<MeshVertexUv> packed = PackMeshVertexUvs(positions, normals, uvs);

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_FLOAT_EQ(packed[0].normal[1], 1.0f);
}

} // namespace
} // namespace gte
