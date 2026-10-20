// Unit tests for the Shadow feature's CPU math oracle
// (src/Features/Shadow/ShadowMath.h/.cpp). No Vulkan/Renderer/GPU device
// involved - every function under test is pure.

#include "Features/Shadow/ShadowMath.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace gte {
namespace {

constexpr float kCameraNear = 0.5f;
constexpr float kCameraFar = 100.0f;

// A real perspective camera, run through the same inverse view-projection the renderer hands to the shadow code.
ShadowViewFrustum MakeCameraFrustum(const Vec3& eye, const Vec3& target)
{
    const Mat4 view = Mat4::LookAtLH(eye, target, Vec3::Up());
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(1.0472f, 16.0f / 9.0f, kCameraNear, kCameraFar, /*flipY=*/true);
    Mat4 invViewProj;
    if (!(proj * view).TryInverse(invViewProj)) {
        return ShadowViewFrustum{};
    }
    return ExtractViewFrustum(eye, invViewProj);
}

// Frustum without a matrix, for the depth-range smoothing tests that only read viewNear/viewFar.
ShadowViewFrustum MakeDepthOnlyFrustum(float viewNear, float viewFar)
{
    ShadowViewFrustum frustum;
    frustum.valid = true;
    frustum.viewNear = viewNear;
    frustum.viewFar = viewFar;
    return frustum;
}

bool AllFinite(const Mat4& m)
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m(r, c))) {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// ReconstructWorldPositionFromDepth
// ---------------------------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------------------------
// SanitizeShadowSettings
// ---------------------------------------------------------------------------------------------

TEST(ShadowMathTest, SanitizeClampsCascadeCountIntoOneToMax)
{
    ShadowSettings settings;
    settings.cascadeCount = 0;
    EXPECT_EQ(SanitizeShadowSettings(settings).cascadeCount, 1u);

    settings.cascadeCount = 99;
    EXPECT_EQ(SanitizeShadowSettings(settings).cascadeCount, kMaxShadowCascades);
}

TEST(ShadowMathTest, SanitizeSnapsMapResolutionToTheNearestSupportedSize)
{
    ShadowSettings settings;

    settings.mapResolution = 0;
    EXPECT_EQ(SanitizeShadowSettings(settings).mapResolution, 512u);

    settings.mapResolution = 777;
    EXPECT_EQ(SanitizeShadowSettings(settings).mapResolution, 1024u);

    settings.mapResolution = 2048;
    EXPECT_EQ(SanitizeShadowSettings(settings).mapResolution, 2048u);

    settings.mapResolution = 100000;
    EXPECT_EQ(SanitizeShadowSettings(settings).mapResolution, 4096u);
}

TEST(ShadowMathTest, SanitizeSnapsFilterSamplesToTheNearestSupportedCount)
{
    ShadowSettings settings;

    settings.filterSamples = 0;
    EXPECT_EQ(SanitizeShadowSettings(settings).filterSamples, 1u);

    settings.filterSamples = 5;
    EXPECT_EQ(SanitizeShadowSettings(settings).filterSamples, 4u);

    settings.filterSamples = 12; // Tie between 8 and 16 goes to the larger one.
    EXPECT_EQ(SanitizeShadowSettings(settings).filterSamples, 16u);

    settings.filterSamples = 1000;
    EXPECT_EQ(SanitizeShadowSettings(settings).filterSamples, 16u);
}

TEST(ShadowMathTest, SanitizeClampsStrengthIntoUnitRange)
{
    ShadowSettings settings;
    settings.strength = -2.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).strength, 0.0f);

    settings.strength = 5.0f;
    EXPECT_FLOAT_EQ(SanitizeShadowSettings(settings).strength, 1.0f);
}

