// Tests for ToneMapping settings sanitizing and the push block.

#include "Features/ToneMapping/ToneMappingMath.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

namespace gte {
namespace {

TEST(ToneMappingMathTest, DefaultsAreInsideTheirOwnRanges)
{
    const ToneMappingSettings d{};
    const ToneMappingSettings s = SanitizeToneMappingSettings(d);
    EXPECT_FLOAT_EQ(s.exposure, d.exposure);
    EXPECT_FLOAT_EQ(s.saturation, d.saturation);
    EXPECT_FLOAT_EQ(s.dither, d.dither);
    EXPECT_EQ(s.mode, d.mode);
}

TEST(ToneMappingMathTest, SanitizeClampsEveryField)
{
    ToneMappingSettings in;
    in.exposure = 100.0f;
    in.saturation = -3.0f;
    in.dither = 9.0f;
    const ToneMappingSettings high = SanitizeToneMappingSettings(in);
    EXPECT_FLOAT_EQ(high.exposure, 16.0f);
    EXPECT_FLOAT_EQ(high.saturation, 0.0f);
    EXPECT_FLOAT_EQ(high.dither, 2.0f);

    in.exposure = 0.0f;
    in.saturation = 9.0f;
    in.dither = -1.0f;
    const ToneMappingSettings low = SanitizeToneMappingSettings(in);
    EXPECT_FLOAT_EQ(low.exposure, 0.01f);
    EXPECT_FLOAT_EQ(low.saturation, 2.0f);
    EXPECT_FLOAT_EQ(low.dither, 0.0f);
}

TEST(ToneMappingMathTest, NonFiniteValuesFallBackToTheDefaults)
{
    const ToneMappingSettings d{};
    ToneMappingSettings in;
    in.exposure = std::numeric_limits<float>::quiet_NaN();
    in.saturation = std::numeric_limits<float>::infinity();
    in.dither = -std::numeric_limits<float>::infinity();
    const ToneMappingSettings s = SanitizeToneMappingSettings(in);
    EXPECT_FLOAT_EQ(s.exposure, d.exposure);
    EXPECT_FLOAT_EQ(s.saturation, d.saturation);
    EXPECT_FLOAT_EQ(s.dither, d.dither);
}

TEST(ToneMappingMathTest, UnknownModeFallsBackToTheDefault)
{
    ToneMappingSettings in;
    in.mode = static_cast<tonemap::ToneMapMode>(77);
    EXPECT_EQ(SanitizeToneMappingSettings(in).mode, ToneMappingSettings{}.mode);
}

TEST(ToneMappingMathTest, PushCarriesTheSettingsAndTheFrame)
{
    ToneMappingSettings s;
    s.exposure = 1.5f;
    s.mode = tonemap::ToneMapMode::PbrNeutral;
    s.saturation = 0.5f;
    s.dither = 0.25f;
    const ToneMappingPush p = MakeToneMappingPush(s, 0x1'0000'0007ull);

    EXPECT_FLOAT_EQ(p.exposureModeSaturationDither[0], 1.5f);
    EXPECT_FLOAT_EQ(p.exposureModeSaturationDither[1], 2.0f);
    EXPECT_FLOAT_EQ(p.exposureModeSaturationDither[2], 0.5f);
    EXPECT_FLOAT_EQ(p.exposureModeSaturationDither[3], 0.25f);
    EXPECT_EQ(p.frame[0], 7u);
    EXPECT_EQ(p.frame[1], 0u);
}

TEST(ToneMappingMathTest, PushLayoutMatchesTheShaderBlock)
{
    EXPECT_EQ(sizeof(ToneMappingPush), 32u);
    EXPECT_EQ(offsetof(ToneMappingPush, exposureModeSaturationDither), 0u);
    EXPECT_EQ(offsetof(ToneMappingPush, frame), 16u);
}

} // namespace
} // namespace gte
