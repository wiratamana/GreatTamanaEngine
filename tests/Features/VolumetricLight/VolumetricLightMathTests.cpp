// CPU checks of the VolumetricLight math: froxel mapping, rays, phase, integration, cascades, history, settings.

#include "Features/VolumetricLight/VolumetricLightMath.h"
#include "Features/VolumetricLight/VolumetricLightTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kGridDepth = static_cast<float>(kVolumetricLightGridDepth);

struct TestCamera {
    Mat4 viewProjection;
    Mat4 inverse;
    Vec3 eye;
};

TestCamera MakeCamera(bool flipY)
{
    TestCamera c;
    c.eye = Vec3{ 3.0f, 1.7f, -4.0f };
    const Mat4 view = Mat4::LookAtLH(c.eye, Vec3{ 1.0f, 1.0f, 2.0f }, Vec3::Up());
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 200.0f, flipY);
    c.viewProjection = proj * view;
    EXPECT_TRUE(c.viewProjection.TryInverse(c.inverse));
    return c;
}

// Small deterministic generator in [0,1).
struct Lcg {
    std::uint32_t state = 12345u;
    float Next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    }
};

TEST(VolumetricLightMathTest, SliceMappingEndsAreExactAndRoundTrips)
{
    for (const float exponent : { 1.0f, 2.0f, 3.5f }) {
        EXPECT_FLOAT_EQ(VolumetricLightSliceToDistance(0.0f, kGridDepth, 60.0f, exponent), 0.0f);
        EXPECT_NEAR(VolumetricLightSliceToDistance(kGridDepth, kGridDepth, 60.0f, exponent), 60.0f, 1e-4f);
        float previous = -1.0f;
        for (float slice = 0.0f; slice <= kGridDepth; slice += 0.5f) {
            const float distance = VolumetricLightSliceToDistance(slice, kGridDepth, 60.0f, exponent);
            EXPECT_GT(distance, previous);
            previous = distance;
            if (slice > 0.0f) {
                const float back = VolumetricLightDistanceToSlice(distance, kGridDepth, 60.0f, exponent);
                EXPECT_NEAR(back, slice, slice * 1e-4f) << exponent;
            }
        }
    }
}

TEST(VolumetricLightMathTest, ExponentTwoMatchesTheShaderFormula)
{
    for (std::uint32_t k = 0; k <= kVolumetricLightGridDepth; ++k) {
        const float u = static_cast<float>(k) / kGridDepth;
        EXPECT_NEAR(VolumetricLightSliceToDistance(static_cast<float>(k), kGridDepth, 60.0f, 2.0f), 60.0f * u * u, 1e-4f);
    }
    EXPECT_FLOAT_EQ(VolumetricLightSliceToDistance(8.0f, kGridDepth, 60.0f, 1.0f), 60.0f * 8.0f / kGridDepth);
}

