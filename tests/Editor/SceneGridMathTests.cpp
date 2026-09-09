// Unit tests for SceneGridMath (src/Editor/SceneGridMath.h) - deliberately
// pure logic with no ImGui/SDL/Vulkan dependency at all (unlike most of
// src/Editor/), so it is Tier-1-testable exactly like EditorCamera (see
// AGENTS.md, "Testability & Regression Safety", and
// tests/Editor/EditorCameraTests.cpp for the pattern this file follows).
// Only actually compiled/linked when GTE_ENABLE_EDITOR is ON, since
// SceneGridMath itself is only ever compiled into gte_core then (see the
// root CMakeLists.txt's "Editor Module Structure") - see tests/CMakeLists.txt.
//
// This file is the hand-checkable "spec" SceneGrid.frag (Phase 2) must
// mirror byte-for-byte - see PHASE1_GRID_MATH_FOUNDATION.md and
// SceneGridMath.h's own file comment for the full rationale ("the CPU path
// is the permanent ORACLE" - AGENTS.md, "GPU Vertex Skinning").

#include "Editor/SceneGridMath.h"

#include "Math/MathTypes.h"

#include <cmath>

#include <gtest/gtest.h>

namespace gte {
namespace {

// ---------------------------------------------------------------------
// ComputeGridPlaneHit
// ---------------------------------------------------------------------

TEST(SceneGridMathTest, PlaneHit_TopDownCameraHitsOriginAtNdcCenter)
{
    // Camera sitting directly above the world origin, looking straight
    // down - up must NOT be parallel to the (0,-1,0) forward direction, so
    // use world +Z as the reference up axis instead.
    const Mat4 view = Mat4::LookAtLH(Vec3{ 0.0f, 10.0f, 0.0f }, Vec3::Zero(), Vec3{ 0.0f, 0.0f, 1.0f });
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 1.0f, 0.1f, 1000.0f, /*flipY=*/false);
    const Mat4 viewProj = proj * view;

    Mat4 invViewProj;
    ASSERT_TRUE(viewProj.TryInverse(invViewProj));

    const GridPlaneHit hit = ComputeGridPlaneHit(invViewProj, viewProj, Vec2{ 0.0f, 0.0f });

    ASSERT_TRUE(hit.valid);
    EXPECT_TRUE(ApproximatelyEqual(hit.worldPosition, Vec3::Zero(), 1e-3f));
    EXPECT_GE(hit.ndcDepth, 0.0f);
    EXPECT_LE(hit.ndcDepth, 1.0f);
}

TEST(SceneGridMathTest, PlaneHit_LevelCameraNeverHitsThePlaneAtAnyNdcX)
{
    // Camera looking exactly level with the horizon (forward has y == 0,
    // up is the exact world up axis) - every pixel along the vertical
    // center row (ndcY == 0) casts a ray that stays exactly parallel to
    // the Y = 0 plane, since only ndcY perturbs a ray's vertical pitch for
    // a standard, non-rolled rectilinear projection.
    const Mat4 view = Mat4::LookAtLH(Vec3{ 0.0f, 5.0f, 0.0f }, Vec3{ 0.0f, 5.0f, 10.0f }, Vec3{ 0.0f, 1.0f, 0.0f });
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 1.0f, 0.1f, 1000.0f, /*flipY=*/false);
    const Mat4 viewProj = proj * view;

    Mat4 invViewProj;
    ASSERT_TRUE(viewProj.TryInverse(invViewProj));

    EXPECT_FALSE(ComputeGridPlaneHit(invViewProj, viewProj, Vec2{ 0.0f, 0.0f }).valid);
    EXPECT_FALSE(ComputeGridPlaneHit(invViewProj, viewProj, Vec2{ -0.8f, 0.0f }).valid);
    EXPECT_FALSE(ComputeGridPlaneHit(invViewProj, viewProj, Vec2{ 0.8f, 0.0f }).valid);
}

TEST(SceneGridMathTest, PlaneHit_PlaneOnlyBehindCameraIsRejected)
{
    // Camera below the plane, looking further downward/away from it - the
    // Y = 0 plane only exists behind this camera's own view direction, so
    // every pixel must be rejected (t <= 0).
    const Mat4 view = Mat4::LookAtLH(Vec3{ 0.0f, -5.0f, 0.0f }, Vec3{ 0.0f, -15.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f });
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 1.0f, 0.1f, 1000.0f, /*flipY=*/false);
    const Mat4 viewProj = proj * view;

    Mat4 invViewProj;
    ASSERT_TRUE(viewProj.TryInverse(invViewProj));

    EXPECT_FALSE(ComputeGridPlaneHit(invViewProj, viewProj, Vec2{ 0.0f, 0.0f }).valid);
    EXPECT_FALSE(ComputeGridPlaneHit(invViewProj, viewProj, Vec2{ 0.3f, 0.2f }).valid);
}

