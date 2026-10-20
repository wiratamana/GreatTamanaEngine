// Tests for the RGBA16F to RGBA8 visualization modes and the texture name classifier.

#include "Encoding/HdrColorVisualization.h"
#include "Math/ToneMapCurves.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gte::Encoding {
namespace {

constexpr std::uint16_t kHalfZero = 0x0000;
constexpr std::uint16_t kHalfOneSixteenth = 0x2C00;
constexpr std::uint16_t kHalfQuarter = 0x3400;
constexpr std::uint16_t kHalfHalf = 0x3800;
constexpr std::uint16_t kHalfOne = 0x3C00;
constexpr std::uint16_t kHalfTwo = 0x4000;
constexpr std::uint16_t kHalfEight = 0x4800;
constexpr std::uint16_t kHalfMinusOne = 0xBC00;

// Packs RGBA half-float texels (alpha fixed at 1.0) into a tightly packed readback buffer.
std::vector<std::uint8_t> Pack(const std::vector<std::array<std::uint16_t, 3>>& texels)
{
    std::vector<std::uint8_t> bytes(texels.size() * 8);
    for (std::size_t i = 0; i < texels.size(); ++i) {
        const std::uint16_t rgba[4] = { texels[i][0], texels[i][1], texels[i][2], kHalfOne };
        std::memcpy(bytes.data() + i * 8, rgba, 8);
    }
    return bytes;
}

std::vector<std::uint8_t> Convert(const std::vector<std::array<std::uint16_t, 3>>& texels, HdrVisualization mode)
{
    const std::vector<std::uint8_t> raw = Pack(texels);
    std::vector<std::uint8_t> out(texels.size() * 4, 0xCD);
    EXPECT_TRUE(ConvertRgba16fToRgba8(
        raw.data(), PixelFormat::RGBA16_Float, static_cast<int>(texels.size()), 1, out.data(), mode));
    return out;
}

std::uint8_t OldDebugScaled(std::uint16_t half)
{
    const float v = DecodeHalfFloat(half);
    if (!(v > 0.0f)) {
        return 0;
    }
    const float e = v * 400.0f;
    const float t = std::min(e / (1.0f + e), 1.0f);
    return static_cast<std::uint8_t>(t * 255.0f + 0.5f);
}

TEST(HdrColorVisualizationTest, DisplayReferredRoundsClampsAndKeepsAlphaOpaque)
{
    const auto out = Convert({ { kHalfHalf, kHalfTwo, kHalfMinusOne }, { kHalfZero, kHalfOne, kHalfHalf } },
        HdrVisualization::DisplayReferred);

    EXPECT_EQ(out[0], 128);
    EXPECT_EQ(out[1], 255);
    EXPECT_EQ(out[2], 0);
    EXPECT_EQ(out[3], 255);
    EXPECT_EQ(out[4], 0);
    EXPECT_EQ(out[5], 255);
    EXPECT_EQ(out[6], 128);
    EXPECT_EQ(out[7], 255);
}

TEST(HdrColorVisualizationTest, SceneReferredMapsBlackToBlackAndIsMonotonic)
{
    const std::array<std::uint16_t, 7> values = { kHalfZero, kHalfOneSixteenth, kHalfQuarter, kHalfHalf,
        kHalfOne, kHalfTwo, kHalfEight };
    std::vector<std::array<std::uint16_t, 3>> texels;
    for (const std::uint16_t v : values) {
        texels.push_back({ v, v, v });
    }
    const auto out = Convert(texels, HdrVisualization::SceneReferred);

    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[3], 255);
    for (std::size_t i = 1; i < values.size(); ++i) {
        EXPECT_LE(out[(i - 1) * 4], out[i * 4]) << "texel " << i;
    }
    EXPECT_LT(out[4], out[12]);

    const Vec3 one = tonemap::ToneMapLinearToDisplay(Vec3{ 1.0f, 1.0f, 1.0f }, tonemap::kDefaultExposure,
        tonemap::kDefaultMode, tonemap::kDefaultSaturation);
    EXPECT_EQ(out[4 * 4], static_cast<std::uint8_t>(one.x * 255.0f + 0.5f));
}

TEST(HdrColorVisualizationTest, DebugScaledMatchesTheFixedExposureReinhardFormula)
{
    const std::vector<std::array<std::uint16_t, 3>> texels = { { 0x1400, kHalfZero, kHalfMinusOne },
        { kHalfOneSixteenth, kHalfOne, kHalfEight } };
    const auto out = Convert(texels, HdrVisualization::DebugScaled);

    for (std::size_t i = 0; i < texels.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_EQ(out[i * 4 + c], OldDebugScaled(texels[i][c])) << "texel " << i << " channel " << c;
        }
        EXPECT_EQ(out[i * 4 + 3], 255);
    }
}

TEST(HdrColorVisualizationTest, NonHalfFloatFormatIsRejectedWithoutWriting)
{
    const std::vector<std::uint8_t> raw(8, 0);
    std::vector<std::uint8_t> out(4, 0xCD);
    EXPECT_FALSE(ConvertRgba16fToRgba8(
        raw.data(), PixelFormat::RGBA8_UNorm, 1, 1, out.data(), HdrVisualization::DisplayReferred));
    EXPECT_EQ(out[0], 0xCD);
}

TEST(HdrColorVisualizationTest, TextureNameClassifierPicksTheMode)
{
    EXPECT_EQ(HdrVisualizationForTextureName("GameView"), HdrVisualization::DisplayReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("SceneView"), HdrVisualization::DisplayReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("GameViewComposited"), HdrVisualization::SceneReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("SceneViewComposited"), HdrVisualization::SceneReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("GameViewVolumetricLight"), HdrVisualization::SceneReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("SceneViewVolumetricLight"), HdrVisualization::SceneReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("FrameDebuggerReplayStep3"), HdrVisualization::SceneReferred);
    EXPECT_EQ(HdrVisualizationForTextureName("SkyViewLut"), HdrVisualization::DebugScaled);
    EXPECT_EQ(HdrVisualizationForTextureName(""), HdrVisualization::DebugScaled);
    EXPECT_EQ(HdrVisualizationForTextureName("GameViewX"), HdrVisualization::DebugScaled);
}

} // namespace
} // namespace gte::Encoding
