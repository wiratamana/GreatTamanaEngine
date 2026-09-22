// Unit tests for GPU-Driven Frustum Culling + Indirect Draw campaign
// (render-pass-5), PHASE1's pure, Vulkan-device-free bounds/frustum math
// (src/Renderer/Culling/CullingTypes.h) - ComputeLocalAABB()/TransformAABB()/
// ExtractFrustumPlanes()/AABBIntersectsFrustum()/PackCullingInstanceInput(),
// plus IndirectDrawCommand's own documented byte size
// (src/Renderer/IndirectDrawTypes.h). No live VkDevice/Renderer involved at
// all - mirrors tests/Renderer/GpuSkinning/GpuSkinningTypesTests.cpp's own
// precedent exactly.

#include "Renderer/Culling/CullingTypes.h"
#include "Renderer/IndirectDrawTypes.h"

#include "Math/Quat.h"

#include <gtest/gtest.h>

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
    // Rotating +90 degrees around Up() maps (x,y,z) -> (z,y,-x) - matches
    // Quat.h's own documented "rotating Forward() by +90 degrees around
    // Up() yields Right()" convention (hand-verified in
    // tests/Math/QuatTests.cpp), confirmed here for a genuinely asymmetric
    // box (extends further along Z than X before rotating - further along X
    // than Z after) so the test can't pass by symmetry-coincidence.
    const AABB localAABB{ Vec3(-0.5f, -1.0f, -2.0f), Vec3(0.5f, 1.0f, 2.0f) };
    const Mat4 worldMatrix = Mat4::FromQuat(Quat::FromAxisAngle(Vec3::Up(), kHalfPi));

    const AABB worldAABB = TransformAABB(localAABB, worldMatrix);

    EXPECT_TRUE(ApproximatelyEqual(worldAABB.min, Vec3(-2.0f, -1.0f, -0.5f), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(worldAABB.max, Vec3(2.0f, 1.0f, 0.5f), 1e-4f));
}

// --- ExtractFrustumPlanes() -------------------------------------------------
//
// A known, simple, hand-computed case: identity view (camera at the world
// origin, looking down +Z - this engine's own Z-forward convention) composed
// with a simple orthographic projection. See this campaign's
// PHASE1_COMPLETION_REPORT.md for the full hand-derivation these exact
// expected numbers come from (Gribb/Hartmann row extraction against Mat4's
// own OrthographicLH_ZO output).

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

// --- AABBIntersectsFrustum() -------------------------------------------------

class CullingTypesAABBIntersectsFrustumTest : public ::testing::Test {
protected:
    // Same orthographic frustum as ExtractFrustumPlanesFromOrthographic...
    // above: visible region is x in [-1,1], y in [-2,2], z in [0.5,10].
    std::array<Plane, 6> planes = ExtractFrustumPlanes(Mat4::OrthographicLH_ZO(-1.0f, 1.0f, -2.0f, 2.0f, 0.5f, 10.0f));
};

TEST_F(CullingTypesAABBIntersectsFrustumTest, BoxFullyInsideEveryPlaneIntersects)
{
    const AABB box{ Vec3(-0.5f, -1.0f, 1.0f), Vec3(0.5f, 1.0f, 2.0f) };
    EXPECT_TRUE(AABBIntersectsFrustum(box, planes));
}

TEST_F(CullingTypesAABBIntersectsFrustumTest, BoxFullyOutsideRightPlaneDoesNotIntersect)
{
    // Entirely beyond x > 1 (the right plane).
    const AABB box{ Vec3(2.0f, 0.0f, 1.0f), Vec3(3.0f, 1.0f, 2.0f) };
    EXPECT_FALSE(AABBIntersectsFrustum(box, planes));
}

TEST_F(CullingTypesAABBIntersectsFrustumTest, BoxStraddlingRightPlaneIsConservativelyConsideredIntersecting)
{
    // Straddles x == 1 (the right plane) - part inside, part outside.
    const AABB box{ Vec3(0.5f, 0.0f, 1.0f), Vec3(1.5f, 1.0f, 2.0f) };
    EXPECT_TRUE(AABBIntersectsFrustum(box, planes));
}

// --- PackCullingInstanceInput() / static_assert sizes -----------------------

TEST(CullingTypes, PackCullingInstanceInputPacksWorldMatrixAabbAndMeshOffsets)
{
    const Mat4 worldMatrix = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));
    const AABB worldAABB{ Vec3(-1.0f, -2.0f, -3.0f), Vec3(4.0f, 5.0f, 6.0f) };

    const GpuCullingInstanceInput packed = PackCullingInstanceInput(worldMatrix, worldAABB, 10u, 36u, 100);

    // Mat4 is column-major storage and Data() already matches GLSL's std430
    // mat4 layout exactly (see Mat4.h's own class comment / CullingTypes.h's
    // own header comment) - a straight memcpy, confirmed field-for-field
    // here rather than merely assumed.
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(packed.worldMatrix[i], worldMatrix.Data()[i]) << "element " << i;
    }

    EXPECT_FLOAT_EQ(packed.aabbMinX, -1.0f);
    EXPECT_FLOAT_EQ(packed.aabbMinY, -2.0f);
    EXPECT_FLOAT_EQ(packed.aabbMinZ, -3.0f);
    EXPECT_FLOAT_EQ(packed.aabbMaxX, 4.0f);
    EXPECT_FLOAT_EQ(packed.aabbMaxY, 5.0f);
    EXPECT_FLOAT_EQ(packed.aabbMaxZ, 6.0f);

    EXPECT_EQ(packed.firstIndex, 10u);
    EXPECT_EQ(packed.indexCount, 36u);
    EXPECT_EQ(packed.vertexOffset, 100);
}

TEST(CullingTypes, GpuCullingInstanceInputMatchesDocumentedByteSize)
{
    EXPECT_EQ(sizeof(GpuCullingInstanceInput), 112u);
}

// --- IndirectDrawCommand (src/Renderer/IndirectDrawTypes.h) -----------------
//
// Compile-time-only checks already exist directly in IndirectDrawTypes.h as
// static_asserts (byte-for-byte match against VkDrawIndexedIndirectCommand) -
// referencing sizeof() here is what actually forces this translation unit to
// instantiate/see them, mirroring GpuSkinningTypesTests.cpp's own
// "compiling at all is the test" precedent.
TEST(CullingTypes, IndirectDrawCommandMatchesDocumentedByteSize)
{
    EXPECT_EQ(sizeof(IndirectDrawCommand), 20u);
}

} // namespace
} // namespace gte