TEST(VolumetricLightMathTest, MappingClampsAndNeverProducesNaN)
{
    EXPECT_FLOAT_EQ(VolumetricLightDistanceToSlice(500.0f, kGridDepth, 60.0f, 2.0f), kGridDepth);
    EXPECT_FLOAT_EQ(VolumetricLightSliceToDistance(0.0f, 0.0f, 60.0f, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(VolumetricLightSliceToDistance(5.0f, kGridDepth, 0.0f, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(VolumetricLightDistanceToSlice(5.0f, kGridDepth, -1.0f, 2.0f), 0.0f);
    EXPECT_TRUE(std::isfinite(VolumetricLightSliceToDistance(kNaN, kGridDepth, 60.0f, 2.0f)));
    EXPECT_TRUE(std::isfinite(VolumetricLightDistanceToSlice(kNaN, kGridDepth, 60.0f, kNaN)));
}

TEST(VolumetricLightMathTest, CenterRayIsTheForwardAndRaysAreUnit)
{
    for (const bool flipY : { false, true }) {
        const TestCamera cam = MakeCamera(flipY);
        const Vec3 forward = DeriveVolumetricLightForward(cam.inverse);
        EXPECT_NEAR(Length(forward), 1.0f, 1e-5f);
        EXPECT_NEAR(Dot(forward, Normalize(Vec3{ 1.0f, 1.0f, 2.0f } - cam.eye)), 1.0f, 1e-4f);
        for (const auto& [u, v] : { std::pair{ 0.0f, 0.0f }, std::pair{ 1.0f, 0.0f }, std::pair{ 1.0f, 1.0f }, std::pair{ 0.0f, 1.0f } }) {
            EXPECT_NEAR(Length(VolumetricLightRayDirection(u, v, cam.inverse)), 1.0f, 1e-5f);
        }
    }
}

TEST(VolumetricLightMathTest, CellToWorldAndBackReturnsTheSameCoordinate)
{
    for (const bool flipY : { false, true }) {
        const TestCamera cam = MakeCamera(flipY);
        Lcg rng;
        for (int i = 0; i < 200; ++i) {
            const VolumetricLightCell cell{ static_cast<std::uint32_t>(rng.Next() * kVolumetricLightGridWidth),
                static_cast<std::uint32_t>(rng.Next() * kVolumetricLightGridHeight),
                1u + static_cast<std::uint32_t>(rng.Next() * (kVolumetricLightGridDepth - 1)) };
            const Vec3 jitter{ 0.1f + 0.8f * rng.Next(), 0.1f + 0.8f * rng.Next(), 0.1f + 0.8f * rng.Next() };

            const Vec3 world = VolumetricLightCellToWorld(cell, jitter, cam.eye, cam.inverse, 60.0f, 2.0f);
            const VolumetricLightCoord coord = VolumetricLightWorldToCoord(world, cam.eye, cam.viewProjection, 60.0f, 2.0f);
            ASSERT_TRUE(coord.valid) << i;
            EXPECT_NEAR(coord.u, (static_cast<float>(cell.x) + jitter.x) / kVolumetricLightGridWidth, 1e-3f) << i;
            EXPECT_NEAR(coord.v, (static_cast<float>(cell.y) + jitter.y) / kVolumetricLightGridHeight, 1e-3f) << i;
            EXPECT_NEAR(coord.slice, static_cast<float>(cell.z) + jitter.z, 1e-3f) << i;
        }
    }
}

TEST(VolumetricLightMathTest, PointsBehindOffScreenOrBeyondRangeAreInvalid)
{
    const TestCamera cam = MakeCamera(false);
    const Vec3 forward = DeriveVolumetricLightForward(cam.inverse);
    EXPECT_TRUE(VolumetricLightWorldToCoord(cam.eye + forward * 10.0f, cam.eye, cam.viewProjection, 60.0f, 2.0f).valid);
    EXPECT_FALSE(VolumetricLightWorldToCoord(cam.eye - forward * 10.0f, cam.eye, cam.viewProjection, 60.0f, 2.0f).valid);
    EXPECT_FALSE(VolumetricLightWorldToCoord(cam.eye + Vec3{ 0.0f, 500.0f, 0.0f }, cam.eye, cam.viewProjection, 600.0f, 2.0f).valid);
    EXPECT_FALSE(VolumetricLightWorldToCoord(cam.eye + forward * 90.0f, cam.eye, cam.viewProjection, 60.0f, 2.0f).valid);
}

TEST(VolumetricLightMathTest, AtlasCoversEveryTexelExactlyOnce)
{
    const Extent2D extent = VolumetricLightAtlasExtent();
    EXPECT_EQ(extent.width, 1280u);
    EXPECT_EQ(extent.height, 720u);

    const VolumetricLightAtlasTexel first = VolumetricLightAtlasTexelOf({ 0, 0, 0 });
    EXPECT_EQ(first.x, 0u);
    EXPECT_EQ(first.y, 0u);
    const VolumetricLightAtlasTexel last = VolumetricLightAtlasTexelOf({ 159, 89, 63 });
    EXPECT_EQ(last.x, 1279u);
    EXPECT_EQ(last.y, 719u);

    std::set<std::uint64_t> seen;
    for (std::uint32_t z = 0; z < kVolumetricLightGridDepth; ++z) {
        for (std::uint32_t y = 0; y < kVolumetricLightGridHeight; ++y) {
            for (std::uint32_t x = 0; x < kVolumetricLightGridWidth; ++x) {
                const VolumetricLightAtlasTexel t = VolumetricLightAtlasTexelOf({ x, y, z });
                ASSERT_LT(t.x, extent.width);
                ASSERT_LT(t.y, extent.height);
                seen.insert(std::uint64_t{ t.y } * extent.width + t.x);
            }
        }
    }
    EXPECT_EQ(seen.size(), std::size_t{ kVolumetricLightGridWidth } * kVolumetricLightGridHeight * kVolumetricLightGridDepth);
}

TEST(VolumetricLightMathTest, PhaseIntegratesToOneOverTheSphere)
{
    constexpr int kSteps = 20000;
    for (const float g : { -0.5f, 0.0f, 0.7f, 0.9f }) {
        double sum = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            const float c = -1.0f + 2.0f * (static_cast<float>(i) + 0.5f) / kSteps;
            sum += 2.0 * 3.14159265358979 * VolumetricLightPhase(c, g) * (2.0 / kSteps);
        }
        EXPECT_NEAR(sum, 1.0, 1e-3) << g;
    }
}

TEST(VolumetricLightMathTest, PhaseIsIsotropicAtZeroAndForwardForPositiveG)
{
    EXPECT_NEAR(VolumetricLightPhase(0.3f, 0.0f), 1.0f / (4.0f * 3.14159265f), 1e-6f);
    EXPECT_GT(VolumetricLightPhase(1.0f, 0.7f), VolumetricLightPhase(-1.0f, 0.7f));
    EXPECT_FLOAT_EQ(VolumetricLightPhase(0.4f, 0.6f), VolumetricLightPhase(-0.4f, -0.6f));
    EXPECT_TRUE(std::isfinite(VolumetricLightPhase(kNaN, 5.0f)));
}

TEST(VolumetricLightMathTest, SliceIntegralMatchesNumericIntegration)
{
    const Vec3 s{ 0.4f, 0.2f, 0.1f };
    for (const float sigma : { 0.0f, 0.05f, 0.5f, 3.0f }) {
        constexpr float kThickness = 2.0f;
        constexpr int kSteps = 10000;
        double inScatter = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            const double t = (i + 0.5) * kThickness / kSteps;
            inScatter += s.x * std::exp(-sigma * t) * kThickness / kSteps;
        }
        const VolumetricLightSliceIntegral r = VolumetricLightIntegrateSlice(s, sigma, kThickness);
        EXPECT_NEAR(r.inScatter.x, inScatter, 1e-4) << sigma;
        EXPECT_NEAR(r.transmittance, std::exp(-sigma * kThickness), 1e-6f) << sigma;
    }
    const VolumetricLightSliceIntegral thick = VolumetricLightIntegrateSlice(s, 5.0f, 100.0f);
    EXPECT_NEAR(thick.inScatter.x, s.x / 5.0f, 1e-5f);
    EXPECT_NEAR(thick.transmittance, 0.0f, 1e-6f);
}

TEST(VolumetricLightMathTest, SlicingDoesNotChangeTheIntegral)
{
    const Vec3 s{ 0.3f, 0.3f, 0.3f };
    constexpr float kSigma = 0.04f;
    constexpr float kMax = 60.0f;

    Vec3 inScatter = Vec3::Zero();
    float transmittance = 1.0f;
    float previousDistance = 0.0f;
    for (std::uint32_t k = 1; k <= kVolumetricLightGridDepth; ++k) {
        const float distance = VolumetricLightSliceToDistance(static_cast<float>(k), kGridDepth, kMax, 2.0f);
        const VolumetricLightSliceIntegral slice = VolumetricLightIntegrateSlice(s, kSigma, distance - previousDistance);
        inScatter += slice.inScatter * transmittance;
        transmittance *= slice.transmittance;
        previousDistance = distance;
    }
    const VolumetricLightSliceIntegral whole = VolumetricLightIntegrateSlice(s, kSigma, kMax);
    EXPECT_NEAR(inScatter.x, whole.inScatter.x, 1e-3f);
    EXPECT_NEAR(transmittance, whole.transmittance, 1e-4f);
}

TEST(VolumetricLightMathTest, ExtinctionAddsTheHeightTermAndFadesIn)
{
    VolumetricLightSettings s;
    s.density = 0.02f;
    s.heightDensity = 0.5f;
    s.heightBase = 1.0f;
    s.heightFalloff = 0.5f;
    EXPECT_NEAR(VolumetricLightExtinction(s, 0.0f, 10.0f), 0.02f + 0.5f, 1e-6f);
    EXPECT_NEAR(VolumetricLightExtinction(s, 3.0f, 10.0f), 0.02f + 0.5f * std::exp(-1.0f), 1e-6f);

    s.startDistance = 10.0f;
    EXPECT_FLOAT_EQ(VolumetricLightExtinction(s, 0.0f, 0.0f), 0.0f);
    EXPECT_GT(VolumetricLightExtinction(s, 0.0f, 5.0f), 0.0f);
    EXPECT_FLOAT_EQ(VolumetricLightExtinction(s, 0.0f, 20.0f), 0.02f + 0.5f);
}

TEST(VolumetricLightMathTest, SunFadeFollowsTheElevation)
{
    EXPECT_FLOAT_EQ(VolumetricLightSunFade(Vec3::Up()), 1.0f);
    EXPECT_FLOAT_EQ(VolumetricLightSunFade(Vec3{ kNaN, 0.0f, 0.0f }), 1.0f);

    const auto fadeAt = [](float degrees) {
        const float r = degrees * 3.14159265f / 180.0f;
        return VolumetricLightSunFade(Vec3{ std::cos(r), std::sin(r), 0.0f });
    };
    EXPECT_GT(fadeAt(0.0f), 0.0f);
    EXPECT_LT(fadeAt(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(fadeAt(-3.0f), 0.0f);
    EXPECT_FLOAT_EQ(fadeAt(6.0f), 1.0f);
    float previous = 0.0f;
    for (float degrees = -4.0f; degrees <= 8.0f; degrees += 0.5f) {
        EXPECT_GE(fadeAt(degrees), previous);
        previous = fadeAt(degrees);
    }
}

SunShadowReceiver MakeReceiver(std::uint32_t count)
{
    SunShadowReceiver r;
    r.cascadeCount = count;
    r.resolution = 1024;
    for (std::uint32_t c = 0; c < std::min(count, kSunShadowReceiverMaxCascades); ++c) {
        r.maps[c] = fx::Texture{ rg::TextureHandle{ c, 1 }, 3 };
        r.viewProjection[c] = Mat4::Identity();
        r.splitFar[c] = 10.0f * static_cast<float>(c + 1);
        r.blendWidth[c] = c + 1 < count ? 2.0f : 0.0f;
        r.texelWorld[c] = 0.05f;
        r.depthRange[c] = 100.0f;
    }
    r.cameraPosition = Vec3{ 1.0f, 2.0f, 3.0f };
    r.cameraForward = Vec3::Forward();
    return r;
}

TEST(VolumetricLightMathTest, ReceiverValidityRules)
{
    EXPECT_TRUE(VolumetricLightReceiverUsable(MakeReceiver(1)));
    EXPECT_TRUE(VolumetricLightReceiverUsable(MakeReceiver(4)));
    EXPECT_FALSE(VolumetricLightReceiverUsable(MakeReceiver(0)));
    EXPECT_FALSE(VolumetricLightReceiverUsable(MakeReceiver(5)));

    SunShadowReceiver r = MakeReceiver(3);
    r.resolution = 0;
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));

    r = MakeReceiver(3);
    r.maps[1] = fx::Texture{};
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));

    r = MakeReceiver(3);
    r.viewProjection[2](1, 1) = kNaN;
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));

    r = MakeReceiver(3);
    r.splitFar[1] = 0.0f;
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));

    r = MakeReceiver(3);
    r.splitFar[2] = 5.0f; // Decreasing.
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));

    r = MakeReceiver(3);
    r.texelWorld[0] = 0.0f;
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));

    r = MakeReceiver(3);
    r.cameraForward = Vec3{ 0.0f, 0.0f, 2.0f };
    EXPECT_FALSE(VolumetricLightReceiverUsable(r));
}

