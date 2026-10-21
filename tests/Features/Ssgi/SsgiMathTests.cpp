// CPU checks of the Ssgi math: settings, camera mapping, sampling, ray march rules, albedo guess.

#include "Features/Ssgi/SsgiMath.h"
#include "Features/Ssgi/SsgiTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gte {
namespace {

constexpr Extent2D kView{ 640, 360 };

// Eye at the origin looking along +z; pixel x grows along +x, pixel y grows along -y.
fx::CameraBasis MakeBasis()
{
    fx::CameraBasis b;
    b.valid = true;
    b.right = Vec3{ 1.0f, 0.0f, 0.0f };
    b.down = Vec3{ 0.0f, -1.0f, 0.0f };
    b.forward = Vec3{ 0.0f, 0.0f, 1.0f };
    b.pixelScale = 0.002f;
    return b;
}

TEST(SsgiMathTest, SanitizeReplacesBadValuesAndClampsRanges)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    constexpr SsgiSettings d{};

    SsgiSettings bad;
    bad.intensity = nan;
    bad.maxRayDistance = inf;
    bad.thickness = -3.0f;
    bad.depthBias = 9.0f;
    bad.rayCount = 0;
    bad.stepCount = 999;
    bad.stepDistributionPower = 0.0f;
    bad.endFadeFraction = 5.0f;
    bad.maxRadiance = 1.0e9f;
    bad.albedoLuma = 2.0f;
    bad.albedoSaturation = -1.0f;
    bad.maxDistance = 1.0e9f;
    bad.distanceFadeFraction = nan;
    bad.historyWeight = 5.0f;
    bad.depthSigma = -3.0f;
    bad.debugView = static_cast<SsgiDebugView>(77);
    const SsgiSettings s = SanitizeSsgiSettings(bad);

    EXPECT_FLOAT_EQ(s.intensity, d.intensity);
    EXPECT_FLOAT_EQ(s.maxRayDistance, d.maxRayDistance);
    EXPECT_FLOAT_EQ(s.thickness, 0.01f);
    EXPECT_FLOAT_EQ(s.depthBias, 1.0f);
    EXPECT_EQ(s.rayCount, 1u);
    EXPECT_EQ(s.stepCount, 48u);
    EXPECT_FLOAT_EQ(s.stepDistributionPower, 1.0f);
    EXPECT_FLOAT_EQ(s.endFadeFraction, 0.9f);
    EXPECT_FLOAT_EQ(s.maxRadiance, 64.0f);
    EXPECT_FLOAT_EQ(s.albedoLuma, 0.95f);
    EXPECT_FLOAT_EQ(s.albedoSaturation, 0.0f);
    EXPECT_FLOAT_EQ(s.maxDistance, 100000.0f);
    EXPECT_FLOAT_EQ(s.distanceFadeFraction, d.distanceFadeFraction);
    EXPECT_FLOAT_EQ(s.historyWeight, 0.98f);
    EXPECT_FLOAT_EQ(s.depthSigma, 0.001f);
    EXPECT_EQ(s.debugView, SsgiDebugView::Off);
}

TEST(SsgiMathTest, SanitizeKeepsValidValuesAndSnapsDivisor)
{
    SsgiSettings ok;
    ok.endFadeFraction = 0.0f;
    ok.intensity = 0.0f;
    ok.debugView = SsgiDebugView::Normals;
    const SsgiSettings s = SanitizeSsgiSettings(ok);
    EXPECT_FLOAT_EQ(s.endFadeFraction, 0.0f);
    EXPECT_FLOAT_EQ(s.intensity, 0.0f);
    EXPECT_EQ(s.debugView, SsgiDebugView::Normals);

    for (const auto& [in, out] : { std::pair{ 0u, 1u }, std::pair{ 1u, 1u }, std::pair{ 2u, 2u }, std::pair{ 3u, 2u } }) {
        SsgiSettings t;
        t.resolutionDivisor = in;
        EXPECT_EQ(SanitizeSsgiSettings(t).resolutionDivisor, out) << in;
    }
}

TEST(SsgiMathTest, EyeRelativePointAndProjectionRoundTrip)
{
    const fx::CameraBasis basis = MakeBasis();
    const float xs[] = { 0.0f, 320.0f, 639.0f };
    const float ys[] = { 0.0f, 180.0f, 359.0f };
    for (const float px : xs) {
        for (const float py : ys) {
            const Vec3 p = SsgiEyeRelativePoint(px, py, 7.0f, kView, basis);
            EXPECT_NEAR(Dot(p, basis.forward), 7.0f, 1e-4f);
            const SsgiProjection back = SsgiProjectToPixel(p, kView, basis);
            ASSERT_TRUE(back.valid);
            EXPECT_NEAR(back.x, px, 5e-3f);
            EXPECT_NEAR(back.y, py, 5e-3f);
            EXPECT_NEAR(back.viewZ, 7.0f, 1e-4f);
        }
    }
}