TEST(ShadowMathTest, DefaultsApplyFullStrengthInMaterials)
{
    const ShadowSettings defaults;
    EXPECT_FLOAT_EQ(defaults.strength, 1.0f);
    EXPECT_TRUE(defaults.applyInMaterials);

    ShadowSettings settings;
    settings.applyInMaterials = false;
    EXPECT_FALSE(SanitizeShadowSettings(settings).applyInMaterials);
}

TEST(ShadowMathTest, SanitizeClampsDistancesAndFractionsIntoTheirRanges)
{
    ShadowSettings settings;
    settings.maxDistance = -10.0f;
    settings.splitLambda = 4.0f;
    settings.cascadeBlendFraction = 2.0f;
    settings.distanceFadeFraction = -1.0f;
    settings.casterRange = 0.0f;
    settings.sdsmPaddingFraction = 7.0f;
    settings.sdsmQuantizeSteps = 1000;

    const ShadowSettings safe = SanitizeShadowSettings(settings);
    EXPECT_FLOAT_EQ(safe.maxDistance, 1.0f);
    EXPECT_FLOAT_EQ(safe.splitLambda, 1.0f);
    EXPECT_FLOAT_EQ(safe.cascadeBlendFraction, 0.5f);
    EXPECT_FLOAT_EQ(safe.distanceFadeFraction, 0.0f);
    EXPECT_FLOAT_EQ(safe.casterRange, 1.0f);
    EXPECT_FLOAT_EQ(safe.sdsmPaddingFraction, 1.0f);
    EXPECT_EQ(safe.sdsmQuantizeSteps, 16u);
}

TEST(ShadowMathTest, SanitizeReplacesNonFiniteFloatsWithTheirDefaults)
{
    const ShadowSettings defaults;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    ShadowSettings settings;
    settings.maxDistance = nan;
    settings.strength = inf;
    settings.filterRadiusTexels = -inf;

    const ShadowSettings safe = SanitizeShadowSettings(settings);
    EXPECT_FLOAT_EQ(safe.maxDistance, defaults.maxDistance);
    EXPECT_FLOAT_EQ(safe.strength, defaults.strength);
    EXPECT_FLOAT_EQ(safe.filterRadiusTexels, defaults.filterRadiusTexels);
}

TEST(ShadowMathTest, SanitizeResetsAnOutOfRangeDebugViewToOff)
{
    ShadowSettings settings;
    settings.debugView = static_cast<ShadowDebugView>(200);
    EXPECT_EQ(SanitizeShadowSettings(settings).debugView, ShadowDebugView::Off);

    settings.debugView = ShadowDebugView::Mask;
    EXPECT_EQ(SanitizeShadowSettings(settings).debugView, ShadowDebugView::Mask);
}

TEST(ShadowMathTest, SanitizeIsIdempotentForAlreadyValidSettings)
{
    ShadowSettings settings;
    settings.cascadeCount = 3;
    settings.mapResolution = 1024;
    settings.maxDistance = 80.0f;
    settings.strength = 0.4f;
    settings.filterSamples = 16;

    const ShadowSettings once = SanitizeShadowSettings(settings);
    const ShadowSettings twice = SanitizeShadowSettings(once);
    EXPECT_EQ(once.cascadeCount, twice.cascadeCount);
    EXPECT_EQ(once.mapResolution, twice.mapResolution);
    EXPECT_FLOAT_EQ(once.maxDistance, twice.maxDistance);
    EXPECT_FLOAT_EQ(once.strength, twice.strength);
    EXPECT_EQ(once.filterSamples, twice.filterSamples);
    EXPECT_EQ(once.cascadeCount, 3u);
    EXPECT_EQ(once.mapResolution, 1024u);
}