TEST(VolumetricLightMathTest, CascadePickSelectsBySplitAndBlendsNearTheEnd)
{
    SunShadowReceiver r = MakeReceiver(4);
    r.splitFar = { 10.0f, 30.0f, 70.0f, 150.0f };
    r.blendWidth = { 3.0f, 4.0f, 7.0f, 0.0f };

    const VolumetricLightCascadePick near = VolumetricLightPickCascade(5.0f, r);
    EXPECT_EQ(near.index, 0u);
    EXPECT_FLOAT_EQ(near.blendToNext, 0.0f);

    const VolumetricLightCascadePick seam = VolumetricLightPickCascade(28.0f, r);
    EXPECT_EQ(seam.index, 1u);
    EXPECT_NEAR(seam.blendToNext, 0.5f, 1e-6f);

    const VolumetricLightCascadePick beyond = VolumetricLightPickCascade(400.0f, r);
    EXPECT_EQ(beyond.index, 3u);
    EXPECT_FLOAT_EQ(beyond.blendToNext, 0.0f);
}

TEST(VolumetricLightMathTest, ProjectToCascadeRejectsOutsideTheMap)
{
    float u = 0.0f;
    float v = 0.0f;
    float z = 0.0f;
    const Mat4 identity = Mat4::Identity();
    EXPECT_TRUE(VolumetricLightProjectToCascade(identity, Vec3{ 0.0f, 0.0f, 0.5f }, u, v, z));
    EXPECT_NEAR(u, 0.5f, 1e-6f);
    EXPECT_NEAR(v, 0.5f, 1e-6f);
    EXPECT_NEAR(z, 0.5f, 1e-6f);
    EXPECT_FALSE(VolumetricLightProjectToCascade(identity, Vec3{ 1.5f, 0.0f, 0.5f }, u, v, z));
    EXPECT_FALSE(VolumetricLightProjectToCascade(identity, Vec3{ 0.0f, -1.5f, 0.5f }, u, v, z));
    EXPECT_FALSE(VolumetricLightProjectToCascade(identity, Vec3{ 0.0f, 0.0f, 1.5f }, u, v, z));
    EXPECT_FALSE(VolumetricLightProjectToCascade(identity, Vec3{ 0.0f, 0.0f, -0.1f }, u, v, z));
}

