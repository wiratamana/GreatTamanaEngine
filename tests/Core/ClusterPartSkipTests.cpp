// Tier-1 tests for the part skip: union of instance boxes, the skip decision and the claim of skipped entities.
// The skip must never remove a part that one cluster test would keep. No device involved.

#include "Core/FeatureKit/ClusterPartPlanning.h"
#include "Math/Quat.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace gte::fx {
namespace {

using Planes = std::optional<std::array<Plane, 6>>;

constexpr ClusterCullFlags kFrustumOnly = ClusterCullFlags::Frustum;

AABB Box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    return AABB{ Vec3(x0, y0, z0), Vec3(x1, y1, z1) };
}

Planes OrthoPlanes()
{
    return TryExtractFrustumPlanes(Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f));
}

Planes PerspectivePlanes(const Vec3& eye, const Vec3& forward)
{
    const Mat4 view = Mat4::LookAtLH(eye, eye + forward, Vec3::Up());
    return TryExtractFrustumPlanes(Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, 100.0f) * view);
}

ClusterPartBox Union(const std::vector<AABB>& boxes)
{
    std::vector<std::uint32_t> indices(boxes.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<std::uint32_t>(i);
    }
    return UnionInstanceBoxes(boxes, indices);
}

TEST(ClusterPartSkipTest, UnionOfNoInstancesIsUnbounded)
{
    EXPECT_FALSE(Union({}).bounded);
}

TEST(ClusterPartSkipTest, UnionOfOneInstanceIsThatBox)
{
    const ClusterPartBox part = Union({ Box(1, 2, 3, 4, 5, 6) });
    ASSERT_TRUE(part.bounded);
    EXPECT_EQ(part.box.min.x, 1.0f);
    EXPECT_EQ(part.box.max.z, 6.0f);
}

TEST(ClusterPartSkipTest, UnionOfManyInstancesCoversAll)
{
    const ClusterPartBox part = Union({ Box(0, 0, 0, 1, 1, 1), Box(-3, 2, 0, -2, 3, 1), Box(0, 0, 5, 1, 1, 9) });
    ASSERT_TRUE(part.bounded);
    EXPECT_EQ(part.box.min.x, -3.0f);
    EXPECT_EQ(part.box.max.y, 3.0f);
    EXPECT_EQ(part.box.max.z, 9.0f);
}

TEST(ClusterPartSkipTest, UnionWithAnUnusableInstanceIsUnbounded)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float huge = std::numeric_limits<float>::max();
    EXPECT_FALSE(Union({ Box(0, 0, 0, 1, 1, 1), Box(nan, 0, 0, 1, 1, 1) }).bounded);
    EXPECT_FALSE(Union({ Box(0, 0, 0, 1, 1, 1), Box(huge, huge, huge, -huge, -huge, -huge) }).bounded);

    const std::vector<AABB> boxes{ Box(0, 0, 0, 1, 1, 1) };
    const std::array<std::uint32_t, 1> outOfRange{ 5 };
    EXPECT_FALSE(UnionInstanceBoxes(boxes, outOfRange).bounded);
}

TEST(ClusterPartSkipTest, APartClearlyOutsideIsSkippedAndAnInsidePartIsNot)
{
    const Planes planes = OrthoPlanes();
    ASSERT_TRUE(planes.has_value());
    const ClusterPartBox outside = Union({ Box(5, -0.5f, 1, 6, 0.5f, 2) });
    const ClusterPartBox inside = Union({ Box(-0.5f, -0.5f, 1, 0.5f, 0.5f, 2) });
    EXPECT_TRUE(ShouldSkipClusterPart(planes, kFrustumOnly, outside));
    EXPECT_FALSE(ShouldSkipClusterPart(planes, kFrustumOnly, inside));
}

TEST(ClusterPartSkipTest, APartJustOutsideIsKeptByThePadding)
{
    const Planes planes = OrthoPlanes();
    const ClusterPartBox sliver = Union({ Box(1.000001f, -0.5f, 1, 2.0f, 0.5f, 2) });
    EXPECT_FALSE(ShouldSkipClusterPart(planes, kFrustumOnly, sliver));
}