TEST(ShadowMathTest, DefaultSettingsSurviveSanitizeUnchanged)
{
    const ShadowSettings defaults;
    const ShadowSettings safe = SanitizeShadowSettings(defaults);
    EXPECT_EQ(safe.cascadeCount, defaults.cascadeCount);
    EXPECT_EQ(safe.mapResolution, defaults.mapResolution);
    EXPECT_FLOAT_EQ(safe.maxDistance, defaults.maxDistance);
    EXPECT_FLOAT_EQ(safe.splitLambda, defaults.splitLambda);
    EXPECT_FLOAT_EQ(safe.casterRange, defaults.casterRange);
    EXPECT_EQ(safe.filterSamples, defaults.filterSamples);
    EXPECT_EQ(safe.sdsmQuantizeSteps, defaults.sdsmQuantizeSteps);
}

// ---------------------------------------------------------------------------------------------
// ExtractViewFrustum
// ---------------------------------------------------------------------------------------------

TEST(ShadowMathTest, ExtractViewFrustumRecoversEyeForwardAndDepthRange)
{
    const Vec3 eye(0.0f, 2.0f, -10.0f);
    const Vec3 target = Vec3::Zero();
    const ShadowViewFrustum frustum = MakeCameraFrustum(eye, target);

    ASSERT_TRUE(frustum.valid);
    EXPECT_NEAR(frustum.viewNear, kCameraNear, 1e-2f);
    EXPECT_NEAR(frustum.viewFar, kCameraFar, 1.0f);

    const Vec3 expectedForward = Normalize(target - eye);
    EXPECT_NEAR(frustum.forward.x, expectedForward.x, 1e-3f);
    EXPECT_NEAR(frustum.forward.y, expectedForward.y, 1e-3f);
    EXPECT_NEAR(frustum.forward.z, expectedForward.z, 1e-3f);
    EXPECT_EQ(frustum.eye.x, eye.x);
    EXPECT_EQ(frustum.eye.y, eye.y);
    EXPECT_EQ(frustum.eye.z, eye.z);
}

TEST(ShadowMathTest, ExtractViewFrustumRaysReachDepthOneAlongTheForwardAxis)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(1.0f, 3.0f, -6.0f), Vec3(0.0f, 1.0f, 4.0f));
    ASSERT_TRUE(frustum.valid);

    // "A point at view depth z of corner i is eye + rays[i] * z": the ray's depth along forward is 1.
    for (const Vec3& ray : frustum.rays) {
        EXPECT_NEAR(Dot(ray, frustum.forward), 1.0f, 1e-3f);
    }
}

TEST(ShadowMathTest, ExtractViewFrustumRejectsADegenerateMatrix)
{
    const ShadowViewFrustum frustum = ExtractViewFrustum(Vec3::Zero(), Mat4::Identity());
    EXPECT_FALSE(frustum.valid);
}

// ---------------------------------------------------------------------------------------------
// BuildShadowCascades
// ---------------------------------------------------------------------------------------------

TEST(ShadowMathTest, BuildShadowCascadesReturnsInvalidSetForAnInvalidFrustum)
{
    const ShadowSettings safe = SanitizeShadowSettings(ShadowSettings{});
    const ShadowCascadeSet set = BuildShadowCascades(ShadowViewFrustum{}, Vec3::Up(), safe, 2048);
    EXPECT_FALSE(set.valid);
    EXPECT_EQ(set.count, 0u);
}

TEST(ShadowMathTest, BuildShadowCascadesReturnsInvalidSetForZeroResolution)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);
    const ShadowSettings safe = SanitizeShadowSettings(ShadowSettings{});
    EXPECT_FALSE(BuildShadowCascades(frustum, Vec3::Up(), safe, 0).valid);
}