TEST(VolumetricLightMathTest, WindGrowsWrapsAndIgnoresBadSteps)
{
    std::array<double, 3> offset{};
    offset = VolumetricLightAdvanceWind(offset, 2.0f, -1.0f, 0.5f);
    EXPECT_DOUBLE_EQ(offset[0], 1.0);
    EXPECT_DOUBLE_EQ(offset[1], 0.0);
    EXPECT_DOUBLE_EQ(offset[2], kVolumetricLightNoiseWrap - 0.5);

    const std::array<double, 3> nearWrap{ kVolumetricLightNoiseWrap - 0.25, 0.0, 0.0 };
    const std::array<double, 3> wrapped = VolumetricLightAdvanceWind(nearWrap, 1.0f, 0.0f, 0.5f);
    EXPECT_NEAR(wrapped[0], 0.25, 1e-9);
    for (const double value : wrapped) {
        EXPECT_GE(value, 0.0);
        EXPECT_LT(value, kVolumetricLightNoiseWrap);
    }

    EXPECT_EQ(VolumetricLightAdvanceWind(nearWrap, 5.0f, 5.0f, kNaN), nearWrap);
    EXPECT_EQ(VolumetricLightAdvanceWind(nearWrap, 5.0f, 5.0f, -1.0f), nearWrap);
}

