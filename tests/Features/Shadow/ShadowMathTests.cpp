// Unit tests for the Shadow feature's CPU math oracle
// (src/Features/Shadow/ShadowMath.h/.cpp). No Vulkan/Renderer/GPU device
// involved - every function under test is pure.

#include "Features/Shadow/ShadowMath.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(ShadowMathTest, SunStraightOverheadNeverNaNs)
{
    const Mat4 viewProj = BuildDirectionalShadowViewProjection(Vec3::Down(), Vec3::Zero(), 20.0f, 0.1f, 200.0f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_FALSE(std::isnan(viewProj(r, c)));
            EXPECT_FALSE(std::isinf(viewProj(r, c)));
        }
    }
}

TEST(ShadowMathTest, FortyFiveDegreeSunProducesExpectedEyePosition)
{
    const Vec3 directionTowardSun = Normalize(Vec3(0.0f, 1.0f, -1.0f));
    const Vec3 sceneCenter = Vec3::Zero();
    constexpr float halfExtent = 10.0f;
    constexpr float nearZ = 1.0f;

    const Mat4 viewProj =
        BuildDirectionalShadowViewProjection(directionTowardSun, sceneCenter, halfExtent, nearZ, 100.0f);

    Mat4 invViewProj;
    ASSERT_TRUE(viewProj.TryInverse(invViewProj));

    // The light's own eye must project to NDC (0, 0, 0) - the near-plane center.
    const Vec3 reconstructedEye = ReconstructWorldPositionFromDepth(invViewProj, 0.0f, 0.0f, 0.0f);
    const Vec3 expectedEye = sceneCenter - Normalize(-directionTowardSun) * (halfExtent + nearZ);
    EXPECT_NEAR(reconstructedEye.x, expectedEye.x, 1e-3f);
    EXPECT_NEAR(reconstructedEye.y, expectedEye.y, 1e-3f);
    EXPECT_NEAR(reconstructedEye.z, expectedEye.z, 1e-3f);
}

TEST(ShadowMathTest, ReconstructWorldPositionRoundTripsThroughACamera)
{
    const Mat4 view = Mat4::LookAtLH(Vec3(5.0f, 3.0f, -8.0f), Vec3::Zero(), Vec3::Up());
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(1.0472f, 16.0f / 9.0f, 0.1f, 500.0f, /*flipY=*/true);
    const Mat4 viewProj = proj * view;

    Mat4 invViewProj;
    ASSERT_TRUE(viewProj.TryInverse(invViewProj));

    const Vec3 sourcePoint(2.0f, 1.0f, 4.0f);
    const Vec4 clip = viewProj * Vec4(sourcePoint, 1.0f);
    const Vec3 ndc = clip.XYZ() / clip.w;

    const Vec3 reconstructed = ReconstructWorldPositionFromDepth(invViewProj, ndc.x, ndc.y, ndc.z);
    EXPECT_NEAR(reconstructed.x, sourcePoint.x, 1e-2f);
    EXPECT_NEAR(reconstructed.y, sourcePoint.y, 1e-2f);
    EXPECT_NEAR(reconstructed.z, sourcePoint.z, 1e-2f);
}

TEST(ShadowMathTest, SanitizeClampsNonPositiveOrthoHalfExtentToMinimum)
{
    ShadowSettings settings;
    settings.orthoHalfExtentWorld = -5.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).orthoHalfExtentWorld, 0.01f);

    settings.orthoHalfExtentWorld = 0.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).orthoHalfExtentWorld, 0.01f);
}

TEST(ShadowMathTest, SanitizeClampsNonPositiveNearZToMinimum)
{
    ShadowSettings settings;
    settings.nearZ = -1.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).nearZ, 0.01f);
}

TEST(ShadowMathTest, SanitizeDeeplyNegativeFarZStillYieldsAPositiveBoxDepth)
{
    ShadowSettings settings;
    settings.orthoHalfExtentWorld = 5.0f;
    settings.farZ = -1000.0f;

    const ShadowSettings sanitized = SanitizeShadowSettings(settings);
    const float boxDepth = sanitized.orthoHalfExtentWorld * 2.0f + sanitized.farZ;
    EXPECT_GE(boxDepth, 0.01f - 1e-5f);
}

TEST(ShadowMathTest, SanitizeClampsStrengthIntoUnitRange)
{
    ShadowSettings settings;
    settings.strength = -2.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).strength, 0.0f);

    settings.strength = 5.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).strength, 1.0f);
}

TEST(ShadowMathTest, SanitizeIsIdempotentForAlreadyValidSettings)
{
    ShadowSettings settings;
    settings.orthoHalfExtentWorld = 15.0f;
    settings.nearZ = 0.5f;
    settings.farZ = 50.0f;
    settings.strength = 0.4f;

    const ShadowSettings once = SanitizeShadowSettings(settings);
    const ShadowSettings twice = SanitizeShadowSettings(once);
    EXPECT_FLOAT_EQ(once.orthoHalfExtentWorld, twice.orthoHalfExtentWorld);
    EXPECT_FLOAT_EQ(once.nearZ, twice.nearZ);
    EXPECT_FLOAT_EQ(once.farZ, twice.farZ);
    EXPECT_FLOAT_EQ(once.strength, twice.strength);
}

TEST(ShadowMathTest, SanitizeNeverAltersMapResolution)
{
    ShadowSettings settings;
    settings.mapResolution = 777;
    EXPECT_EQ(SanitizeShadowSettings(settings).mapResolution, 777u);
}

} // namespace
} // namespace gte