TEST(ShadowMathTest, BuildShadowCascadesSplitsAscendAndCoverTheShadowDistance)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);

    ShadowSettings settings;
    settings.cascadeCount = 4;
    settings.maxDistance = 60.0f;
    const ShadowSettings safe = SanitizeShadowSettings(settings);

    const ShadowCascadeSet set = BuildShadowCascades(frustum, Normalize(Vec3(0.3f, 0.8f, -0.2f)), safe, 2048);
    ASSERT_TRUE(set.valid);
    ASSERT_EQ(set.count, 4u);

    EXPECT_FLOAT_EQ(set.shadowDistance, 60.0f); // min(camera far, maxDistance)
    EXPECT_FLOAT_EQ(set.fadeStart, 60.0f * (1.0f - safe.distanceFadeFraction));
    EXPECT_FLOAT_EQ(set.cascades[0].sliceStart, frustum.viewNear);
    EXPECT_FLOAT_EQ(set.cascades[set.count - 1].sliceEnd, 60.0f);
    EXPECT_FALSE(set.sdsmActive);

    for (std::uint32_t c = 0; c < set.count; ++c) {
        EXPECT_LT(set.cascades[c].sliceStart, set.cascades[c].sliceEnd) << "cascade " << c;
        EXPECT_GT(set.cascades[c].radius, 0.0f) << "cascade " << c;
        EXPECT_GT(set.cascades[c].texelWorldSize, 0.0f) << "cascade " << c;
        EXPECT_GT(set.cascades[c].depthRange, 0.0f) << "cascade " << c;
        if (c > 0) {
            EXPECT_GT(set.cascades[c].sliceEnd, set.cascades[c - 1].sliceEnd) << "cascade " << c;
            // Neighbors overlap by the blend band, they never leave a gap.
            EXPECT_LE(set.cascades[c].sliceStart, set.cascades[c - 1].sliceEnd) << "cascade " << c;
        }
    }
}

TEST(ShadowMathTest, BuildShadowCascadesClampsShadowDistanceToTheCameraFarPlane)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);

    ShadowSettings settings;
    settings.maxDistance = 100000.0f;
    const ShadowSettings safe = SanitizeShadowSettings(settings);

    const ShadowCascadeSet set = BuildShadowCascades(frustum, Vec3::Up(), safe, 1024);
    ASSERT_TRUE(set.valid);
    EXPECT_FLOAT_EQ(set.shadowDistance, frustum.viewFar);
}

TEST(ShadowMathTest, BuildShadowCascadesHonorsTheRequestedCascadeCount)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);

    for (std::uint32_t count = 1; count <= kMaxShadowCascades; ++count) {
        ShadowSettings settings;
        settings.cascadeCount = count;
        const ShadowCascadeSet set =
            BuildShadowCascades(frustum, Vec3::Up(), SanitizeShadowSettings(settings), 1024);
        ASSERT_TRUE(set.valid) << "count " << count;
        EXPECT_EQ(set.count, count);
    }
}

TEST(ShadowMathTest, BuildShadowCascadesSunStraightOverheadNeverNaNs)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);
    const ShadowSettings safe = SanitizeShadowSettings(ShadowSettings{});

    for (const Vec3& toward : { Vec3::Up(), Vec3::Down() }) {
        const ShadowCascadeSet set = BuildShadowCascades(frustum, toward, safe, 2048);
        ASSERT_TRUE(set.valid);
        for (std::uint32_t c = 0; c < set.count; ++c) {
            EXPECT_TRUE(AllFinite(set.cascades[c].viewProjection)) << "cascade " << c;
        }
    }
}

TEST(ShadowMathTest, BuildShadowCascadesTreatsANonFiniteSunDirectionAsStraightUp)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);
    const ShadowSettings safe = SanitizeShadowSettings(ShadowSettings{});

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const ShadowCascadeSet set = BuildShadowCascades(frustum, Vec3(nan, 0.0f, 0.0f), safe, 2048);
    ASSERT_TRUE(set.valid);
    EXPECT_NEAR(set.directionTowardSun.y, 1.0f, 1e-6f);
    EXPECT_TRUE(AllFinite(set.cascades[0].viewProjection));
}