TEST(SceneGridMathTest, PlaneHit_ValidHitReprojectsBackToTheSameNdcPosition)
{
    // The single most important regression this file guards: for a valid
    // hit, re-projecting worldPosition by hand through the SAME viewProj
    // used to unproject it must reproduce the original NDC (x, y) - this
    // is exactly the self-consistency SceneGrid.frag depends on.
    const Mat4 view = Mat4::LookAtLH(Vec3{ 0.0f, 10.0f, 0.0f }, Vec3::Zero(), Vec3{ 0.0f, 0.0f, 1.0f });
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 1.0f, 0.1f, 1000.0f, /*flipY=*/false);
    const Mat4 viewProj = proj * view;

    Mat4 invViewProj;
    ASSERT_TRUE(viewProj.TryInverse(invViewProj));

    const Vec2 ndcXY{ 0.3f, -0.4f };
    const GridPlaneHit hit = ComputeGridPlaneHit(invViewProj, viewProj, ndcXY);
    ASSERT_TRUE(hit.valid);

    const Vec4 clipHit = viewProj * Vec4(hit.worldPosition, 1.0f);
    ASSERT_GT(clipHit.w, kEpsilon);
    const float reprojectedNdcX = clipHit.x / clipHit.w;
    const float reprojectedNdcY = clipHit.y / clipHit.w;
    const float reprojectedNdcDepth = clipHit.z / clipHit.w;

    EXPECT_TRUE(ApproximatelyEqual(reprojectedNdcX, ndcXY.x, 1e-3f));
    EXPECT_TRUE(ApproximatelyEqual(reprojectedNdcY, ndcXY.y, 1e-3f));
    EXPECT_TRUE(ApproximatelyEqual(reprojectedNdcDepth, hit.ndcDepth, 1e-3f));
}

// ---------------------------------------------------------------------
// ComputeGridLineCoverage
// ---------------------------------------------------------------------

TEST(SceneGridMathTest, GridLineCoverage_AtACellBoundaryIsNearFullCoverage)
{
    // worldX sits exactly on a cell boundary (a multiple of cellSize);
    // worldZ sits at a cell CENTER so only the X boundary is being tested.
    const float coverage = ComputeGridLineCoverage(
        /*worldX=*/0.0f, /*worldZ=*/0.5f, /*cellSize=*/1.0f, /*derivativeX=*/0.02f, /*derivativeZ=*/0.02f);
    EXPECT_NEAR(coverage, 1.0f, 1e-4f);
}

TEST(SceneGridMathTest, GridLineCoverage_AtACellCenterIsNearZeroCoverage)
{
    // Both worldX and worldZ sit at a cell center (half a cell away from
    // the nearest boundary in both axes) - nowhere near any line.
    const float coverage = ComputeGridLineCoverage(
        /*worldX=*/0.5f, /*worldZ=*/0.5f, /*cellSize=*/1.0f, /*derivativeX=*/0.01f, /*derivativeZ=*/0.01f);
    EXPECT_NEAR(coverage, 0.0f, 1e-4f);
}

TEST(SceneGridMathTest, GridLineCoverage_NonPositiveCellSizeReturnsExactlyZero)
{
    EXPECT_EQ(ComputeGridLineCoverage(0.0f, 0.0f, 0.0f, 0.01f, 0.01f), 0.0f);
    EXPECT_EQ(ComputeGridLineCoverage(0.0f, 0.0f, -1.0f, 0.01f, 0.01f), 0.0f);
}

TEST(SceneGridMathTest, GridLineCoverage_VeryLargeDerivativeStaysFiniteAndInRange)
{
    // Simulates being zoomed far out - a single grid cell is far smaller
    // than one pixel's own on-screen footprint.
    const float coverage = ComputeGridLineCoverage(
        /*worldX=*/0.3f, /*worldZ=*/0.7f, /*cellSize=*/1.0f, /*derivativeX=*/100.0f, /*derivativeZ=*/100.0f);
    EXPECT_TRUE(std::isfinite(coverage));
    EXPECT_GE(coverage, 0.0f);
    EXPECT_LE(coverage, 1.0f);
}

// ---------------------------------------------------------------------
// ComputeAxisLineCoverage
// ---------------------------------------------------------------------

TEST(SceneGridMathTest, AxisLineCoverage_WellInsideHalfWidthIsExactlyOne)
{
    EXPECT_EQ(ComputeAxisLineCoverage(/*distanceFromAxis=*/0.0f, /*derivative=*/0.001f, /*halfWidthWorld=*/0.05f), 1.0f);
}

TEST(SceneGridMathTest, AxisLineCoverage_SeveralDerivativeWidthsBeyondHalfWidthIsNearZero)
{
    const float coverage = ComputeAxisLineCoverage(
        /*distanceFromAxis=*/0.1f, /*derivative=*/0.01f, /*halfWidthWorld=*/0.05f);
    EXPECT_NEAR(coverage, 0.0f, 1e-4f);
}

TEST(SceneGridMathTest, AxisLineCoverage_NonPositiveHalfWidthReturnsExactlyZero)
{
    EXPECT_EQ(ComputeAxisLineCoverage(0.0f, 0.01f, 0.0f), 0.0f);
    EXPECT_EQ(ComputeAxisLineCoverage(0.0f, 0.01f, -1.0f), 0.0f);
}

TEST(SceneGridMathTest, AxisLineCoverage_NegativeDerivativeMatchesItsAbsoluteMagnitude)
{
    // A negative derivative is a synthetic-test-only input (real fwidth()
    // values are always non-negative) - the std::fabs() in this function's
    // own implementation must make the result identical either way.
    const float withPositiveDerivative = ComputeAxisLineCoverage(
        /*distanceFromAxis=*/0.055f, /*derivative=*/0.01f, /*halfWidthWorld=*/0.05f);
    const float withNegativeDerivative = ComputeAxisLineCoverage(
        /*distanceFromAxis=*/0.055f, /*derivative=*/-0.01f, /*halfWidthWorld=*/0.05f);

    EXPECT_TRUE(ApproximatelyEqual(withPositiveDerivative, withNegativeDerivative));
    // Also confirm this is a genuine PARTIAL-coverage case (not a
    // degenerate 0.0/1.0 clamp on both sides), so the parity check above
    // is actually exercising the fabs() path.
    EXPECT_GT(withPositiveDerivative, 0.0f);
    EXPECT_LT(withPositiveDerivative, 1.0f);
}

} // namespace
} // namespace gte