TEST(ClusterPartSkipTest, NothingIsSkippedWithoutTheFrustumTestPlanesOrBounds)
{
    const Planes planes = OrthoPlanes();
    const ClusterPartBox outside = Union({ Box(5, 0, 1, 6, 1, 2) });
    EXPECT_FALSE(ShouldSkipClusterPart(planes, ClusterCullFlags::None, outside));
    EXPECT_FALSE(ShouldSkipClusterPart(std::nullopt, kFrustumOnly, outside));
    EXPECT_FALSE(ShouldSkipClusterPart(planes, kFrustumOnly, ClusterPartBox{}));

    const std::vector<ClusterMeshGroup> groups{ { MeshToken{ 1, 1 }, {}, ClusterMaterialKind::None, { 0 } } };
    const std::vector<AABB> boxes{ Box(5, 0, 1, 6, 1, 2) };
    EXPECT_EQ(PlanClusterPartSkips(planes, ClusterCullFlags::None, boxes, groups), (std::vector<std::uint8_t>{ 0 }));
    EXPECT_EQ(PlanClusterPartSkips(std::nullopt, kFrustumOnly, boxes, groups), (std::vector<std::uint8_t>{ 0 }));
}

TEST(ClusterPartSkipTest, PerspectiveAndOrthographicPlanesAgreeOnObviousCases)
{
    const Planes perspective = PerspectivePlanes(Vec3(0, 0, 0), Vec3(0, 0, 1));
    ASSERT_TRUE(perspective.has_value());
    EXPECT_FALSE(ShouldSkipClusterPart(perspective, kFrustumOnly, Union({ Box(-1, -1, 10, 1, 1, 12) })));
    EXPECT_TRUE(ShouldSkipClusterPart(perspective, kFrustumOnly, Union({ Box(-1, -1, -12, 1, 1, -10) })));
    EXPECT_TRUE(ShouldSkipClusterPart(perspective, kFrustumOnly, Union({ Box(500, -1, 10, 501, 1, 12) })));
}

TEST(ClusterPartSkipTest, GroupsAreDecidedIndependently)
{
    const Planes planes = OrthoPlanes();
    const std::vector<AABB> boxes{ Box(5, 0, 1, 6, 1, 2), Box(-0.5f, -0.5f, 1, 0.5f, 0.5f, 2), Box(7, 0, 1, 8, 1, 2) };
    const std::vector<ClusterMeshGroup> groups{
        { MeshToken{ 1, 1 }, {}, ClusterMaterialKind::None, { 0, 2 } },
        { MeshToken{ 2, 1 }, {}, ClusterMaterialKind::None, { 1 } },
    };
    EXPECT_EQ(PlanClusterPartSkips(planes, kFrustumOnly, boxes, groups), (std::vector<std::uint8_t>{ 1, 0 }));
}

TEST(ClusterPartSkipTest, ASkippedPartDoesNotTakeASlotFromAVisiblePart)
{
    const Planes planes = OrthoPlanes();
    const std::vector<AABB> boxes{ Box(5, 0, 1, 6, 1, 2), Box(-0.5f, -0.5f, 1, 0.5f, 0.5f, 2) };
    const std::vector<ClusterMeshGroup> groups{
        { MeshToken{ 1, 1 }, {}, ClusterMaterialKind::None, { 0 } },
        { MeshToken{ 2, 1 }, {}, ClusterMaterialKind::None, { 1 } },
    };
    const std::vector<std::uint8_t> skips = PlanClusterPartSkips(planes, kFrustumOnly, boxes, groups);
    std::vector<ClusterPartSize> sizes;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (skips[i] == 0) {
            sizes.push_back(ClusterPartSize{ groups[i].mesh, groups[i].material, 1, 4 });
        }
    }
    const ClusterSlotAssignment assignment = AssignClusterPartSlots(sizes, 1);
    EXPECT_EQ(assignment.assigned.size(), 1u);
    EXPECT_TRUE(assignment.dropped.empty());
}

TEST(ClusterPartSkipTest, SkippedEntitiesAreClaimedUnderTheContextRules)
{
    const std::array<Entity, 2> skipped{ Entity{ 7, 1 }, Entity{ 8, 1 } };
    const std::array<std::span<const Entity>, 0> noParts{};
    const std::array<ClusterStep, 0> noSteps{};

    ClusterClaimContext view{ rg::ViewScope::Primary, false };
    const ClusterRecordOutcome outcome = ClusterOutcomeOf(noParts, view, noSteps, false, skipped);
    EXPECT_EQ(outcome.claimed.size(), 2u);

    ClusterClaimContext shared{ rg::ViewScope::Shared, false };
    EXPECT_TRUE(ClusterOutcomeOf(noParts, shared, noSteps, false, skipped).claimed.empty());
    ClusterClaimContext captured{ rg::ViewScope::Primary, true };
    EXPECT_TRUE(ClusterOutcomeOf(noParts, captured, noSteps, false, skipped).claimed.empty());
}

