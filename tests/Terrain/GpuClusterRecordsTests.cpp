// Tier 1: cluster table of the node pool against the real validator and the CPU cull oracle.

#include "Assets/MeshClusterValidation.h"
#include "Renderer/Culling/ClusterCullMath.h"
#include "Renderer/Culling/CullingTypes.h"
#include "Terrain/Gpu/TerrainClusterRecords.h"
#include "Terrain/Gpu/TerrainNodePacking.h"
#include "Terrain/TerrainHeightField.h"
#include "Terrain/TerrainNode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace gte::terrain::gpu {

namespace {

PoolLayout Layout(std::uint32_t slots)
{
    return *MakePoolLayout(64, slots);
}

BoxF Box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    BoxF b;
    b.min[0] = x0; b.min[1] = y0; b.min[2] = z0;
    b.max[0] = x1; b.max[1] = y1; b.max[2] = z1;
    return b;
}

// Camera at `eye` looking along +Z with a 60 degree vertical field of view.
std::array<gte::Plane, 6> ForwardPlanes(const Vec3& eye, float farZ)
{
    const Mat4 projection = Mat4::PerspectiveFovLH_ZO(1.0471976f, 16.0f / 9.0f, 3.0f, farZ, true);
    const Mat4 view = Mat4::LookToLH(eye, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f));
    return ::gte::ExtractFrustumPlanes(projection * view);
}

} // namespace

TEST(GpuClusterRecordsTests, InitialTableIsValidAndTilesTheWholeIndexBuffer)
{
    const PoolLayout layout = Layout(96);
    ClusterTableShadow table(layout);
    std::string why;
    EXPECT_TRUE(ValidateClusterTable(table.Records(), layout.TotalIndices(), &why)) << why;
    EXPECT_EQ(table.LiveCount(), 0u);
}

TEST(GpuClusterRecordsTests, TableStaysValidWithLiveAndInactiveSlotsMixed)
{
    const PoolLayout layout = Layout(40);
    ClusterTableShadow table(layout);
    for (std::uint32_t slot = 0; slot < layout.slotCount; slot += 3) {
        ASSERT_TRUE(table.SetLive(slot, Box(100.0f * slot, -20.0f, 50.0f, 100.0f * slot + 128.0f, 300.0f, 178.0f)));
    }
    ASSERT_TRUE(table.SetInactive(3));
    std::string why;
    EXPECT_TRUE(ValidateClusterTable(table.Records(), layout.TotalIndices(), &why)) << why;
    EXPECT_EQ(table.LiveCount(), 13u) << "14 slots were made live, one of them was switched off again";
    EXPECT_TRUE(table.IsLive(0));
    EXPECT_FALSE(table.IsLive(1));
}

TEST(GpuClusterRecordsTests, RecordsCarryTheSlotIndexRangeAndNoCone)
{
    const PoolLayout layout = Layout(8);
    const MeshCluster live = MakeLiveRecord(5, layout, Box(0, 0, 0, 10, 20, 30));
    EXPECT_EQ(live.firstIndex, 5u * layout.indicesPerSlot);
    EXPECT_EQ(live.indexCount, layout.indicesPerSlot);
    EXPECT_EQ(live.cone[3], 1.0f) << "cutoff 1 = no back-face cone";
    EXPECT_FLOAT_EQ(live.sphere[0], 5.0f);
    EXPECT_FLOAT_EQ(live.sphere[1], 10.0f);
    EXPECT_NEAR(live.sphere[3], std::sqrt(25.0f + 100.0f + 225.0f), 1e-4f);

    const MeshCluster idle = MakeInactiveRecord(2, layout);
    EXPECT_EQ(idle.firstIndex, 2u * layout.indicesPerSlot);
    EXPECT_EQ(idle.aabbMin[1], kInactiveY);
    EXPECT_EQ(idle.aabbMax[1], kInactiveY);
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_TRUE(std::isfinite(idle.aabbMin[axis]));
        EXPECT_TRUE(std::isfinite(idle.aabbMax[axis]));
    }
}

TEST(GpuClusterRecordsTests, UnusableBoxesAreRefused)
{
    ClusterTableShadow table(Layout(4));
    BoxF inverted = Box(10, 0, 0, 5, 1, 1);
    BoxF nan = Box(0, 0, 0, 1, 1, 1);
    nan.max[1] = std::nanf("");
    EXPECT_FALSE(table.SetLive(0, inverted));
    EXPECT_FALSE(table.SetLive(0, nan));
    EXPECT_FALSE(table.SetLive(4, Box(0, 0, 0, 1, 1, 1)));
    EXPECT_FALSE(table.SetInactive(4));
    EXPECT_EQ(table.LiveCount(), 0u);
}

