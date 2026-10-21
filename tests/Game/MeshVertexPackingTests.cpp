// Unit tests for the vertex packing helpers in MeshVertexPacking.h.

#include "Game/Instantiation/MeshVertexPacking.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

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

TEST(MeshVertexPackingTest, PackMeshVertexRangeMatchesFullPackForASubrange)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 2.0f, 0.0f, 0.0f },
        Vec3{ 3.0f, 0.0f, 0.0f }, Vec3{ 4.0f, 0.0f, 0.0f } };
    const std::vector<Vec3> normals = { Vec3::Up(), Vec3::Up(), Vec3::Up(), Vec3::Up() };

    const std::vector<MeshVertex> expected = PackMeshVertices(positions, normals);

    // Two disjoint batches, like a parallel dispatch.
    std::vector<MeshVertex> actual(positions.size());
    PackMeshVertexRange(0, 2, positions, normals, actual);
    PackMeshVertexRange(2, 4, positions, normals, actual);

    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_FLOAT_EQ(actual[i].position[0], expected[i].position[0]);
        EXPECT_FLOAT_EQ(actual[i].normal[1], expected[i].normal[1]);
    }
}

TEST(MeshVertexPackingTest, PackMeshVertexRangeNeverWritesPastClampedBounds)
{
    const std::vector<Vec3> positions = { Vec3::Zero(), Vec3::Zero() };
    const std::vector<Vec3> normals = { Vec3::Up(), Vec3::Up() };

    std::vector<MeshVertex> out(2);
    // endIndex beyond the sizes must clamp.
    PackMeshVertexRange(0, 100, positions, normals, out);

    EXPECT_FLOAT_EQ(out[1].normal[1], 1.0f);
}

TEST(MeshVertexPackingTest, PackMeshVertexUvRangeMatchesFullPackForASubrange)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 2.0f, 0.0f, 0.0f },
        Vec3{ 3.0f, 0.0f, 0.0f } };
    const std::vector<Vec3> normals = { Vec3::Up(), Vec3::Up(), Vec3::Up() };
    const std::vector<Vec2> uvs = { Vec2{ 0.1f, 0.1f }, Vec2{ 0.2f, 0.2f }, Vec2{ 0.3f, 0.3f } };

    const std::vector<MeshVertexUv> expected = PackMeshVertexUvs(positions, normals, uvs);

    std::vector<MeshVertexUv> actual(positions.size());
    PackMeshVertexUvRange(0, 1, positions, normals, uvs, actual);
    PackMeshVertexUvRange(1, 3, positions, normals, uvs, actual);

    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_FLOAT_EQ(actual[i].uv[0], expected[i].uv[0]);
        EXPECT_FLOAT_EQ(actual[i].uv[1], expected[i].uv[1]);
    }
}

TEST(MeshVertexPackingTest, TangentVertexLayoutIs48BytesWithExpectedOffsets)
{
    EXPECT_EQ(sizeof(MeshVertexTangent), 48u);
    EXPECT_EQ(offsetof(MeshVertexTangent, position), 0u);
    EXPECT_EQ(offsetof(MeshVertexTangent, normal), 12u);
    EXPECT_EQ(offsetof(MeshVertexTangent, uv), 24u);
    EXPECT_EQ(offsetof(MeshVertexTangent, tangent), 32u);
}

TEST(MeshVertexPackingTest, PackMeshVertexTangentsCopiesEveryFieldExactly)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 2.0f, 3.0f } };
    const std::vector<Vec3> normals = { Vec3{ 0.0f, 1.0f, 0.0f } };
    const std::vector<Vec2> uvs = { Vec2{ 0.25f, 0.75f } };
    const std::vector<Vec4> tangents = { Vec4{ 0.0f, 0.0f, 1.0f, -1.0f } };

    const std::vector<MeshVertexTangent> packed = PackMeshVertexTangents(positions, normals, uvs, tangents);

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_FLOAT_EQ(packed[0].position[2], 3.0f);
    EXPECT_FLOAT_EQ(packed[0].normal[1], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].uv[0], 0.25f);
    EXPECT_FLOAT_EQ(packed[0].uv[1], 0.75f);
    EXPECT_FLOAT_EQ(packed[0].tangent[2], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].tangent[3], -1.0f);
}

TEST(MeshVertexPackingTest, PackMeshVertexTangentsUsesDefaultsForMissingArrays)
{
    const std::vector<Vec3> positions = { Vec3::Zero() };

    const std::vector<MeshVertexTangent> packed = PackMeshVertexTangents(positions, {}, {}, {});

    ASSERT_EQ(packed.size(), 1u);
    EXPECT_FLOAT_EQ(packed[0].normal[1], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].uv[0], 0.0f);
    EXPECT_FLOAT_EQ(packed[0].uv[1], 0.0f);
    EXPECT_FLOAT_EQ(packed[0].tangent[0], 1.0f);
    EXPECT_FLOAT_EQ(packed[0].tangent[1], 0.0f);
    EXPECT_FLOAT_EQ(packed[0].tangent[2], 0.0f);
    EXPECT_FLOAT_EQ(packed[0].tangent[3], 1.0f);
}

TEST(MeshVertexPackingTest, PackMeshVertexTangentRangeClampsAndMatchesFullPack)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 2.0f, 0.0f, 0.0f }, Vec3{ 3.0f, 0.0f, 0.0f } };
    const std::vector<Vec3> normals = { Vec3::Up(), Vec3::Up(), Vec3::Up() };
    const std::vector<Vec2> uvs = { Vec2{ 0.1f, 0.1f }, Vec2{ 0.2f, 0.2f }, Vec2{ 0.3f, 0.3f } };
    const std::vector<Vec4> tangents = { Vec4{ 1, 0, 0, 1 }, Vec4{ 0, 0, 1, -1 }, Vec4{ 0, 0, -1, 1 } };

    const std::vector<MeshVertexTangent> expected = PackMeshVertexTangents(positions, normals, uvs, tangents);

    std::vector<MeshVertexTangent> actual(positions.size());
    PackMeshVertexTangentRange(0, 1, positions, normals, uvs, tangents, actual);
    PackMeshVertexTangentRange(1, 100, positions, normals, uvs, tangents, actual); // Clamped.

    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(std::memcmp(&actual[i], &expected[i], sizeof(MeshVertexTangent)), 0) << i;
    }
}


} // namespace
} // namespace gte