TEST(ShadowMathTest, BuildShadowCascadesEveryCascadeContainsTheViewAxisPointsOfItsSlice)
{
    const Vec3 eye(0.0f, 2.0f, -10.0f);
    const ShadowViewFrustum frustum = MakeCameraFrustum(eye, Vec3::Zero());
    ASSERT_TRUE(frustum.valid);
    const ShadowSettings safe = SanitizeShadowSettings(ShadowSettings{});

    const ShadowCascadeSet set = BuildShadowCascades(frustum, Normalize(Vec3(0.4f, 0.7f, 0.3f)), safe, 2048);
    ASSERT_TRUE(set.valid);

    for (std::uint32_t c = 0; c < set.count; ++c) {
        const float depth = 0.5f * (set.cascades[c].sliceStart + set.cascades[c].sliceEnd);
        const Vec3 point = eye + frustum.forward * depth;
        const Vec4 clip = set.cascades[c].viewProjection * Vec4(point, 1.0f);
        ASSERT_NE(clip.w, 0.0f);
        const Vec3 ndc = clip.XYZ() / clip.w;
        EXPECT_GE(ndc.x, -1.0f) << "cascade " << c;
        EXPECT_LE(ndc.x, 1.0f) << "cascade " << c;
        EXPECT_GE(ndc.y, -1.0f) << "cascade " << c;
        EXPECT_LE(ndc.y, 1.0f) << "cascade " << c;
        EXPECT_GE(ndc.z, 0.0f) << "cascade " << c;
        EXPECT_LE(ndc.z, 1.0f) << "cascade " << c;
    }
}

TEST(ShadowMathTest, BuildShadowCascadesStabilizationOnlySetsGridIndicesWhenEnabled)
{
    const Vec3 eye(123.4f, 5.0f, -77.7f);
    const ShadowViewFrustum frustum = MakeCameraFrustum(eye, Vec3(130.0f, 3.0f, -60.0f));
    ASSERT_TRUE(frustum.valid);

    ShadowSettings settings;
    settings.stabilizeCascades = false;
    const ShadowCascadeSet unstable =
        BuildShadowCascades(frustum, Normalize(Vec3(0.3f, 0.8f, 0.2f)), SanitizeShadowSettings(settings), 2048);
    ASSERT_TRUE(unstable.valid);
    EXPECT_EQ(unstable.cascades[0].gridX, 0);
    EXPECT_EQ(unstable.cascades[0].gridY, 0);

    settings.stabilizeCascades = true;
    const ShadowCascadeSet stable =
        BuildShadowCascades(frustum, Normalize(Vec3(0.3f, 0.8f, 0.2f)), SanitizeShadowSettings(settings), 2048);
    ASSERT_TRUE(stable.valid);
    EXPECT_TRUE(stable.cascades[0].gridX != 0 || stable.cascades[0].gridY != 0);
}

TEST(ShadowMathTest, BuildShadowCascadesFitsTheSplitsToAMeasuredDepthRange)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);
    const ShadowSettings safe = SanitizeShadowSettings(ShadowSettings{});

    const ShadowCascadeSet set =
        BuildShadowCascades(frustum, Vec3::Up(), safe, 2048, ShadowDepthRange{ 5.0f, 40.0f });
    ASSERT_TRUE(set.valid);
    EXPECT_TRUE(set.sdsmActive);
    EXPECT_FLOAT_EQ(set.fittedRange.nearDepth, 5.0f);
    EXPECT_FLOAT_EQ(set.fittedRange.farDepth, 40.0f);
    EXPECT_FLOAT_EQ(set.cascades[0].sliceStart, 5.0f);
    EXPECT_FLOAT_EQ(set.cascades[set.count - 1].sliceEnd, 40.0f);
}

TEST(ShadowMathTest, BuildShadowCascadesClampsAMeasuredRangeIntoTheShadowDistance)
{
    const ShadowViewFrustum frustum = MakeCameraFrustum(Vec3(0.0f, 2.0f, -10.0f), Vec3::Zero());
    ASSERT_TRUE(frustum.valid);

    ShadowSettings settings;
    settings.maxDistance = 50.0f;
    const ShadowSettings safe = SanitizeShadowSettings(settings);

    const ShadowCascadeSet set =
        BuildShadowCascades(frustum, Vec3::Up(), safe, 2048, ShadowDepthRange{ 0.0f, 9000.0f });
    ASSERT_TRUE(set.valid);
    EXPECT_GE(set.fittedRange.nearDepth, frustum.viewNear);
    EXPECT_LE(set.fittedRange.farDepth, 50.0f);
}

