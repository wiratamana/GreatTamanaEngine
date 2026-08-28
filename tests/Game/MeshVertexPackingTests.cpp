// Unit tests for MeshVertexPacking.h's PackMeshVertices()/PackMeshVertexUvs() -
// the two pure functions that replaced four hand-copied packing loops in
// Game.cpp (see GameInstantiationRefactorProposal.txt, Step 2.6/3.1) - plus
// PackMeshVertexRange()/PackMeshVertexUvRange(), the batched/parallelizable
// variants added for the multithreaded CPU-skinning optimization (see
// task_manager/optimizing_multi_thread_cpu_skinning/
// MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md).

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

TEST(MeshVertexPackingTest, PackMeshVertexRangeMatchesFullPackForASubrange)
{
    const std::vector<Vec3> positions = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 2.0f, 0.0f, 0.0f },
        Vec3{ 3.0f, 0.0f, 0.0f }, Vec3{ 4.0f, 0.0f, 0.0f } };
    const std::vector<Vec3> normals = { Vec3::Up(), Vec3::Up(), Vec3::Up(), Vec3::Up() };

    const std::vector<MeshVertex> expected = PackMeshVertices(positions, normals);

    std::vector<MeshVertex> actual(positions.size());
    // Two disjoint batches, exactly like AnimationSystem::Update() would
    // dispatch via gte::Jobs::Dispatch() - see MeshVertexPacking.h's own
    // header comment.
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
    // endIndex deliberately beyond positions.size()/out.size() - must clamp,
    // never read/write out of bounds.
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

} // namespace
} // namespace gte
