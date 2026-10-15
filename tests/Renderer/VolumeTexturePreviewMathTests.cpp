// Tier-1 tests for the volume preview CPU oracle (src/Renderer/VolumeTexturePreviewMath.h): pure math, no GPU.

#include "Renderer/VolumeTexturePreviewMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

namespace gte {
namespace {

constexpr float kEpsilon = 1e-4f;

// --- ComputeVolumeCameraSetup() --------------------------------------------

TEST(VolumeTexturePreviewMathTest, PerfectCubeProducesExactHalfExtents)
{
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 128);
    EXPECT_NEAR(setup.boxHalfExtents.x, 0.5f, kEpsilon);
    EXPECT_NEAR(setup.boxHalfExtents.y, 0.5f, kEpsilon);
    EXPECT_NEAR(setup.boxHalfExtents.z, 0.5f, kEpsilon);
}

TEST(VolumeTexturePreviewMathTest, NonCubicVolumeScalesShorterAxesProportionally)
{
    // Longest axis (128) maps to 0.5; depth (32) is a quarter of that: 0.125.
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 32);
    EXPECT_NEAR(setup.boxHalfExtents.x, 0.5f, kEpsilon);
    EXPECT_NEAR(setup.boxHalfExtents.y, 0.5f, kEpsilon);
    EXPECT_NEAR(setup.boxHalfExtents.z, 0.125f, kEpsilon);
}

TEST(VolumeTexturePreviewMathTest, CameraBasisIsOrthonormal)
{
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 32);

    EXPECT_NEAR(Length(setup.forward), 1.0f, kEpsilon);
    EXPECT_NEAR(Length(setup.right), 1.0f, kEpsilon);
    EXPECT_NEAR(Length(setup.up), 1.0f, kEpsilon);

    EXPECT_NEAR(Dot(setup.forward, setup.right), 0.0f, kEpsilon);
    EXPECT_NEAR(Dot(setup.forward, setup.up), 0.0f, kEpsilon);
    EXPECT_NEAR(Dot(setup.right, setup.up), 0.0f, kEpsilon);
}

TEST(VolumeTexturePreviewMathTest, EyeSitsStrictlyOutsideTheBoundingSphere)
{
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 32);
    EXPECT_GT(Length(setup.eyePosition), Length(setup.boxHalfExtents));
}

TEST(VolumeTexturePreviewMathTest, EveryBoxCornerIsReachableFromTheEye)
{
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 32);
    const Vec3& e = setup.boxHalfExtents;

    for (int cx = -1; cx <= 1; cx += 2) {
        for (int cy = -1; cy <= 1; cy += 2) {
            for (int cz = -1; cz <= 1; cz += 2) {
                const Vec3 corner(static_cast<float>(cx) * e.x, static_cast<float>(cy) * e.y,
                    static_cast<float>(cz) * e.z);
                const Vec3 rayDir = Normalize(corner - setup.eyePosition);

                float tEnter = 0.0f;
                float tExit = 0.0f;
                const bool hit = IntersectRayBox(setup.eyePosition, rayDir, setup.boxHalfExtents, tEnter, tExit);
                ASSERT_TRUE(hit) << "corner (" << cx << "," << cy << "," << cz << ")";
                EXPECT_GE(tExit, tEnter) << "corner (" << cx << "," << cy << "," << cz << ")";
                EXPECT_GE(tEnter, 0.0f) << "corner (" << cx << "," << cy << "," << cz << ")";
            }
        }
    }
}

// --- IntersectRayBox() ------------------------------------------------------

TEST(VolumeTexturePreviewMathTest, RayFromOutsidePointedAtCenterHitsWithPositiveTEnter)
{
    const Vec3 boxHalfExtents(0.5f, 0.5f, 0.5f);
    const Vec3 rayOrigin(0.0f, 0.0f, -5.0f);
    const Vec3 rayDir = Normalize(Vec3::Zero() - rayOrigin);

    float tEnter = 0.0f;
    float tExit = 0.0f;
    ASSERT_TRUE(IntersectRayBox(rayOrigin, rayDir, boxHalfExtents, tEnter, tExit));
    EXPECT_GT(tEnter, 0.0f);
    EXPECT_GT(tExit, tEnter);
}

TEST(VolumeTexturePreviewMathTest, RayStartingInsideTheBoxHasNonPositiveTEnter)
{
    const Vec3 boxHalfExtents(0.5f, 0.5f, 0.5f);
    const Vec3 rayOrigin(0.0f, 0.0f, 0.0f);
    const Vec3 rayDir = Vec3::Forward();

    float tEnter = 0.0f;
    float tExit = 0.0f;
    ASSERT_TRUE(IntersectRayBox(rayOrigin, rayDir, boxHalfExtents, tEnter, tExit));
    EXPECT_LE(tEnter, 0.0f);
    EXPECT_GE(tExit, 0.0f);
    EXPECT_GE(tExit, tEnter);
}

