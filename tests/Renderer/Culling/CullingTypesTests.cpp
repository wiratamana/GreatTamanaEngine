// Tier-1 tests for the Vulkan-free culling math in src/Renderer/Culling/CullingTypes.h,
// PartLocalBounds() and IndirectDrawCommand's byte size.

#include "Renderer/Culling/CullingTypes.h"
#include "Renderer/IndirectDrawTypes.h"

#include "Game/Instantiation/MeshPartBounds.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>

namespace gte {
namespace {

// --- ComputeLocalAABB() ----------------------------------------------------

TEST(CullingTypes, ComputeLocalAABBOfEmptyInputIsDegenerateZeroBoxAtOrigin)
{
    const std::vector<Vec3> positions;
    const AABB aabb = ComputeLocalAABB(positions);

    EXPECT_TRUE(ApproximatelyEqual(aabb.min, Vec3::Zero()));
    EXPECT_TRUE(ApproximatelyEqual(aabb.max, Vec3::Zero()));
}

TEST(CullingTypes, ComputeLocalAABBOfSinglePointIsThatPointTwice)
{
    const std::vector<Vec3> positions = { Vec3(2.0f, 3.0f, 4.0f) };
    const AABB aabb = ComputeLocalAABB(positions);

    EXPECT_TRUE(ApproximatelyEqual(aabb.min, Vec3(2.0f, 3.0f, 4.0f)));
    EXPECT_TRUE(ApproximatelyEqual(aabb.max, Vec3(2.0f, 3.0f, 4.0f)));
}

TEST(CullingTypes, ComputeLocalAABBOfCubeCornersProducesExpectedTightBox)
{
    // The 8 corners of a [-1,1]^3 cube, deliberately shuffled (not in any
    // convenient min-to-max order) to prove this is a genuine min/max scan,
    // not an accidental "first/last element" shortcut.
    const std::vector<Vec3> positions = {
        Vec3(1.0f, -1.0f, 1.0f),
        Vec3(-1.0f, 1.0f, -1.0f),
        Vec3(1.0f, 1.0f, 1.0f),
        Vec3(-1.0f, -1.0f, -1.0f),
        Vec3(1.0f, 1.0f, -1.0f),
        Vec3(-1.0f, -1.0f, 1.0f),
        Vec3(1.0f, -1.0f, -1.0f),
        Vec3(-1.0f, 1.0f, 1.0f),
    };

    const AABB aabb = ComputeLocalAABB(positions);

    EXPECT_TRUE(ApproximatelyEqual(aabb.min, Vec3(-1.0f, -1.0f, -1.0f)));
    EXPECT_TRUE(ApproximatelyEqual(aabb.max, Vec3(1.0f, 1.0f, 1.0f)));
}

// --- TransformAABB() --------------------------------------------------------

TEST(CullingTypes, TransformAABBByIdentityIsANoOp)
{
    const AABB localAABB{ Vec3(-1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f) };
    const AABB worldAABB = TransformAABB(localAABB, Mat4::Identity());

    EXPECT_TRUE(ApproximatelyEqual(worldAABB.min, localAABB.min));
    EXPECT_TRUE(ApproximatelyEqual(worldAABB.max, localAABB.max));
}

TEST(CullingTypes, TransformAABBByPureTranslationShiftsMinAndMaxByExactlyThatAmount)
{
    const AABB localAABB{ Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f) };
    const Mat4 worldMatrix = Mat4::Translation(Vec3(5.0f, -3.0f, 2.0f));

    const AABB worldAABB = TransformAABB(localAABB, worldMatrix);

    EXPECT_TRUE(ApproximatelyEqual(worldAABB.min, Vec3(4.0f, -4.0f, 1.0f)));
    EXPECT_TRUE(ApproximatelyEqual(worldAABB.max, Vec3(6.0f, -2.0f, 3.0f)));
}

TEST(CullingTypes, TransformAABBBy90DegreeYawProducesExpectedReorientedBox)
{
    // +90 degrees around Up() maps (x,y,z) -> (z,y,-x); the asymmetric box rules out a symmetry pass.
    const AABB localAABB{ Vec3(-0.5f, -1.0f, -2.0f), Vec3(0.5f, 1.0f, 2.0f) };
    const Mat4 worldMatrix = Mat4::FromQuat(Quat::FromAxisAngle(Vec3::Up(), kHalfPi));

    const AABB worldAABB = TransformAABB(localAABB, worldMatrix);

    EXPECT_TRUE(ApproximatelyEqual(worldAABB.min, Vec3(-2.0f, -1.0f, -0.5f), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(worldAABB.max, Vec3(2.0f, 1.0f, 0.5f), 1e-4f));
}

// --- ExtractFrustumPlanes() -------------------------------------------------
// Identity view (camera at the origin looking down +Z) with a simple orthographic projection.
TEST(CullingTypes, ExtractFrustumPlanesFromOrthographicProjectionMatchesHandComputedValues)
{
    const Mat4 viewProjection = Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f);

    const std::array<Plane, 6> planes = ExtractFrustumPlanes(viewProjection);

    // [0] Left: x >= -1
    EXPECT_TRUE(ApproximatelyEqual(planes[0].normal, Vec3(1.0f, 0.0f, 0.0f), 1e-4f));
    EXPECT_NEAR(planes[0].distance, 1.0f, 1e-4f);

    // [1] Right: x <= 1
    EXPECT_TRUE(ApproximatelyEqual(planes[1].normal, Vec3(-1.0f, 0.0f, 0.0f), 1e-4f));
    EXPECT_NEAR(planes[1].distance, 1.0f, 1e-4f);

    // [2] Bottom: y >= -2
    EXPECT_TRUE(ApproximatelyEqual(planes[2].normal, Vec3(0.0f, 1.0f, 0.0f), 1e-4f));
    EXPECT_NEAR(planes[2].distance, 2.0f, 1e-4f);

    // [3] Top: y <= 2
    EXPECT_TRUE(ApproximatelyEqual(planes[3].normal, Vec3(0.0f, -1.0f, 0.0f), 1e-4f));
    EXPECT_NEAR(planes[3].distance, 2.0f, 1e-4f);

    // [4] Near (Vulkan zero-to-one depth range): z >= 0.5
    EXPECT_TRUE(ApproximatelyEqual(planes[4].normal, Vec3(0.0f, 0.0f, 1.0f), 1e-4f));
    EXPECT_NEAR(planes[4].distance, -0.5f, 1e-4f);

    // [5] Far: z <= 10
    EXPECT_TRUE(ApproximatelyEqual(planes[5].normal, Vec3(0.0f, 0.0f, -1.0f), 1e-4f));
    EXPECT_NEAR(planes[5].distance, 10.0f, 1e-4f);
}

// --- TryExtractFrustumPlanes() ----------------------------------------------

TEST(CullingTypes, TryExtractFrustumPlanesMatchesExtractFrustumPlanesForAFiniteMatrix)
{
    const Mat4 viewProjection = Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f);