TEST(SsgiMathTest, ProjectionRejectsPointsBehindTheCamera)
{
    EXPECT_FALSE(SsgiProjectToPixel(Vec3{ 0.0f, 0.0f, -1.0f }, kView, MakeBasis()).valid);
    EXPECT_FALSE(SsgiProjectToPixel(Vec3{ 1.0f, 0.0f, 0.0f }, kView, MakeBasis()).valid);
}

TEST(SsgiMathTest, CosineHemisphereIsUnitLengthAboveTheSurfaceWithMeanTwoThirds)
{
    const Vec3 normals[] = { Normalize(Vec3{ 0.3f, 0.8f, -0.5f }), Vec3::Up(), Vec3::Right(), Vec3::Back() };
    for (const Vec3& n : normals) {
        constexpr int kSamples = 10000;
        double sum = 0.0;
        for (int i = 0; i < kSamples; ++i) {
            const float u0 = static_cast<float>(std::fmod(0.5 + i * 0.7548776662, 1.0));
            const float u1 = static_cast<float>(std::fmod(0.5 + i * 0.5698402910, 1.0));
            const Vec3 d = SsgiCosineHemisphereDirection(u0, u1, n);
            ASSERT_NEAR(Length(d), 1.0f, 1e-4f);
            ASSERT_GE(Dot(d, n), -1e-5f);
            sum += Dot(d, n);
        }
        EXPECT_NEAR(sum / kSamples, 2.0 / 3.0, 0.02);
    }
}

TEST(SsgiMathTest, RayNoiseStaysInRangeAndLooksUncorrelated)
{
    constexpr int kSize = 64;
    double sum0 = 0.0;
    double sum1 = 0.0;
    double sumJ = 0.0;
    double sum01 = 0.0;
    double sum00 = 0.0;
    double sum11 = 0.0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const SsgiRayNoise n = SsgiRayNoiseFor(x, y, 0, 1, 0);
            for (const float v : { n.u0, n.u1, n.jitter }) {
                ASSERT_GE(v, 0.0f);
                ASSERT_LT(v, 1.0f);
            }
            sum0 += n.u0;
            sum1 += n.u1;
            sumJ += n.jitter;
            sum01 += static_cast<double>(n.u0) * n.u1;
            sum00 += static_cast<double>(n.u0) * n.u0;
            sum11 += static_cast<double>(n.u1) * n.u1;
        }
    }
    constexpr double kCount = kSize * kSize;
    EXPECT_NEAR(sum0 / kCount, 0.5, 0.03);
    EXPECT_NEAR(sum1 / kCount, 0.5, 0.03);
    EXPECT_NEAR(sumJ / kCount, 0.5, 0.03);

    const double m0 = sum0 / kCount;
    const double m1 = sum1 / kCount;
    const double covariance = sum01 / kCount - m0 * m1;
    const double correlation = covariance / std::sqrt((sum00 / kCount - m0 * m0) * (sum11 / kCount - m1 * m1));
    EXPECT_LT(std::abs(correlation), 0.05);
}

TEST(SsgiMathTest, RayNoiseChangesWithFrameAndRayIndex)
{
    const SsgiRayNoise a = SsgiRayNoiseFor(10, 20, 0, 2, 0);
    const SsgiRayNoise nextFrame = SsgiRayNoiseFor(10, 20, 1, 2, 0);
    const SsgiRayNoise nextRay = SsgiRayNoiseFor(10, 20, 0, 2, 1);
    EXPECT_NE(a.u0, nextFrame.u0);
    EXPECT_NE(a.jitter, nextFrame.jitter);
    EXPECT_NE(a.u1, nextRay.u1);
}

TEST(SsgiMathTest, RayOriginOffsetGrowsWithDepthAndCoversHalfFloatStep)
{
    EXPECT_FLOAT_EQ(SsgiRayOriginOffset(0.03f, 1.0f, 6e-4f, 2), 0.03f);
    EXPECT_GT(SsgiRayOriginOffset(0.03f, 80.0f, 6e-4f, 2), SsgiRayOriginOffset(0.03f, 40.0f, 6e-4f, 2));
    EXPECT_GT(SsgiRayOriginOffset(0.03f, 80.0f, 6e-4f, 2), 0.0625f);
}

// A scene of one plane seen from the origin, sampled into a GI grid.
struct PlaneScene {
    Vec3 point;
    Vec3 normal;
    Vec3 radiance{ 1.0f, 0.5f, 0.25f };
};