TEST(VolumeTexturePreviewMathTest, RayThatMissesTheBoxEntirelyReturnsFalse)
{
    const Vec3 boxHalfExtents(0.5f, 0.5f, 0.5f);
    // Parallel to the Z axis, offset well outside the box's X extent.
    const Vec3 rayOrigin(10.0f, 0.0f, -5.0f);
    const Vec3 rayDir = Vec3::Forward();

    float tEnter = 0.0f;
    float tExit = 0.0f;
    EXPECT_FALSE(IntersectRayBox(rayOrigin, rayDir, boxHalfExtents, tEnter, tExit));
}

// Characterization: the generic setup scales the box by raw texel counts. That is right for a spatial volume but
// flattens the aerial perspective volume (128x128x32), whose Z is a distance-slice index, not a length.
TEST(VolumeTexturePreviewMathTest, AerialPerspectiveVolumeDimensionsProduceSeverelyFlattenedDepthAxisUnderGenericFunction)
{
    const VolumeCameraSetup setup = ComputeVolumeCameraSetup(128, 128, 32);

    // X/Y both hit the generic function's own maximum half-extent (0.5),
    // since width == height == the max dimension here.
    EXPECT_NEAR(setup.boxHalfExtents.x, 0.5f, 1e-5f);
    EXPECT_NEAR(setup.boxHalfExtents.y, 0.5f, 1e-5f);
    // Z (the depth/distance axis - the ONLY axis carrying this LUT's
    // near/far story) is squashed to exactly 32/128 = 0.25 of that.
    EXPECT_NEAR(setup.boxHalfExtents.z, 0.125f, 1e-5f);

    // Written as an explicit ratio assertion too, so the "severely
    // flattened" claim is a checked number, not just an eyeballed one.
    const float depthToWidthRatio = setup.boxHalfExtents.z / setup.boxHalfExtents.x;
    EXPECT_LT(depthToWidthRatio, 0.3f); // 0.25 in practice - comfortably confirms the flattening.
}

// --- ComputeAerialPerspectivePreviewCameraSetup() ---------------------------
// The dedicated setup for the aerial perspective volume: depth must be the dominant axis, unlike the generic one.

TEST(VolumeTexturePreviewMathTest, AerialPerspectivePreviewProducesADepthDominantBox)
{
    const VolumeCameraSetup setup = ComputeAerialPerspectivePreviewCameraSetup(128, 128, 32);
    EXPECT_GT(setup.boxHalfExtents.z, setup.boxHalfExtents.x);
    EXPECT_GT(setup.boxHalfExtents.z, setup.boxHalfExtents.y);
}

TEST(VolumeTexturePreviewMathTest, AerialPerspectivePreviewCameraBasisIsOrthonormal)
{
    const VolumeCameraSetup setup = ComputeAerialPerspectivePreviewCameraSetup(128, 128, 32);
    EXPECT_NEAR(Length(setup.forward), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(setup.right), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(setup.up), 1.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.forward, setup.right), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.forward, setup.up), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.right, setup.up), 0.0f, 1e-4f);
}

TEST(VolumeTexturePreviewMathTest, AerialPerspectivePreviewEyeSitsOutsideTheBoundingSphere)
{
    const VolumeCameraSetup setup = ComputeAerialPerspectivePreviewCameraSetup(128, 128, 32);
    const float boundingRadius = Length(setup.boxHalfExtents);
    EXPECT_GT(Length(setup.eyePosition), boundingRadius);
}

// --- IntersectRayFrustum() / MapFrustumLocalPositionToUvw() ---------------

TEST(VolumeTexturePreviewMathTest, IntersectRayFrustumAlongCentralAxisSpansFullDepth)
{
    FrustumProxy frustum;
    frustum.halfDepth = 2.0f;
    frustum.farHalfWidth = 1.0f;
    frustum.farHalfHeight = 1.0f;

    const Vec3 rayOrigin(0.0f, 0.0f, -10.0f);
    const Vec3 rayDirection(0.0f, 0.0f, 1.0f);

    float tEnter = 0.0f;
    float tExit = 0.0f;
    ASSERT_TRUE(IntersectRayFrustum(rayOrigin, rayDirection, frustum, tEnter, tExit));

    // tEnter/tExit are ray-parameter offsets from rayOrigin, not z coordinates.
    EXPECT_NEAR(rayOrigin.z + tEnter, -frustum.halfDepth, kEpsilon);
    EXPECT_NEAR(rayOrigin.z + tExit, frustum.halfDepth, kEpsilon);
    EXPECT_NEAR(tExit - tEnter, 2.0f * frustum.halfDepth, kEpsilon);
}