    const std::optional<std::array<Plane, 6>> planes = TryExtractFrustumPlanes(viewProjection);
    ASSERT_TRUE(planes.has_value());

    const std::array<Plane, 6> expected = ExtractFrustumPlanes(viewProjection);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual((*planes)[i].normal, expected[i].normal));
        EXPECT_FLOAT_EQ((*planes)[i].distance, expected[i].distance);
    }
}

TEST(CullingTypes, TryExtractFrustumPlanesRejectsNonFiniteAndZeroMatrices)
{
    Mat4 withNaN = Mat4::Identity();
    withNaN(0, 0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(TryExtractFrustumPlanes(withNaN).has_value());

    Mat4 withInf = Mat4::Identity();
    withInf(1, 3) = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(TryExtractFrustumPlanes(withInf).has_value());

    EXPECT_FALSE(TryExtractFrustumPlanes(Mat4::Zero()).has_value());
}

// --- IsBoundsOutsideFrustum() ------------------------------------------------

TEST(CullingTypes, IsBoundsOutsideFrustumClassifiesInsideOutsideAndStraddlingBoxes)
{
    const std::array<Plane, 6> planes =
        ExtractFrustumPlanes(Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f));
    const std::optional<AABB> box = AABB{ Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f) };

    EXPECT_FALSE(IsBoundsOutsideFrustum(planes, box, Mat4::Translation(Vec3(0.0f, 0.0f, 5.0f)), 0.0f));
    EXPECT_TRUE(IsBoundsOutsideFrustum(planes, box, Mat4::Translation(Vec3(10.0f, 0.0f, 5.0f)), 0.0f));
    EXPECT_FALSE(IsBoundsOutsideFrustum(planes, box, Mat4::Translation(Vec3(1.0f, 0.0f, 5.0f)), 0.0f));
}

TEST(CullingTypes, IsBoundsOutsideFrustumKeepsBoxesWithoutBoundsOrWithNonFiniteWorld)
{
    const std::array<Plane, 6> planes =
        ExtractFrustumPlanes(Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f));
    const Mat4 distant = Mat4::Translation(Vec3(100.0f, 0.0f, 5.0f));

    EXPECT_FALSE(IsBoundsOutsideFrustum(planes, std::nullopt, distant, 0.0f));

    const std::optional<AABB> box = AABB{ Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f) };
    const Mat4 broken = Mat4::Translation(Vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 5.0f));
    EXPECT_FALSE(IsBoundsOutsideFrustum(planes, box, broken, 0.0f));
}

TEST(CullingTypes, IsBoundsOutsideFrustumPaddingKeepsANearMissingBox)
{
    const std::array<Plane, 6> planes =
        ExtractFrustumPlanes(Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f));
    const std::optional<AABB> box = AABB{ Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f) };
    const Mat4 world = Mat4::Translation(Vec3(1.7f, 0.0f, 5.0f));

    EXPECT_TRUE(IsBoundsOutsideFrustum(planes, box, world, 0.0f));
    EXPECT_FALSE(IsBoundsOutsideFrustum(planes, box, world, 0.5f));
}

// --- PartLocalBounds() ---------------------------------------------------------

TEST(CullingTypes, PartLocalBoundsIsEmptyForSkinnedPartsAndTightForStaticParts)
{
    const std::vector<Vec3> positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 2.0f, 3.0f), Vec3(5.0f, 5.0f, 5.0f) };
    const std::vector<std::uint32_t> indices = { 0, 1 };

    EXPECT_FALSE(PartLocalBounds(true, positions, indices).has_value());

    const std::optional<AABB> bounds = PartLocalBounds(false, positions, indices);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_TRUE(ApproximatelyEqual(bounds->min, Vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(bounds->max, Vec3(1.0f, 2.0f, 3.0f)));
}

// --- IndirectDrawCommand (src/Renderer/IndirectDrawTypes.h) -----------------
//
// The static_asserts live in IndirectDrawTypes.h; sizeof() here forces this TU to see them.
TEST(CullingTypes, IndirectDrawCommandMatchesDocumentedByteSize)
{
    EXPECT_EQ(sizeof(IndirectDrawCommand), 20u);
}

} // namespace
} // namespace gte
