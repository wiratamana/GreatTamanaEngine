// Tests for the tone map curves (src/Math/ToneMapCurves.h) against hand-calculated values.

#include "Math/ToneMapCurves.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

using namespace tonemap;

TEST(ToneMapCurvesTests, AcesMatchesReferenceValues)
{
    EXPECT_FLOAT_EQ(AcesFilm(0.0f), 0.0f);
    EXPECT_NEAR(AcesFilm(0.18f), 0.2669f, 0.0005f);
    EXPECT_NEAR(AcesFilm(1.0f), 0.8038f, 0.0005f);
    EXPECT_LE(AcesFilm(100.0f), 1.0f);
}

TEST(ToneMapCurvesTests, PbrNeutralMatchesReferenceValues)
{
    const Vec3 mid = PbrNeutral(Vec3{ 0.5f, 0.5f, 0.5f });
    EXPECT_NEAR(mid.x, 0.46f, 0.0005f);
    EXPECT_NEAR(mid.y, 0.46f, 0.0005f);
    EXPECT_NEAR(mid.z, 0.46f, 0.0005f);

    const Vec3 one = PbrNeutral(Vec3{ 1.0f, 1.0f, 1.0f });
    EXPECT_NEAR(one.x, 0.8691f, 0.0005f);
    EXPECT_NEAR(one.y, 0.8691f, 0.0005f);
    EXPECT_NEAR(one.z, 0.8691f, 0.0005f);
}

TEST(ToneMapCurvesTests, SrgbEncodeMatchesReferenceAndRoundTrips)
{
    EXPECT_NEAR(SrgbEncode(0.5f), 0.7354f, 0.0005f);
    EXPECT_FLOAT_EQ(SrgbEncode(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(SrgbEncode(1.0f), 1.0f);
    for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
        EXPECT_NEAR(SrgbDecode(SrgbEncode(x)), x, 1e-5f) << x;
    }
}

TEST(ToneMapCurvesTests, NonFiniteInputGivesBlack)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(SrgbEncode(nan), 0.0f);
    EXPECT_TRUE(ApproximatelyEqual(ToneMapLinearToDisplay(Vec3{ nan, 1.0f, 1.0f }, 1.0f, ToneMapMode::Aces, 1.0f), Vec3::Zero()));
    EXPECT_TRUE(ApproximatelyEqual(ToneMapLinearToDisplay(Vec3{ inf, 1.0f, 1.0f }, 1.0f, ToneMapMode::Clamp, 1.0f), Vec3::Zero()));
}

TEST(ToneMapCurvesTests, ClampModeEncodesAndClips)
{
    const Vec3 out = ToneMapLinearToDisplay(Vec3{ 0.5f, 4.0f, -1.0f }, 1.0f, ToneMapMode::Clamp, 1.0f);
    EXPECT_NEAR(out.x, 0.7354f, 0.0005f);
    EXPECT_FLOAT_EQ(out.y, 1.0f);
    EXPECT_FLOAT_EQ(out.z, 0.0f);
}

TEST(ToneMapCurvesTests, ExposureScalesBeforeTheCurve)
{
    const Vec3 out = ToneMapLinearToDisplay(Vec3{ 0.25f, 0.25f, 0.25f }, 2.0f, ToneMapMode::Clamp, 1.0f);
    EXPECT_NEAR(out.x, SrgbEncode(0.5f), 1e-6f);
}

TEST(ToneMapCurvesTests, ZeroSaturationGivesGreyAndNeverNegative)
{
    const Vec3 grey = ApplySaturation(Vec3{ 1.0f, 0.0f, 0.0f }, 0.0f);
    EXPECT_NEAR(grey.x, grey.y, 1e-6f);
    EXPECT_NEAR(grey.y, grey.z, 1e-6f);

    const Vec3 boosted = ApplySaturation(Vec3{ 1.0f, 0.0f, 0.0f }, 2.0f);
    EXPECT_GE(boosted.y, 0.0f);
    EXPECT_GE(boosted.z, 0.0f);
}

TEST(ToneMapCurvesTests, DisplayOutputStaysInRangeForAllModes)
{
    for (const ToneMapMode mode : { ToneMapMode::Clamp, ToneMapMode::Aces, ToneMapMode::PbrNeutral }) {
        const Vec3 out = ToneMapLinearToDisplay(Vec3{ 60000.0f, 0.5f, 0.0f }, 0.8f, mode, 1.1f);
        for (int i = 0; i < 3; ++i) {
            EXPECT_GE(out[i], 0.0f);
            EXPECT_LE(out[i], 1.0f);
        }
    }
}

} // namespace
} // namespace gte
