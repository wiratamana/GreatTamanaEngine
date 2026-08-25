// Unit tests for MeshMaterialPartitioner.h's PartitionMeshMaterials() - the
// pure index-range math extracted out of Game::EnsureMeshAsset() (see
// GameInstantiationRefactorProposal.txt, Step 3.2).

#include "Game/Instantiation/MeshMaterialPartitioner.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

Material MakeMaterial(const std::string& name, std::uint32_t indexCount)
{
    Material material;
    material.name = name;
    material.indexCount = indexCount;
    return material;
}

TEST(MeshMaterialPartitionerTest, MaterialsSummingExactlyToIndexCountProduceNoLeftover)
{
    const std::vector<Material> materials = { MakeMaterial("A", 6), MakeMaterial("B", 9) };

    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(15, materials);

    ASSERT_EQ(slices.size(), 2u);
    EXPECT_EQ(slices[0].start, 0u);
    EXPECT_EQ(slices[0].count, 6u);
    EXPECT_EQ(slices[0].materialIndex, 0);
    EXPECT_EQ(slices[0].name, "A");
    EXPECT_EQ(slices[1].start, 6u);
    EXPECT_EQ(slices[1].count, 9u);
    EXPECT_EQ(slices[1].materialIndex, 1);
    EXPECT_EQ(slices[1].name, "B");
}

TEST(MeshMaterialPartitionerTest, MaterialsSummingToLessThanIndexCountProduceLeftoverBucket)
{
    const std::vector<Material> materials = { MakeMaterial("A", 6) };

    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(10, materials);

    ASSERT_EQ(slices.size(), 2u);
    EXPECT_EQ(slices[0].materialIndex, 0);
    EXPECT_EQ(slices[0].count, 6u);
    EXPECT_EQ(slices[1].materialIndex, -1);
    EXPECT_EQ(slices[1].start, 6u);
    EXPECT_EQ(slices[1].count, 4u);
    EXPECT_TRUE(slices[1].name.empty());
}

TEST(MeshMaterialPartitionerTest, CorruptFileOverclaimingPastIndexCountIsClamped)
{
    // Material "A" claims 20 indices but the mesh only has 10 total - the
    // slice should be clamped to whatever actually remains, and no
    // leftover/next-material slice should read past the end.
    const std::vector<Material> materials = { MakeMaterial("A", 20), MakeMaterial("B", 5) };

    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(10, materials);

    ASSERT_EQ(slices.size(), 1u);
    EXPECT_EQ(slices[0].start, 0u);
    EXPECT_EQ(slices[0].count, 10u);
    EXPECT_EQ(slices[0].materialIndex, 0);
}

TEST(MeshMaterialPartitionerTest, ZeroMaterialsProducesOneLeftoverBucketCoveringEverything)
{
    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(12, {});

    ASSERT_EQ(slices.size(), 1u);
    EXPECT_EQ(slices[0].start, 0u);
    EXPECT_EQ(slices[0].count, 12u);
    EXPECT_EQ(slices[0].materialIndex, -1);
}

TEST(MeshMaterialPartitionerTest, ZeroIndexCountProducesNoSlicesAtAll)
{
    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(0, {});
    EXPECT_TRUE(slices.empty());
}

TEST(MeshMaterialPartitionerTest, MaterialThatStartsPastTheEndProducesNoSliceForIt)
{
    // Material "A" itself has zero indexCount, "B" claims 5, mesh has 5
    // total - "A" should simply produce nothing (not a zero-length slice).
    const std::vector<Material> materials = { MakeMaterial("A", 0), MakeMaterial("B", 5) };

    const std::vector<MeshMaterialSlice> slices = PartitionMeshMaterials(5, materials);

    ASSERT_EQ(slices.size(), 1u);
    EXPECT_EQ(slices[0].materialIndex, 1);
    EXPECT_EQ(slices[0].name, "B");
}

} // namespace
} // namespace gte
