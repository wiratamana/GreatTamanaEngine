// CPU checks of the Gtao math: camera basis, falloff, slice integral against brute force, settings, history.

#include "Features/Gtao/GtaoMath.h"
#include "Features/Gtao/GtaoTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gte {
namespace {

struct TestCamera {
    Mat4 viewProjection;
    Mat4 inverse;
    Vec3 eye;
};

TestCamera MakeCamera(bool flipY, bool orthographic = false)
{
    TestCamera c;
    c.eye = Vec3{ 3.0f, 1.7f, -4.0f };
    const Mat4 view = Mat4::LookAtLH(c.eye, Vec3{ 1.0f, 1.0f, 2.0f }, Vec3::Up());
    const Mat4 proj = orthographic ? Mat4::OrthographicLH_ZO(-8.0f, 8.0f, -4.5f, 4.5f, 0.1f, 100.0f, flipY)
                                   : Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 200.0f, flipY);
    c.viewProjection = proj * view;
    EXPECT_TRUE(c.viewProjection.TryInverse(c.inverse));
    return c;
}

TEST(GtaoMathTest, BasisMatchesPerspectiveForBothYConventions)
{
    for (const bool flipY : { false, true }) {
        const TestCamera cam = MakeCamera(flipY);
        const GtaoCameraBasis basis = DeriveGtaoCameraBasis(cam.eye, cam.inverse, Extent2D{ 1280, 720 });
        ASSERT_TRUE(basis.valid) << "flipY " << flipY;

        EXPECT_NEAR(Dot(basis.right, basis.forward), 0.0f, 1e-4f);
        EXPECT_NEAR(Dot(basis.down, basis.forward), 0.0f, 1e-4f);
        const Vec3 toTarget = Normalize(Vec3{ 1.0f, 1.0f, 2.0f } - cam.eye);
        EXPECT_NEAR(Dot(basis.forward, toTarget), 1.0f, 1e-4f);

        const float expected = 2.0f * std::tan(0.5f) * (16.0f / 9.0f) / 1280.0f;
        EXPECT_NEAR(basis.pixelScale, expected, expected * 1e-3f) << "flipY " << flipY;
    }
}

TEST(GtaoMathTest, BasisRejectsOrthographicCamera)
{
    const TestCamera cam = MakeCamera(false, true);
    EXPECT_FALSE(DeriveGtaoCameraBasis(cam.eye, cam.inverse, Extent2D{ 1280, 720 }).valid);
}

TEST(GtaoMathTest, BasisRejectsEmptyExtent)
{
    const TestCamera cam = MakeCamera(false);
    EXPECT_FALSE(DeriveGtaoCameraBasis(cam.eye, cam.inverse, Extent2D{ 0, 720 }).valid);
}

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

TEST(GtaoMathTest, AoExtentRoundsUpAndNeverReachesZero)
{
    const Extent2D odd = AoExtentFor(Extent2D{ 1919, 1079 }, 2);
    EXPECT_EQ(odd.width, 960u);
    EXPECT_EQ(odd.height, 540u);
    const Extent2D tiny = AoExtentFor(Extent2D{ 1, 1 }, 2);
    EXPECT_EQ(tiny.width, 1u);
    EXPECT_EQ(tiny.height, 1u);
    EXPECT_EQ(AoExtentFor(Extent2D{ 0, 0 }, 2).width, 1u);
}

TEST(GtaoMathTest, HistoryRulesRejectEveryUnsafeCase)
{
    GtaoCameraBasis basis;
    basis.valid = true;
    basis.forward = Vec3::Forward();
    const Extent2D extent{ 640, 360 };
    GtaoHistory good = MakeHistory(basis, Mat4::Identity(), 10, extent, 2, GtaoHistory{});
    good.stableFrames = 2;

    EXPECT_TRUE(HistoryUsable(good, basis, 11, extent, 2));
    EXPECT_FALSE(HistoryUsable(GtaoHistory{}, basis, 11, extent, 2));
    EXPECT_FALSE(HistoryUsable(good, basis, 12, extent, 2)); // Frame gap.
    EXPECT_FALSE(HistoryUsable(good, basis, 11, Extent2D{ 641, 360 }, 2));
    EXPECT_FALSE(HistoryUsable(good, basis, 11, extent, 1));

    GtaoHistory young = good;
    young.stableFrames = 1;
    EXPECT_FALSE(HistoryUsable(young, basis, 11, extent, 2));

    GtaoCameraBasis jumped = basis;
    jumped.eye = Vec3{ 50.0f, 0.0f, 0.0f };
    EXPECT_FALSE(HistoryUsable(good, jumped, 11, extent, 2));

    GtaoCameraBasis turned = basis;
    turned.forward = Vec3::Back();
    EXPECT_FALSE(HistoryUsable(good, turned, 11, extent, 2));
}

TEST(GtaoMathTest, MakeHistoryCountsOnlyUninterruptedStableFrames)
{
    GtaoCameraBasis basis;
    basis.valid = true;
    const Extent2D extent{ 640, 360 };

    const GtaoHistory first = MakeHistory(basis, Mat4::Identity(), 5, extent, 2, GtaoHistory{});
    EXPECT_EQ(first.stableFrames, 0u);
    const GtaoHistory second = MakeHistory(basis, Mat4::Identity(), 6, extent, 2, first);
    EXPECT_EQ(second.stableFrames, 1u);
    const GtaoHistory afterGap = MakeHistory(basis, Mat4::Identity(), 9, extent, 2, second);
    EXPECT_EQ(afterGap.stableFrames, 0u);
    const GtaoHistory resized = MakeHistory(basis, Mat4::Identity(), 10, Extent2D{ 320, 180 }, 2, afterGap);
    EXPECT_EQ(resized.stableFrames, 0u);
}

} // namespace
} // namespace gte