// ---------------------------------------------------------------------------------------------
// SunElevationDegrees
// ---------------------------------------------------------------------------------------------

TEST(ShadowMathTest, SunElevationMatchesKnownDirections)
{
    EXPECT_NEAR(SunElevationDegrees(Vec3::Up()), 90.0f, 1e-3f);
    EXPECT_NEAR(SunElevationDegrees(Vec3::Down()), -90.0f, 1e-3f);
    EXPECT_NEAR(SunElevationDegrees(Vec3(1.0f, 0.0f, 0.0f)), 0.0f, 1e-3f);
    EXPECT_NEAR(SunElevationDegrees(Vec3(0.0f, 1.0f, 1.0f)), 45.0f, 1e-3f);
}

TEST(ShadowMathTest, SunElevationOfANonFiniteOrZeroDirectionCountsAsStraightUp)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_NEAR(SunElevationDegrees(Vec3(nan, nan, nan)), 90.0f, 1e-3f);
    EXPECT_NEAR(SunElevationDegrees(Vec3::Zero()), 90.0f, 1e-3f);
}

// ---------------------------------------------------------------------------------------------
// DecodeDepthRange
// ---------------------------------------------------------------------------------------------

TEST(ShadowMathTest, DecodeDepthRangeOfAnUntouchedResultIsNothing)
{
    EXPECT_FALSE(DecodeDepthRange(ShadowDepthRangeGpu{}).has_value());
}

TEST(ShadowMathTest, DecodeDepthRangeReadsMinAndInvertedMaxBits)
{
    ShadowDepthRangeGpu gpu;
    gpu.minDepthBits = std::bit_cast<std::uint32_t>(2.0f);
    gpu.invMaxDepthBits = ~std::bit_cast<std::uint32_t>(10.0f);

    const std::optional<ShadowDepthRange> range = DecodeDepthRange(gpu);
    ASSERT_TRUE(range.has_value());
    EXPECT_FLOAT_EQ(range->nearDepth, 2.0f);
    EXPECT_FLOAT_EQ(range->farDepth, 10.0f);
}

TEST(ShadowMathTest, DecodeDepthRangeRejectsFarSmallerThanNear)
{
    ShadowDepthRangeGpu gpu;
    gpu.minDepthBits = std::bit_cast<std::uint32_t>(10.0f);
    gpu.invMaxDepthBits = ~std::bit_cast<std::uint32_t>(2.0f);
    EXPECT_FALSE(DecodeDepthRange(gpu).has_value());
}

TEST(ShadowMathTest, DecodeDepthRangeRejectsANonPositiveNear)
{
    ShadowDepthRangeGpu gpu;
    gpu.minDepthBits = std::bit_cast<std::uint32_t>(0.0f);
    gpu.invMaxDepthBits = ~std::bit_cast<std::uint32_t>(10.0f);
    EXPECT_FALSE(DecodeDepthRange(gpu).has_value());
}

// ---------------------------------------------------------------------------------------------
// ExponentialBlendFactor
// ---------------------------------------------------------------------------------------------

TEST(ShadowMathTest, ExponentialBlendFactorMatchesOneMinusExpMinusRatio)
{
    EXPECT_NEAR(ExponentialBlendFactor(0.3f, 0.3f), 1.0f - std::exp(-1.0f), 1e-6f);
    EXPECT_NEAR(ExponentialBlendFactor(0.1f, 0.5f), 1.0f - std::exp(-0.2f), 1e-6f);
}

