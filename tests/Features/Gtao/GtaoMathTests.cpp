// CPU checks of the Gtao math: falloff, slice integral against brute force, settings.

#include "Features/Gtao/GtaoMath.h"
#include "Features/Gtao/GtaoTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gte {
namespace {

TEST(GtaoMathTest, FalloffWeightIsZeroAtRadiusAndOneInsideTheFadeStart)
{
    const float radius = 2.0f;
    const GtaoFalloff f = MakeGtaoFalloff(radius, 0.5f);
    const auto weight = [&](float dist) { return std::clamp(dist * f.mul + f.add, 0.0f, 1.0f); };
    EXPECT_NEAR(weight(radius), 0.0f, 1e-5f);
    EXPECT_NEAR(weight(0.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(weight(radius * 0.5f), 1.0f, 1e-5f);
    EXPECT_NEAR(weight(radius * 0.75f), 0.5f, 1e-5f);
}

TEST(GtaoMathTest, ScreenRadiusShrinksWithDepthAndClamps)
{
    const float closeRadius = GtaoScreenRadiusPixels(1.0f, 2.0f, 0.002f, 1000.0f);
    const float farRadius = GtaoScreenRadiusPixels(1.0f, 4.0f, 0.002f, 1000.0f);
    EXPECT_NEAR(closeRadius, 2.0f * farRadius, 1e-3f);
    EXPECT_FLOAT_EQ(GtaoScreenRadiusPixels(1.0f, 0.01f, 0.002f, 192.0f), 192.0f);
}

// View-aligned frame: the view vector looks down -z and a slice direction is an angle around it.
struct SliceSetup {
    Vec3 viewVec{ 0.0f, 0.0f, -1.0f };
    Vec3 right{ 1.0f, 0.0f, 0.0f };
    Vec3 down{ 0.0f, -1.0f, 0.0f };

    Vec3 SliceDir(float phi) const { return right * std::cos(phi) + down * std::sin(phi); }
};

Vec3 TiltedNormal(float degrees)
{
    const float t = degrees * kPi / 180.0f;
    return Vec3{ std::sin(t), 0.0f, -std::cos(t) };
}

float OpenSliceVisibility(const SliceSetup& s, const Vec3& normal, float phi)
{
    const GtaoSliceFrame slice = MakeGtaoSliceFrame(s.viewVec, normal, s.SliceDir(phi));
    return GtaoSliceVisibility(slice, std::cos(slice.n + kPi * 0.5f), std::cos(slice.n - kPi * 0.5f));
}

TEST(GtaoMathTest, SliceOfFacingOpenSurfaceIsExactlyOne)
{
    const SliceSetup s;
    EXPECT_NEAR(OpenSliceVisibility(s, s.viewVec, 0.3f), 1.0f, 1e-5f);
}

TEST(GtaoMathTest, TiltedOpenPlaneAveragesToOne)
{
    const SliceSetup s;
    for (const float tiltDegrees : { 20.0f, 45.0f, 70.0f }) {
        constexpr int kSlices = 180;
        float sum = 0.0f;
        for (int i = 0; i < kSlices; ++i) {
            sum += OpenSliceVisibility(s, TiltedNormal(tiltDegrees), (static_cast<float>(i) + 0.5f) / kSlices * kPi);
        }
        EXPECT_NEAR(sum / kSlices, 1.0f, 0.01f) << "tilt " << tiltDegrees;
    }
}

// A wall of any distance blocks every direction that points into its half-space.
struct HalfSpaceOccluder {
    Vec3 axis;
    bool Blocks(const Vec3& d) const { return Dot(d, axis) > 0.0f; }
};

// Smallest angle from the view vector, going along `sign * orthoDir`, whose direction is blocked.
float HorizonCosine(const HalfSpaceOccluder& wall, const SliceSetup& s, const GtaoSliceFrame& slice, float sign)
{
    constexpr int kSteps = 3600;
    for (int i = 0; i <= kSteps; ++i) {
        const float theta = kPi * static_cast<float>(i) / kSteps;
        const Vec3 d = s.viewVec * std::cos(theta) + slice.orthoDir * (sign * std::sin(theta));
        if (wall.Blocks(d)) {
            return std::cos(theta);
        }
    }
    return -1.0f;
}

// Cosine-weighted visible share of the normal's hemisphere, from stratified directions.
float BruteForceVisibility(const HalfSpaceOccluder& wall, const Vec3& normal)
{
    const Vec3 helper = std::abs(normal.x) < 0.9f ? Vec3::Right() : Vec3::Up();
    const Vec3 tangent = Normalize(Cross(helper, normal));
    const Vec3 bitangent = Cross(normal, tangent);
    constexpr int kGrid = 400;
    int visible = 0;
    for (int i = 0; i < kGrid; ++i) {
        for (int j = 0; j < kGrid; ++j) {
            const float u = (static_cast<float>(i) + 0.5f) / kGrid;
            const float phi = 2.0f * kPi * (static_cast<float>(j) + 0.5f) / kGrid;
            const float r = std::sqrt(u);
            const Vec3 d = tangent * (r * std::cos(phi)) + bitangent * (r * std::sin(phi)) + normal * std::sqrt(1.0f - u);
            visible += wall.Blocks(d) ? 0 : 1;
        }
    }
    return static_cast<float>(visible) / (kGrid * kGrid);
}

float SliceAveragedVisibility(const HalfSpaceOccluder& wall, const SliceSetup& s, const Vec3& normal)
{
    constexpr int kSlices = 256;
    float sum = 0.0f;
    for (int i = 0; i < kSlices; ++i) {
        const float phi = (static_cast<float>(i) + 0.5f) / kSlices * kPi;
        const GtaoSliceFrame slice = MakeGtaoSliceFrame(s.viewVec, normal, s.SliceDir(phi));
        sum += GtaoSliceVisibility(slice, HorizonCosine(wall, s, slice, 1.0f), HorizonCosine(wall, s, slice, -1.0f));
    }
    return sum / kSlices;
}

TEST(GtaoMathTest, SliceIntegralMatchesBruteForceHemisphere)
{
    const SliceSetup s;
    struct Case {
        float tiltDegrees;
        Vec3 wallAxis;
    };
    const Case cases[] = {
        { 0.0f, Vec3{ 1.0f, 0.0f, 0.0f } },
        { 30.0f, Vec3{ 1.0f, 0.0f, 0.0f } },
        { -20.0f, Vec3{ 0.6f, 0.64f, 0.48f } },
        { 50.0f, Vec3{ 0.866f, 0.0f, 0.5f } },
    };
    for (const Case& c : cases) {
        const Vec3 normal = TiltedNormal(c.tiltDegrees);
        const HalfSpaceOccluder wall{ Normalize(c.wallAxis) };
        ASSERT_LE(Dot(s.viewVec, wall.axis), 0.0f); // The view vector itself must stay visible.

        EXPECT_NEAR(SliceAveragedVisibility(wall, s, normal), BruteForceVisibility(wall, normal), 0.01f)
            << "tilt " << c.tiltDegrees;
    }
}

TEST(GtaoMathTest, SanitizeReplacesBadValuesAndClampsRanges)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    constexpr GtaoSettings d{};

    GtaoSettings bad;
    bad.radius = nan;
    bad.strength = inf;
    bad.depthSigma = -3.0f;
    bad.historyWeight = 5.0f;
    bad.sliceCount = 99;
    bad.stepCount = 0;
    bad.maxDistance = 1.0e9f;
    bad.debugView = static_cast<GtaoDebugView>(77);
    const GtaoSettings s = SanitizeGtaoSettings(bad);

    EXPECT_FLOAT_EQ(s.radius, d.radius);
    EXPECT_FLOAT_EQ(s.strength, d.strength);
    EXPECT_FLOAT_EQ(s.depthSigma, 0.001f);
    EXPECT_FLOAT_EQ(s.historyWeight, 0.98f);
    EXPECT_EQ(s.sliceCount, 8u);
    EXPECT_EQ(s.stepCount, 1u);
    EXPECT_FLOAT_EQ(s.maxDistance, 100000.0f);
    EXPECT_EQ(s.debugView, GtaoDebugView::Off);
}

TEST(GtaoMathTest, SanitizeSnapsDivisorToOneOrTwo)
{
    for (const auto& [in, out] : { std::pair{ 0u, 1u }, std::pair{ 1u, 1u }, std::pair{ 2u, 2u }, std::pair{ 3u, 2u }, std::pair{ 7u, 2u } }) {
        GtaoSettings s;
        s.resolutionDivisor = in;
        EXPECT_EQ(SanitizeGtaoSettings(s).resolutionDivisor, out) << in;
    }
}

} // namespace
} // namespace gte
