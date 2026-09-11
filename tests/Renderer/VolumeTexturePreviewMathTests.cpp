// Unit tests for the network-impl-6 campaign's Phase 3 CPU oracle
// (src/Renderer/VolumeTexturePreviewMath.h/.cpp) - see
// task_manager/network-impl-6/PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md,
// Step 3.2. No Vulkan/Renderer/live GPU device involved at all - every
// function under test is pure, taking/returning only plain ints/Vec3/float
// values.

#include "Renderer/VolumeTexturePreviewMath.h"

#include <gtest/gtest.h>

#include <cmath>

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
    // The real Atmosphere aerial-perspective volume shape - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 7. Longest axis
    // (width/height, 128) maps to exactly 0.5; depth (32) is 32/128 = 0.25
    // of that -> 0.125.
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

// --- atmosphere-scattering-3 campaign, Phase 1 -----------------------------
// (task_manager/atmosphere-scattering-3/PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md)
// Codifies, as a permanent checked fact, the exact geometry defect
// PHASE0_MASTER_STRATEGY.md's own investigation found:
// ComputeVolumeCameraSetup() scales boxHalfExtents directly proportional to
// raw texel counts, which is CORRECT for a genuine spatial volume but WRONG
// for the Aerial Perspective froxel volume (128x128x32 - see
// AtmosphereLutRenderer.cpp's kAerialPerspectiveVolumeWidth/Height/Depth),
// whose Z axis is a camera-relative DISTANCE SLICE index, not a comparable
// physical length to its X/Y screen-column/row indices. This test is NOT a
// "fails before fix, passes after" regression test - ComputeVolumeCameraSetup()
// itself is NEVER changed by this campaign (see PHASE0's own Locked Design
// Decision 2); it is a CHARACTERIZATION test, permanently documenting the
// generic function's own (still correct, for an ACTUAL spatial volume)
// behavior, so a future reader can see exactly why Phase 2 needed a SECOND,
// separate function instead of just tweaking constants inside this one.
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

// --- atmosphere-scattering-3 campaign, Phase 2 -----------------------------
// (task_manager/atmosphere-scattering-3/PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md)
// ComputeAtmosphereAerialPerspectivePreviewCameraSetup() - the SECOND,
// dedicated camera + proxy-box setup used ONLY for the Aerial Perspective
// froxel volume's own HTTP preview. Deliberately the OPPOSITE of the generic
// function's own behavior for these same dimensions (see Phase 1's own
// characterization test above) - depth must now be the visually DOMINANT
// axis, not the smallest.

TEST(VolumeTexturePreviewMathTest, AtmosphereAerialPerspectivePreviewProducesADepthDominantBox)
{
    const VolumeCameraSetup setup = ComputeAtmosphereAerialPerspectivePreviewCameraSetup(128, 128, 32);
    // The OPPOSITE of the generic function's own behavior for these same
    // dimensions (see PHASE1's characterization test) - depth must now be
    // the LARGEST axis, not the smallest.
    EXPECT_GT(setup.boxHalfExtents.z, setup.boxHalfExtents.x);
    EXPECT_GT(setup.boxHalfExtents.z, setup.boxHalfExtents.y);
}

TEST(VolumeTexturePreviewMathTest, AtmosphereAerialPerspectivePreviewCameraBasisIsOrthonormal)
{
    const VolumeCameraSetup setup = ComputeAtmosphereAerialPerspectivePreviewCameraSetup(128, 128, 32);
    EXPECT_NEAR(Length(setup.forward), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(setup.right), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(setup.up), 1.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.forward, setup.right), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.forward, setup.up), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(setup.right, setup.up), 0.0f, 1e-4f);
}

TEST(VolumeTexturePreviewMathTest, AtmosphereAerialPerspectivePreviewEyeSitsOutsideTheBoundingSphere)
{
    const VolumeCameraSetup setup = ComputeAtmosphereAerialPerspectivePreviewCameraSetup(128, 128, 32);
    const float boundingRadius = Length(setup.boxHalfExtents);
    EXPECT_GT(Length(setup.eyePosition), boundingRadius);
}

} // namespace
} // namespace gte