SsgiGrid BuildGrid(const PlaneScene* scene, std::uint32_t divisor)
{
    const fx::CameraBasis basis = MakeBasis();
    SsgiGrid grid;
    grid.size = Extent2D{ kView.width / divisor, kView.height / divisor };
    grid.texels.resize(static_cast<std::size_t>(grid.size.width) * grid.size.height);
    if (scene == nullptr) {
        return grid;
    }
    for (std::uint32_t y = 0; y < grid.size.height; ++y) {
        for (std::uint32_t x = 0; x < grid.size.width; ++x) {
            const Vec3 d = SsgiEyeRelativePoint(static_cast<float>(x * divisor), static_cast<float>(y * divisor), 1.0f, kView, basis);
            const float denom = Dot(d, scene->normal);
            if (std::abs(denom) < 1e-6f) {
                continue;
            }
            const float viewZ = Dot(scene->point, scene->normal) / denom;
            if (viewZ <= 0.0f) {
                continue;
            }
            SsgiGridTexel& texel = grid.texels[static_cast<std::size_t>(y) * grid.size.width + x];
            texel.viewZ = viewZ;
            texel.normal = scene->normal;
            texel.radiance = scene->radiance;
        }
    }
    return grid;
}

SsgiSettings MarchSettings(std::uint32_t steps, float power, float maxDistance)
{
    SsgiSettings s;
    s.resolutionDivisor = 1;
    s.stepCount = steps;
    s.stepDistributionPower = power;
    s.maxRayDistance = maxDistance;
    return SanitizeSsgiSettings(s);
}

SsgiMarchResult March(const SsgiGrid& grid, const SsgiSettings& safe, const Vec3& origin, const Vec3& dir, float jitter)
{
    const float bias = SsgiRayOriginOffset(safe.depthBias, Dot(origin, Vec3::Forward()), MakeBasis().pixelScale, 1);
    return SsgiMarchReference(grid, MakeBasis(), kView, origin, dir, jitter, safe, bias);
}

TEST(SsgiMathTest, MarchFindsNothingInAnEmptyGrid)
{
    const SsgiGrid grid = BuildGrid(nullptr, 1);
    const SsgiMarchResult r = March(grid, MarchSettings(24, 1.0f, 8.0f), Vec3{ 0.0f, 0.0f, 1.0f }, Vec3::Forward(), 0.5f);
    EXPECT_FALSE(r.hit);
    EXPECT_FALSE(r.blocked);
}

TEST(SsgiMathTest, MarchHitsAFacingWallAtTheExpectedDistance)
{
    const PlaneScene wall{ Vec3{ 0.0f, 0.0f, 5.0f }, Vec3::Back() };
    const SsgiGrid grid = BuildGrid(&wall, 1);
    const SsgiMarchResult r = March(grid, MarchSettings(24, 2.0f, 8.0f), Vec3{ 0.0f, 0.0f, 1.0f }, Vec3::Forward(), 0.5f);

    ASSERT_TRUE(r.hit);
    EXPECT_FALSE(r.blocked);
    EXPECT_GE(r.distance, 4.0f);
    EXPECT_LT(r.distance, 4.6f);
    EXPECT_EQ(r.texelX, 320);
    EXPECT_EQ(r.texelY, 180);
    EXPECT_FLOAT_EQ(r.radiance.x, 1.0f);
    EXPECT_FLOAT_EQ(r.radiance.y, 0.5f);
}

TEST(SsgiMathTest, MarchPassesAWallTheRayStartedBehind)
{
    const PlaneScene wall{ Vec3{ 0.0f, 0.0f, 2.0f }, Vec3::Back() };
    const SsgiGrid grid = BuildGrid(&wall, 1);
    const SsgiMarchResult r = March(grid, MarchSettings(24, 2.0f, 8.0f), Vec3{ 0.0f, 0.0f, 3.0f }, Vec3::Forward(), 0.5f);
    EXPECT_FALSE(r.hit);
    EXPECT_FALSE(r.blocked);
}

TEST(SsgiMathTest, MarchHitsAThinWallCrossedBetweenTwoSteps)
{
    // Steps are 2 units apart; the wall is hit 0.6 behind, more than the 0.5 thickness.
    const PlaneScene wall{ Vec3{ 0.0f, 0.0f, 4.9f }, Vec3::Back() };
    const SsgiGrid grid = BuildGrid(&wall, 1);
    const SsgiSettings safe = MarchSettings(4, 1.0f, 8.0f);
    ASSERT_FLOAT_EQ(safe.thickness, 0.5f);

    const SsgiMarchResult r = March(grid, safe, Vec3{ 0.0f, 0.0f, 0.5f }, Vec3::Forward(), 0.5f);
    ASSERT_TRUE(r.hit);
    EXPECT_NEAR(r.distance, 5.0f, 1e-3f);
}