TEST(VolumetricLightMathTest, HaltonJitterIsInRangeAndRepeatsEveryCycle)
{
    EXPECT_FLOAT_EQ(VolumetricLightHaltonJitter(0).x, 0.5f);
    EXPECT_FLOAT_EQ(VolumetricLightHaltonJitter(1).x, 0.25f);
    EXPECT_FLOAT_EQ(VolumetricLightHaltonJitter(2).x, 0.75f);

    Vec3 mean = Vec3::Zero();
    for (std::uint32_t f = 0; f < kVolumetricLightHaltonCycle; ++f) {
        const Vec3 j = VolumetricLightHaltonJitter(f);
        for (int i = 0; i < 3; ++i) {
            EXPECT_GE(j[i], 0.0f);
            EXPECT_LT(j[i], 1.0f);
        }
        EXPECT_EQ(j, VolumetricLightHaltonJitter(f + kVolumetricLightHaltonCycle));
        mean += j;
    }
    mean /= static_cast<float>(kVolumetricLightHaltonCycle);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(mean[i], 0.5f, 0.1f);
    }
}

VolumetricLightHistory MakeGoodHistory(std::uint64_t key)
{
    VolumetricLightFrameFacts facts;
    facts.forward = Vec3::Forward();
    facts.maxDistance = 60.0f;
    facts.frameIndex = 10;
    VolumetricLightHistory h = MakeVolumetricLightHistory(facts, 2.0f, key, VolumetricLightHistory{});
    h.stableFrames = 1;
    return h;
}