TEST(ClusterPartSkipTest, TableBoundsCoverEveryClusterAndRejectBadBoxes)
{
    MeshCluster a{};
    a.aabbMin[0] = -1.0f;
    a.aabbMax[0] = 1.0f;
    MeshCluster b{};
    b.aabbMin[1] = -4.0f;
    b.aabbMax[2] = 3.0f;
    const std::array<MeshCluster, 2> table{ a, b };
    const std::optional<AABB> bounds = ClusterTableBounds(table);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->min.x, -1.0f);
    EXPECT_EQ(bounds->min.y, -4.0f);
    EXPECT_EQ(bounds->max.z, 3.0f);

    EXPECT_FALSE(ClusterTableBounds({}).has_value());
    b.aabbMax[1] = std::numeric_limits<float>::quiet_NaN();
    const std::array<MeshCluster, 2> broken{ a, b };
    EXPECT_FALSE(ClusterTableBounds(broken).has_value());
}

// A skipped part must have every cluster of every instance culled by the per-cluster oracle.
TEST(ClusterPartSkipTest, ASkippedPartHasEveryClusterCulledByTheOracle)
{
    std::mt19937 rng(1234);
    const auto uniform = [&rng](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    std::uint32_t skippedParts = 0;
    std::uint32_t keptParts = 0;

    for (int trial = 0; trial < 400; ++trial) {
        std::vector<MeshCluster> table(1 + static_cast<std::size_t>(uniform(0.0f, 5.99f)));
        for (MeshCluster& cluster : table) {
            const float extent = uniform(0.1f, 2.0f);
            for (int axis = 0; axis < 3; ++axis) {
                const float lo = uniform(-2.0f, 2.0f);
                cluster.aabbMin[axis] = lo;
                cluster.aabbMax[axis] = lo + extent;
            }
        }
        const std::optional<AABB> local = ClusterTableBounds(table);
        ASSERT_TRUE(local.has_value());

        const Vec3 centre(uniform(-80.0f, 80.0f), uniform(-80.0f, 80.0f), uniform(-80.0f, 80.0f));
        std::vector<Mat4> worlds;
        std::vector<AABB> boxes;
        const int instanceCount = 1 + static_cast<int>(uniform(0.0f, 7.99f));
        for (int i = 0; i < instanceCount; ++i) {
            const Vec3 position(centre.x + uniform(-3.0f, 3.0f), centre.y + uniform(-3.0f, 3.0f),
                centre.z + uniform(-3.0f, 3.0f));
            const Quat rotation = Quat::FromAxisAngle(Vec3::Up(), uniform(0.0f, 6.28f));
            const float scale = uniform(0.2f, 3.0f);
            worlds.push_back(Mat4::TRS(position, rotation, Vec3(scale, scale, scale)));
            boxes.push_back(TransformAABB(*local, worlds.back()));
        }

        const Vec3 forward(uniform(-1.0f, 1.0f), uniform(-0.5f, 0.5f), uniform(0.3f, 1.0f));
        const Planes planes = (trial % 4 == 0)
            ? TryExtractFrustumPlanes(Mat4::OrthographicLH_ZO(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f))
            : PerspectivePlanes(Vec3(uniform(-20.0f, 20.0f), uniform(-20.0f, 20.0f), uniform(-20.0f, 20.0f)), forward);
        ASSERT_TRUE(planes.has_value());

        if (!ShouldSkipClusterPart(planes, kFrustumOnly, Union(boxes))) {
            ++keptParts;
            continue;
        }
        ++skippedParts;
        for (const Mat4& world : worlds) {
            for (const MeshCluster& cluster : table) {
                EXPECT_TRUE(ClusterFrustumCulled(cluster, world, *planes)) << "trial " << trial;
            }
        }
    }
    EXPECT_GT(skippedParts, 20u);
    EXPECT_GT(keptParts, 20u);
}

} // namespace
} // namespace gte::fx