TEST(VolumeTexturePreviewMathTest, IntersectRayFrustumSideWallEntryAndExitMatchHandComputedT)
{
    FrustumProxy frustum;
    frustum.halfDepth = 1.0f;
    frustum.farHalfWidth = 2.0f;
    frustum.farHalfHeight = 2.0f;

    const Vec3 rayOrigin(3.0f, 0.0f, 0.0f);
    const Vec3 rayDirection(-1.0f, 0.0f, 0.0f);

    float tEnter = 0.0f;
    float tExit = 0.0f;
    ASSERT_TRUE(IntersectRayFrustum(rayOrigin, rayDirection, frustum, tEnter, tExit));

    EXPECT_NEAR(tEnter, 2.0f, kEpsilon);
    EXPECT_NEAR(tExit, 4.0f, kEpsilon);

    const Vec3 entryPoint = rayOrigin + rayDirection * tEnter;
    const Vec3 exitPoint = rayOrigin + rayDirection * tExit;
    EXPECT_NEAR(entryPoint.x, 1.0f, kEpsilon);
    EXPECT_NEAR(entryPoint.y, 0.0f, kEpsilon);
    EXPECT_NEAR(entryPoint.z, 0.0f, kEpsilon);
    EXPECT_NEAR(exitPoint.x, -1.0f, kEpsilon);
    EXPECT_NEAR(exitPoint.y, 0.0f, kEpsilon);
    EXPECT_NEAR(exitPoint.z, 0.0f, kEpsilon);
}

TEST(VolumeTexturePreviewMathTest, IntersectRayFrustumMissingRayReturnsFalse)
{
    FrustumProxy frustum;
    frustum.halfDepth = 2.0f;
    frustum.farHalfWidth = 1.0f;
    frustum.farHalfHeight = 1.0f;

    // Offset in X beyond farHalfWidth (the widest point), parallel to Z: misses at any depth.
    const Vec3 rayOrigin(2.0f, 0.0f, -10.0f);
    const Vec3 rayDirection(0.0f, 0.0f, 1.0f);

    float tEnter = 0.0f;
    float tExit = 0.0f;
    EXPECT_FALSE(IntersectRayFrustum(rayOrigin, rayDirection, frustum, tEnter, tExit));
}

TEST(VolumeTexturePreviewMathTest, IntersectRayFrustumRayFromInsideHasNonPositiveTEnter)
{
    FrustumProxy frustum;
    frustum.halfDepth = 0.5f;
    frustum.farHalfWidth = 0.5f;
    frustum.farHalfHeight = 0.5f;

    const Vec3 rayOrigin(0.0f, 0.0f, 0.0f);
    const Vec3 rayDirection = Vec3::Forward();

    float tEnter = 0.0f;
    float tExit = 0.0f;
    ASSERT_TRUE(IntersectRayFrustum(rayOrigin, rayDirection, frustum, tEnter, tExit));
    EXPECT_LE(tEnter, 0.0f);
    EXPECT_GE(tExit, 0.0f);
    EXPECT_GE(tExit, tEnter);
}

TEST(VolumeTexturePreviewMathTest, MapFrustumLocalPositionToUvwApexMapsToCenterUAndV)
{
    FrustumProxy frustum;
    frustum.halfDepth = 1.0f;
    frustum.farHalfWidth = 2.0f;
    frustum.farHalfHeight = 2.0f;

    // The cross-section at the apex is zero, so only x == y == 0 is inside the frustum there.
    const Vec3 localPos(0.0f, 0.0f, -frustum.halfDepth);
    const Vec3 uvw = MapFrustumLocalPositionToUvw(localPos, frustum);
    EXPECT_NEAR(uvw.x, 0.5f, kEpsilon);
    EXPECT_NEAR(uvw.y, 0.5f, kEpsilon);
}

TEST(VolumeTexturePreviewMathTest, MapFrustumLocalPositionToUvwFarCapCornersMapToUnitSquareCorners)
{
    FrustumProxy frustum;
    frustum.halfDepth = 1.0f;
    frustum.farHalfWidth = 2.0f;
    frustum.farHalfHeight = 2.0f;

    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const Vec3 localPos(static_cast<float>(sx) * frustum.farHalfWidth,
                static_cast<float>(sy) * frustum.farHalfHeight, frustum.halfDepth);
            const Vec3 uvw = MapFrustumLocalPositionToUvw(localPos, frustum);
            const float expectedU = (sx > 0) ? 1.0f : 0.0f;
            const float expectedV = (sy > 0) ? 1.0f : 0.0f;
            EXPECT_NEAR(uvw.x, expectedU, kEpsilon) << "sx=" << sx << " sy=" << sy;
            EXPECT_NEAR(uvw.y, expectedV, kEpsilon) << "sx=" << sx << " sy=" << sy;
        }
    }
}

TEST(VolumeTexturePreviewMathTest, MapFrustumLocalPositionToUvwWMatchesDepthFraction)
{
    FrustumProxy frustum;
    frustum.halfDepth = 2.0f;
    frustum.farHalfWidth = 1.0f;
    frustum.farHalfHeight = 1.0f;

    const float zValues[] = { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f };
    const float expectedW[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (std::size_t i = 0; i < 5; ++i) {
        const Vec3 localPos(0.0f, 0.0f, zValues[i]);
        const Vec3 uvw = MapFrustumLocalPositionToUvw(localPos, frustum);
        EXPECT_NEAR(uvw.z, expectedW[i], kEpsilon) << "z=" << zValues[i];
    }
}

} // namespace
} // namespace gte