TEST(VolumetricLightMathTest, HistoryRulesRejectEveryUnsafeCase)
{
    const VolumetricLightHistory good = MakeGoodHistory(7);
    const Vec3 eye = Vec3::Zero();
    const Vec3 forward = Vec3::Forward();

    EXPECT_TRUE(VolumetricLightHistoryUsable(good, eye, forward, 11, 7));
    EXPECT_FALSE(VolumetricLightHistoryUsable(VolumetricLightHistory{}, eye, forward, 11, 7));
    EXPECT_FALSE(VolumetricLightHistoryUsable(good, eye, forward, 12, 7)); // Frame gap.
    EXPECT_FALSE(VolumetricLightHistoryUsable(good, eye, forward, 11, 8)); // Lighting changed.

    VolumetricLightHistory young = good;
    young.stableFrames = 0;
    EXPECT_FALSE(VolumetricLightHistoryUsable(young, eye, forward, 11, 7));

    EXPECT_FALSE(VolumetricLightHistoryUsable(good, Vec3{ 16.0f, 0.0f, 0.0f }, forward, 11, 7)); // 0.25 * 60 = 15.
    EXPECT_TRUE(VolumetricLightHistoryUsable(good, Vec3{ 14.0f, 0.0f, 0.0f }, forward, 11, 7));
    EXPECT_FALSE(VolumetricLightHistoryUsable(good, eye, Normalize(Vec3{ 1.0f, 0.0f, 0.5f }), 11, 7)); // Turned over 30 degrees.
}

TEST(VolumetricLightMathTest, MakeHistoryCountsOnlyUninterruptedFrames)
{
    VolumetricLightFrameFacts facts;
    facts.frameIndex = 5;
    const VolumetricLightHistory first = MakeVolumetricLightHistory(facts, 2.0f, 1, VolumetricLightHistory{});
    EXPECT_EQ(first.stableFrames, 0u);

    facts.frameIndex = 6;
    const VolumetricLightHistory second = MakeVolumetricLightHistory(facts, 2.0f, 1, first);
    EXPECT_EQ(second.stableFrames, 1u);

    facts.frameIndex = 9;
    EXPECT_EQ(MakeVolumetricLightHistory(facts, 2.0f, 1, second).stableFrames, 0u);
    facts.frameIndex = 7;
    EXPECT_EQ(MakeVolumetricLightHistory(facts, 2.0f, 2, second).stableFrames, 0u);
}

TEST(VolumetricLightMathTest, HistoryWeightFadesAsTheSunMoves)
{
    VolumetricLightHistory h;
    h.sunDirection = Vec3::Up();
    const auto tilted = [](float degrees) {
        const float r = degrees * 3.14159265f / 180.0f;
        return Vec3{ std::sin(r), std::cos(r), 0.0f };
    };
    EXPECT_FLOAT_EQ(VolumetricLightHistoryWeight(h, Vec3::Up(), 0.9f), 0.9f);
    EXPECT_NEAR(VolumetricLightHistoryWeight(h, tilted(0.5f), 0.9f), 0.45f, 0.01f);
    EXPECT_FLOAT_EQ(VolumetricLightHistoryWeight(h, tilted(2.5f), 0.9f), 0.0f);
    EXPECT_FLOAT_EQ(VolumetricLightHistoryWeight(h, Vec3::Up(), kNaN), 0.0f);
}

TEST(VolumetricLightMathTest, SanitizeReplacesBadValuesAndClampsRanges)
{
    constexpr VolumetricLightSettings d{};
    VolumetricLightSettings bad;
    bad.density = kNaN;
    bad.albedo = std::numeric_limits<float>::infinity();
    bad.anisotropyG = 5.0f;
    bad.maxDistance = 1.0e9f;
    bad.startDistance = 5000.0f;
    bad.depthExponent = 0.1f;
    bad.historyBlend = 3.0f;
    bad.shadowSamples = 3;
    bad.debugView = static_cast<VolumetricLightDebugView>(77);
    const VolumetricLightSettings s = SanitizeVolumetricLightSettings(bad);

    EXPECT_FLOAT_EQ(s.density, d.density);
    EXPECT_FLOAT_EQ(s.albedo, d.albedo);
    EXPECT_FLOAT_EQ(s.anisotropyG, 0.95f);
    EXPECT_FLOAT_EQ(s.maxDistance, 2000.0f);
    EXPECT_LE(s.startDistance, s.maxDistance);
    EXPECT_FLOAT_EQ(s.depthExponent, 1.0f);
    EXPECT_FLOAT_EQ(s.historyBlend, 0.98f);
    EXPECT_EQ(s.shadowSamples, 4u);
    EXPECT_EQ(s.debugView, VolumetricLightDebugView::Off);
}

