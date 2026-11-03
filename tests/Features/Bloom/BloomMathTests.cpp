// Tests for Bloom settings sanitizing, the mip chain, the soft threshold and the shader contract.

#include "Features/Bloom/BloomMath.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace gte {
namespace {

std::string ReadShader(const char* fileName)
{
    std::ifstream file(std::string(GTE_REPO_SRC_DIR) + "/Features/Bloom/Shaders/" + fileName, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

TEST(BloomMathTest, DefaultsSurviveTheSanitizer)
{
    const BloomSettings d{};
    const BloomSettings s = SanitizeBloomSettings(d);
    EXPECT_EQ(s.enabled, d.enabled);
    EXPECT_FLOAT_EQ(s.threshold, d.threshold);
    EXPECT_FLOAT_EQ(s.knee, d.knee);
    EXPECT_FLOAT_EQ(s.intensity, d.intensity);
    EXPECT_EQ(s.levels, d.levels);
}

TEST(BloomMathTest, SanitizeClampsEveryField)
{
    BloomSettings in;
    in.threshold = 100.0f;
    in.knee = 5.0f;
    in.intensity = 50.0f;
    in.levels = 99;
    const BloomSettings high = SanitizeBloomSettings(in);
    EXPECT_FLOAT_EQ(high.threshold, 16.0f);
    EXPECT_FLOAT_EQ(high.knee, 1.0f);
    EXPECT_FLOAT_EQ(high.intensity, 4.0f);
    EXPECT_EQ(high.levels, kBloomMaxLevels);

    in.threshold = -1.0f;
    in.knee = -1.0f;
    in.intensity = -1.0f;
    in.levels = 0;
    const BloomSettings low = SanitizeBloomSettings(in);
    EXPECT_FLOAT_EQ(low.threshold, 0.0f);
    EXPECT_FLOAT_EQ(low.knee, 0.0f);
    EXPECT_FLOAT_EQ(low.intensity, 0.0f);
    EXPECT_EQ(low.levels, kBloomMinLevels);
}

TEST(BloomMathTest, NonFiniteValuesFallBackToTheDefaults)
{
    const BloomSettings d{};
    BloomSettings in;
    in.threshold = std::numeric_limits<float>::quiet_NaN();
    in.knee = std::numeric_limits<float>::infinity();
    in.intensity = -std::numeric_limits<float>::infinity();
    const BloomSettings s = SanitizeBloomSettings(in);
    EXPECT_FLOAT_EQ(s.threshold, d.threshold);
    EXPECT_FLOAT_EQ(s.knee, d.knee);
    EXPECT_FLOAT_EQ(s.intensity, d.intensity);
}

TEST(BloomMathTest, MipChainHalvesAndRoundsUp)
{
    const Extent2D full{ 1920, 1080 };
    const Extent2D expected[] = { { 960, 540 }, { 480, 270 }, { 240, 135 }, { 120, 68 }, { 60, 34 }, { 30, 17 } };
    for (std::uint32_t level = 0; level < 6; ++level) {
        const Extent2D mip = BloomMipExtent(full, level);
        EXPECT_EQ(mip.width, expected[level].width) << "level " << level;
        EXPECT_EQ(mip.height, expected[level].height) << "level " << level;
    }
    const Extent2D odd = BloomMipExtent(Extent2D{ 5, 3 }, 0);
    EXPECT_EQ(odd.width, 3u);
    EXPECT_EQ(odd.height, 2u);
    EXPECT_EQ(BloomMipExtent(Extent2D{ 0, 100 }, 0).width, 0u);
}

TEST(BloomMathTest, ActiveLevelsShrinkWithTheExtent)
{
    EXPECT_EQ(BloomActiveLevels(Extent2D{ 1920, 1080 }, 5), 5u);
    EXPECT_EQ(BloomActiveLevels(Extent2D{ 1920, 1080 }, 99), kBloomMaxLevels);
    EXPECT_EQ(BloomActiveLevels(Extent2D{ 1920, 1080 }, 0), kBloomMinLevels);
    EXPECT_EQ(BloomActiveLevels(Extent2D{ 8, 8 }, 5), 2u);
    EXPECT_EQ(BloomActiveLevels(Extent2D{ 2, 2 }, 5), 0u);
    EXPECT_EQ(BloomActiveLevels(Extent2D{ 0, 0 }, 5), 0u);
}

TEST(BloomMathTest, SoftThresholdCurve)
{
    EXPECT_FLOAT_EQ(BloomSoftThresholdWeight(0.0f, 1.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(BloomSoftThresholdWeight(0.4f, 1.0f, 0.5f), 0.0f);
    EXPECT_NEAR(BloomSoftThresholdWeight(1.0f, 1.0f, 0.5f), 0.125f, 1e-4f);
    EXPECT_NEAR(BloomSoftThresholdWeight(10.0f, 1.0f, 0.5f), 0.9f, 1e-4f);
    EXPECT_NEAR(BloomSoftThresholdWeight(2.0f, 1.0f, 0.0f), 0.5f, 1e-4f);

    // The kept radiance never decreases as the pixel gets brighter.
    float previous = 0.0f;
    for (float brightness = 0.0f; brightness <= 4.0f; brightness += 0.05f) {
        const float kept = brightness * BloomSoftThresholdWeight(brightness, 1.0f, 0.5f);
        EXPECT_GE(kept + 1e-6f, previous);
        previous = kept;
    }
}

TEST(BloomMathTest, PushCarriesTheSettings)
{
    BloomSettings s;
    s.threshold = 2.0f;
    s.knee = 0.25f;
    s.intensity = 0.6f;
    const BloomPrefilterPush prefilter = MakeBloomPrefilterPush(s);
    EXPECT_FLOAT_EQ(prefilter.thresholdKneeMax[0], 2.0f);
    EXPECT_FLOAT_EQ(prefilter.thresholdKneeMax[1], 0.5f);
    EXPECT_FLOAT_EQ(prefilter.thresholdKneeMax[2], kBloomMaxRadiance);
    EXPECT_FLOAT_EQ(MakeBloomUpPush(s, true).scaleAndPad[0], 0.6f);
    EXPECT_FLOAT_EQ(MakeBloomUpPush(s, false).scaleAndPad[0], 1.0f);
}

TEST(BloomMathTest, PushLayoutMatchesTheShaderBlocks)
{
    EXPECT_EQ(sizeof(BloomPrefilterPush), 16u);
    EXPECT_EQ(offsetof(BloomPrefilterPush, thresholdKneeMax), 0u);
    EXPECT_EQ(sizeof(BloomUpPush), 16u);
    EXPECT_EQ(offsetof(BloomUpPush, scaleAndPad), 0u);
}

TEST(BloomShaderTest, SourcesKeepTheNamesAndConstantsTheCppSideBinds)
{
    const std::string prefilter = ReadShader("BloomPrefilter.comp");
    ASSERT_FALSE(prefilter.empty());
    EXPECT_NE(prefilter.find("vec4 thresholdKneeMax;"), std::string::npos);
    EXPECT_NE(prefilter.find("uniform sampler2D sourceColor;"), std::string::npos);
    EXPECT_NE(prefilter.find("image2D destinationImage;"), std::string::npos);
    EXPECT_NE(prefilter.find("soft * soft / (4.0 * knee + 1e-5)"), std::string::npos);
    EXPECT_NE(prefilter.find("max(soft, brightness - threshold) / max(brightness, 1e-5)"), std::string::npos);

    const std::string down = ReadShader("BloomDown.comp");
    ASSERT_FALSE(down.empty());
    EXPECT_NE(down.find("uniform sampler2D sourceColor;"), std::string::npos);
    EXPECT_NE(down.find("image2D destinationImage;"), std::string::npos);
    EXPECT_EQ(down.find("push_constant"), std::string::npos);

    const std::string up = ReadShader("BloomUp.comp");
    ASSERT_FALSE(up.empty());
    EXPECT_NE(up.find("vec4 scaleAndPad;"), std::string::npos);
    EXPECT_NE(up.find("uniform sampler2D sourceCurrent;"), std::string::npos);
    EXPECT_NE(up.find("uniform sampler2D sourceLower;"), std::string::npos);
    EXPECT_NE(up.find("image2D destinationImage;"), std::string::npos);
}

} // namespace
} // namespace gte