TEST(ShadowMathTest, ExponentialBlendFactorEdgeCases)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();

    EXPECT_FLOAT_EQ(ExponentialBlendFactor(0.016f, 0.0f), 1.0f);  // tau <= 0: jump at once
    EXPECT_FLOAT_EQ(ExponentialBlendFactor(0.016f, -1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ExponentialBlendFactor(0.0f, 0.3f), 0.0f);    // no time passed
    EXPECT_FLOAT_EQ(ExponentialBlendFactor(-1.0f, 0.3f), 0.0f);
    EXPECT_FLOAT_EQ(ExponentialBlendFactor(nan, 0.3f), 0.0f);
}

// ---------------------------------------------------------------------------------------------
// UpdateSdsmState
// ---------------------------------------------------------------------------------------------

ShadowSettings SdsmSettingsWithoutQuantizing()
{
    ShadowSettings settings;
    settings.sdsmPaddingFraction = 0.1f;
    settings.sdsmShrinkSeconds = 0.5f;
    settings.sdsmQuantizeSteps = 0;
    settings.maxDistance = 150.0f;
    return SanitizeShadowSettings(settings);
}

TEST(ShadowMathTest, UpdateSdsmStateWithoutASampleKeepsTheInvalidState)
{
    const ShadowSettings safe = SdsmSettingsWithoutQuantizing();
    const ShadowSdsmState next =
        UpdateSdsmState(ShadowSdsmState{}, std::nullopt, MakeDepthOnlyFrustum(0.5f, 500.0f), safe, 10, 0.016f);
    EXPECT_FALSE(next.valid);
}

TEST(ShadowMathTest, UpdateSdsmStateFirstSamplePadsTheMeasuredRange)
{
    const ShadowSettings safe = SdsmSettingsWithoutQuantizing();
    const ShadowViewFrustum frustum = MakeDepthOnlyFrustum(0.5f, 500.0f);
    const ShadowDepthSample sample{ ShadowDepthRange{ 10.0f, 50.0f }, 7 };

    const ShadowSdsmState next = UpdateSdsmState(ShadowSdsmState{}, sample, frustum, safe, 7, 0.016f);
    ASSERT_TRUE(next.valid);
    EXPECT_NEAR(next.smoothNear, 10.0f * 0.9f, 1e-4f);
    EXPECT_NEAR(next.smoothFar, 50.0f * 1.1f, 1e-4f);
    EXPECT_FLOAT_EQ(next.used.nearDepth, next.smoothNear); // quantizing off
    EXPECT_FLOAT_EQ(next.used.farDepth, next.smoothFar);
    EXPECT_FLOAT_EQ(next.raw.nearDepth, 10.0f);
    EXPECT_FLOAT_EQ(next.raw.farDepth, 50.0f);
    EXPECT_EQ(next.lastSampleFrame, 7u);
}

TEST(ShadowMathTest, UpdateSdsmStateGrowsInstantlyAndShrinksSlowly)
{
    const ShadowSettings safe = SdsmSettingsWithoutQuantizing();
    const ShadowViewFrustum frustum = MakeDepthOnlyFrustum(0.5f, 500.0f);

    const ShadowSdsmState first = UpdateSdsmState(
        ShadowSdsmState{}, ShadowDepthSample{ ShadowDepthRange{ 10.0f, 50.0f }, 1 }, frustum, safe, 1, 0.016f);
    ASSERT_TRUE(first.valid);

    // A wider range is taken over right away.
    const ShadowSdsmState grown = UpdateSdsmState(
        first, ShadowDepthSample{ ShadowDepthRange{ 5.0f, 100.0f }, 2 }, frustum, safe, 2, 0.016f);
    ASSERT_TRUE(grown.valid);
    EXPECT_NEAR(grown.smoothNear, 5.0f * 0.9f, 1e-4f);
    EXPECT_NEAR(grown.smoothFar, 100.0f * 1.1f, 1e-3f);

    // A narrower range moves only part of the way, by the exponential blend factor.
    const float dt = 0.1f;
    const float r = ExponentialBlendFactor(dt, safe.sdsmShrinkSeconds);
    const ShadowSdsmState shrunk = UpdateSdsmState(
        grown, ShadowDepthSample{ ShadowDepthRange{ 10.0f, 50.0f }, 3 }, frustum, safe, 3, dt);
    ASSERT_TRUE(shrunk.valid);
    const float targetFar = 50.0f * 1.1f;
    EXPECT_NEAR(shrunk.smoothFar, grown.smoothFar + (targetFar - grown.smoothFar) * r, 1e-3f);
    EXPECT_GT(shrunk.smoothFar, targetFar);
    EXPECT_LT(shrunk.smoothFar, grown.smoothFar);
}