TEST(VolumetricLightMathTest, SanitizeSnapsShadowSamples)
{
    for (const auto& [in, out] : { std::pair{ 0u, 1u }, std::pair{ 1u, 1u }, std::pair{ 2u, 2u }, std::pair{ 3u, 4u },
             std::pair{ 5u, 4u }, std::pair{ 7u, 8u }, std::pair{ 99u, 8u } }) {
        VolumetricLightSettings s;
        s.shadowSamples = in;
        EXPECT_EQ(SanitizeVolumetricLightSettings(s).shadowSamples, out) << in;
    }
}

TEST(VolumetricLightMathTest, EffectiveMaxDistanceFollowsTheShadowCoverage)
{
    const VolumetricLightSettings s = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    EXPECT_FLOAT_EQ(VolumetricLightEffectiveMaxDistance(s, true, 40.0f), 40.0f);
    EXPECT_FLOAT_EQ(VolumetricLightEffectiveMaxDistance(s, true, 400.0f), s.maxDistance);
    EXPECT_FLOAT_EQ(VolumetricLightEffectiveMaxDistance(s, false, 40.0f), s.maxDistance);
    EXPECT_FLOAT_EQ(VolumetricLightEffectiveMaxDistance(s, true, 0.0f), s.maxDistance);
    EXPECT_FLOAT_EQ(VolumetricLightEffectiveMaxDistance(s, true, kNaN), s.maxDistance);
    EXPECT_FLOAT_EQ(VolumetricLightEffectiveMaxDistance(s, true, 0.3f), kVolumetricLightMinDistance);
}

TEST(VolumetricLightMathTest, LightingKeyChangesWithLitSettingsOnly)
{
    const VolumetricLightSettings base = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    VolumetricLightSettings denser = base;
    denser.density *= 2.0f;
    EXPECT_NE(VolumetricLightLightingKey(base), VolumetricLightLightingKey(denser));

    VolumetricLightSettings composite = base;
    composite.strength = 0.5f;
    composite.historyBlend = 0.5f;
    EXPECT_EQ(VolumetricLightLightingKey(base), VolumetricLightLightingKey(composite));
}

TEST(VolumetricLightMathTest, AmbientShadowedFractionDefaultIsLowAndIsClamped)
{
    EXPECT_FLOAT_EQ(VolumetricLightSettings{}.ambientShadowedFraction, 0.15f);

    VolumetricLightSettings s;
    s.ambientShadowedFraction = 5.0f;
    EXPECT_FLOAT_EQ(SanitizeVolumetricLightSettings(s).ambientShadowedFraction, 1.0f);
    s.ambientShadowedFraction = -2.0f;
    EXPECT_FLOAT_EQ(SanitizeVolumetricLightSettings(s).ambientShadowedFraction, 0.0f);
    s.ambientShadowedFraction = kNaN;
    EXPECT_FLOAT_EQ(SanitizeVolumetricLightSettings(s).ambientShadowedFraction, 0.15f);
}

TEST(VolumetricLightMathTest, SkyDistanceFractionIsClampedAndExcludedFromTheLightingKey)
{
    VolumetricLightSettings s;
    s.skyDistanceFraction = 5.0f;
    EXPECT_FLOAT_EQ(SanitizeVolumetricLightSettings(s).skyDistanceFraction, 1.0f);
    s.skyDistanceFraction = -1.0f;
    EXPECT_FLOAT_EQ(SanitizeVolumetricLightSettings(s).skyDistanceFraction, 0.0f);
    s.skyDistanceFraction = kNaN;
    EXPECT_FLOAT_EQ(SanitizeVolumetricLightSettings(s).skyDistanceFraction, 0.3f);

    const VolumetricLightSettings base = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    VolumetricLightSettings sky = base;
    sky.skyDistanceFraction = 0.9f;
    EXPECT_EQ(VolumetricLightLightingKey(base), VolumetricLightLightingKey(sky));
}

TEST(VolumetricLightMathTest, LightingKeyChangesWithTheAmbientShadowedFraction)
{
    const VolumetricLightSettings base = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    VolumetricLightSettings masked = base;
    masked.ambientShadowedFraction = 0.2f;
    EXPECT_NE(VolumetricLightLightingKey(base), VolumetricLightLightingKey(masked));
}

} // namespace
} // namespace gte