TEST(GpuClusterRecordsTests, PoolBoundsHoldEveryLiveAndInactiveBox)
{
    const TerrainSettings s = Sanitize(TerrainSettings{});
    const TerrainLayout world = MakeLayout(s);
    const BoxF bounds = PoolBounds(world.worldHalfExtent, MinHeight(s), MaxHeight(s));

    ClusterTableShadow table(Layout(6));
    table.SetLive(0, Box(-static_cast<float>(world.worldHalfExtent), MinHeight(s) - 400.0f, -static_cast<float>(world.worldHalfExtent),
                         static_cast<float>(world.worldHalfExtent), MaxHeight(s), static_cast<float>(world.worldHalfExtent)));
    const std::optional<AABB> union_ = ClusterTableBounds(table.Records());
    ASSERT_TRUE(union_.has_value());
    EXPECT_GE(union_->min.x, bounds.min[0]);
    EXPECT_GE(union_->min.y, bounds.min[1]);
    EXPECT_GE(union_->min.z, bounds.min[2]);
    EXPECT_LE(union_->max.x, bounds.max[0]);
    EXPECT_LE(union_->max.y, bounds.max[1]);
    EXPECT_LE(union_->max.z, bounds.max[2]);
}

TEST(GpuClusterRecordsTests, InactiveSlotsAreRejectedByTheCpuOracleForEveryTypicalCamera)
{
    const PoolLayout layout = Layout(4);
    const MeshCluster idle = MakeInactiveRecord(1, layout);
    const Vec3 eyes[] = { Vec3(0, 400, 0), Vec3(15000, 3000, -15000), Vec3(-16000, 10, 16000), Vec3(0, 20000, 0) };
    for (const Vec3& eye : eyes) {
        for (const float farZ : { 2000.0f, 12000.0f, 40000.0f }) {
            EXPECT_TRUE(ClusterFrustumCulled(idle, Mat4::Identity(), ForwardPlanes(eye, farZ)))
                << "eye " << eye.x << "," << eye.y << "," << eye.z << " far " << farZ;
        }
    }
}

TEST(GpuClusterRecordsTests, LiveSlotInFrontIsKeptAndOneBehindTheCameraIsCulled)
{
    const PoolLayout layout = Layout(4);
    const auto planes = ForwardPlanes(Vec3(0, 300, 0), 12000.0f);
    const MeshCluster front = MakeLiveRecord(0, layout, Box(-64, 0, 500, 64, 200, 628));
    const MeshCluster behind = MakeLiveRecord(1, layout, Box(-64, 0, -700, 64, 200, -572));
    const MeshCluster tooFar = MakeLiveRecord(2, layout, Box(-64, 0, 20000, 64, 200, 20128));
    EXPECT_FALSE(ClusterFrustumCulled(front, Mat4::Identity(), planes));
    EXPECT_TRUE(ClusterFrustumCulled(behind, Mat4::Identity(), planes));
    EXPECT_TRUE(ClusterFrustumCulled(tooFar, Mat4::Identity(), planes));
}

TEST(GpuClusterRecordsTests, DirtyRangesAreSortedAndMergedAcrossSmallGaps)
{
    ClusterTableShadow table(Layout(64));
    table.TakeDirtyRanges(); // The initial full upload.
    EXPECT_FALSE(table.AnyDirty());

    table.SetLive(2, Box(0, 0, 0, 1, 1, 1));
    table.SetLive(3, Box(0, 0, 0, 1, 1, 1));
    table.SetLive(5, Box(0, 0, 0, 1, 1, 1));
    table.SetLive(40, Box(0, 0, 0, 1, 1, 1));
    ASSERT_TRUE(table.AnyDirty());

    const std::vector<SlotRange> ranges = table.TakeDirtyRanges(2);
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].first, 2u);
    EXPECT_EQ(ranges[0].count, 4u); // 2..5: slot 4 is clean but inside the merged range.
    EXPECT_EQ(ranges[1].first, 40u);
    EXPECT_EQ(ranges[1].count, 1u);
    EXPECT_FALSE(table.AnyDirty());
    EXPECT_TRUE(table.TakeDirtyRanges().empty());
}

TEST(GpuClusterRecordsTests, FirstTakeCoversEverySlotAndMarkAllDirtyRepeatsIt)
{
    ClusterTableShadow table(Layout(10));
    const std::vector<SlotRange> first = table.TakeDirtyRanges();
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].first, 0u);
    EXPECT_EQ(first[0].count, 10u);
    table.MarkAllDirty();
    EXPECT_EQ(table.TakeDirtyRanges().front().count, 10u);
}

TEST(GpuClusterRecordsTests, PackedNodeBoxHoldsEveryVertexOfARealFarAwayNode)
{
    TerrainSettings settings;
    settings.erosionEnabled = false;
    const TerrainHeightField field(Sanitize(settings));
    const NodeKey key{ 120, -125, 0 }; // About 15 km from the origin.
    const NodeMesh node = BuildNode(field, key);

    std::vector<TerrainVertex> packed(node.vertices.size());
    const RampUvMap uv = MakeRampUvMap(128, 4);
    const BoxF box = PackNodeVertices(node, uv, packed);
    ASSERT_TRUE(IsUsableBox(box));
    for (const TerrainVertex& v : packed) {
        for (int axis = 0; axis < 3; ++axis) {
            ASSERT_GE(v.position[axis], box.min[axis]);
            ASSERT_LE(v.position[axis], box.max[axis]);
        }
    }
    EXPECT_GT(box.max[0] - box.min[0], 100.0f);

    // The box is a valid live record of a pool slot.
    ClusterTableShadow table(Layout(3));
    EXPECT_TRUE(table.SetLive(1, box));
    std::string why;
    EXPECT_TRUE(ValidateClusterTable(table.Records(), table.Layout().TotalIndices(), &why)) << why;
}

} // namespace gte::terrain::gpu