TEST(ShadowMathTest, UpdateSdsmStateQuantizedRangeAlwaysCoversTheSmoothedRange)
{
    ShadowSettings settings;
    settings.sdsmPaddingFraction = 0.1f;
    settings.sdsmQuantizeSteps = 4;
    const ShadowSettings safe = SanitizeShadowSettings(settings);
    const ShadowViewFrustum frustum = MakeDepthOnlyFrustum(0.5f, 500.0f);

    const ShadowSdsmState next = UpdateSdsmState(
        ShadowSdsmState{}, ShadowDepthSample{ ShadowDepthRange{ 7.3f, 41.9f }, 1 }, frustum, safe, 1, 0.016f);
    ASSERT_TRUE(next.valid);
    EXPECT_LE(next.used.nearDepth, next.smoothNear + 1e-4f); // near rounds down
    EXPECT_GE(next.used.farDepth, next.smoothFar - 1e-4f);   // far rounds up
}

TEST(ShadowMathTest, UpdateSdsmStateRangeBeyondTheShadowDistanceInvalidatesTheState)
{
    const ShadowSettings safe = SdsmSettingsWithoutQuantizing(); // maxDistance 150
    const ShadowViewFrustum frustum = MakeDepthOnlyFrustum(0.5f, 500.0f);

    const ShadowSdsmState first = UpdateSdsmState(
        ShadowSdsmState{}, ShadowDepthSample{ ShadowDepthRange{ 10.0f, 50.0f }, 1 }, frustum, safe, 1, 0.016f);
    ASSERT_TRUE(first.valid);

    const ShadowSdsmState outside = UpdateSdsmState(
        first, ShadowDepthSample{ ShadowDepthRange{ 400.0f, 450.0f }, 2 }, frustum, safe, 2, 0.016f);
    EXPECT_FALSE(outside.valid);
}

TEST(ShadowMathTest, UpdateSdsmStateAStaleStateIsDroppedWhenNoNewSampleArrives)
{
    const ShadowSettings safe = SdsmSettingsWithoutQuantizing();
    const ShadowViewFrustum frustum = MakeDepthOnlyFrustum(0.5f, 500.0f);

    const ShadowSdsmState first = UpdateSdsmState(
        ShadowSdsmState{}, ShadowDepthSample{ ShadowDepthRange{ 10.0f, 50.0f }, 1 }, frustum, safe, 1, 0.016f);
    ASSERT_TRUE(first.valid);

    // Held while the sample is young enough ...
    EXPECT_TRUE(UpdateSdsmState(first, std::nullopt, frustum, safe, 1 + kSdsmMaxHoldFrames, 0.016f).valid);
    // ... dropped once it is older than kSdsmMaxHoldFrames.
    EXPECT_FALSE(UpdateSdsmState(first, std::nullopt, frustum, safe, 2 + kSdsmMaxHoldFrames, 0.016f).valid);
    // A restarted frame counter also drops it.
    EXPECT_FALSE(UpdateSdsmState(first, std::nullopt, frustum, safe, 0, 0.016f).valid);
}

} // namespace
} // namespace gte