TEST(SsgiMathTest, MarchIsBlockedByABackFace)
{
    const PlaneScene wall{ Vec3{ 0.0f, 0.0f, 5.0f }, Vec3::Forward() };
    const SsgiGrid grid = BuildGrid(&wall, 1);
    const SsgiMarchResult r = March(grid, MarchSettings(24, 2.0f, 8.0f), Vec3{ 0.0f, 0.0f, 1.0f }, Vec3::Forward(), 0.5f);
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.blocked);
    EXPECT_FLOAT_EQ(r.radiance.x, 0.0f);
}

TEST(SsgiMathTest, MarchAlongAFlatSurfaceNeverHitsItself)
{
    const PlaneScene floor{ Vec3{ 0.0f, -1.0f, 0.0f }, Vec3::Up() };
    const SsgiGrid grid = BuildGrid(&floor, 1);
    const SsgiSettings safe = MarchSettings(24, 2.0f, 8.0f);
    const float bias = SsgiRayOriginOffset(safe.depthBias, 3.0f, MakeBasis().pixelScale, 1);
    const Vec3 origin{ 0.0f, -1.0f + bias, 3.0f };

    for (const float jitter : { 0.0f, 0.3f, 0.9f }) {
        const SsgiMarchResult r = SsgiMarchReference(grid, MakeBasis(), kView, origin, Vec3::Forward(), jitter, safe, bias);
        EXPECT_FALSE(r.hit) << jitter;
        EXPECT_FALSE(r.blocked) << jitter;
    }
}

TEST(SsgiMathTest, MarchDoesNotHitAnythingFromOutsideTheScreen)
{
    const PlaneScene wall{ Vec3{ 0.0f, 0.0f, 1.9f }, Vec3::Back() };
    const SsgiGrid grid = BuildGrid(&wall, 1);
    const SsgiMarchResult r = March(grid, MarchSettings(24, 2.0f, 8.0f), Vec3{ 1.4f, 0.0f, 2.0f }, Vec3::Right(), 0.5f);
    EXPECT_FALSE(r.hit);
}

TEST(SsgiMathTest, MarchFadesTheHitNearTheRayEnd)
{
    const PlaneScene wall{ Vec3{ 0.0f, 0.0f, 7.9f }, Vec3::Back(), Vec3{ 1.0f, 1.0f, 1.0f } };
    const SsgiGrid grid = BuildGrid(&wall, 1);
    SsgiSettings faded = MarchSettings(24, 1.0f, 8.0f);
    faded.endFadeFraction = 0.3f;
    const SsgiMarchResult r = March(grid, faded, Vec3{ 0.0f, 0.0f, 0.5f }, Vec3::Forward(), 0.5f);
    ASSERT_TRUE(r.hit);
    EXPECT_GT(r.radiance.x, 0.0f);
    EXPECT_LT(r.radiance.x, 1.0f);

    SsgiSettings hard = MarchSettings(24, 1.0f, 8.0f);
    hard.endFadeFraction = 0.0f;
    const SsgiMarchResult h = March(grid, hard, Vec3{ 0.0f, 0.0f, 0.5f }, Vec3::Forward(), 0.5f);
    ASSERT_TRUE(h.hit);
    EXPECT_TRUE(std::isfinite(h.radiance.x));
    EXPECT_FLOAT_EQ(h.radiance.x, 1.0f);
}

TEST(SsgiMathTest, AlbedoGuessFollowsHueAndStaysInRange)
{
    const Vec3 grey = SsgiAlbedoGuess(Vec3{ 1.0f, 1.0f, 1.0f }, 0.5f, 0.7f);
    EXPECT_NEAR(grey.x, 0.5f, 1e-5f);
    EXPECT_NEAR(grey.y, 0.5f, 1e-5f);
    EXPECT_NEAR(grey.z, 0.5f, 1e-5f);

    const Vec3 neutral = SsgiAlbedoGuess(Vec3{ 0.8f, 0.1f, 0.1f }, 0.5f, 0.0f);
    EXPECT_NEAR(neutral.x, 0.5f, 1e-5f);
    EXPECT_NEAR(neutral.z, 0.5f, 1e-5f);

    const Vec3 red = SsgiAlbedoGuess(Vec3{ 0.8f, 0.1f, 0.1f }, 0.5f, 1.0f);
    EXPECT_GT(red.x, red.y);
    EXPECT_GT(red.x, red.z);

    for (const Vec3& c : { Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 100.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 5.0f, 50.0f } }) {
        const Vec3 a = SsgiAlbedoGuess(c, 0.95f, 1.0f);
        for (const float v : { a.x, a.y, a.z }) {
            EXPECT_TRUE(std::isfinite(v));
            EXPECT_GE(v, 0.0f);
            EXPECT_LE(v, 0.95f);
        }
    }
}

} // namespace
} // namespace gte
