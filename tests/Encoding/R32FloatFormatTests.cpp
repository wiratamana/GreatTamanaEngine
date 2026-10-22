// Tier-1: R32_Float is handled by the pixel format helpers and the single-channel converter.

#include "Core/PixelFormat.h"
#include "Encoding/CapturedPixelConversion.h"
#include "Encoding/SingleChannelVisualization.h"
#include "Renderer/VulkanFormatInfo.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace gte::Encoding {
namespace {

std::vector<std::uint8_t> PackFloats(const std::vector<float>& values)
{
    std::vector<std::uint8_t> bytes(values.size() * sizeof(float));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}

TEST(R32FloatFormatTest, ToStringIsNamed)
{
    EXPECT_STREQ(ToString(PixelFormat::R32_Float), "R32_Float");
}

TEST(R32FloatFormatTest, VulkanFormatRoundTripsAndHasFourBytesPerTexel)
{
    EXPECT_EQ(ToVkFormat(PixelFormat::R32_Float), VK_FORMAT_R32_SFLOAT);
    EXPECT_EQ(FromVkFormat(VK_FORMAT_R32_SFLOAT), PixelFormat::R32_Float);
    EXPECT_EQ(BytesPerTexelForFormat(VK_FORMAT_R32_SFLOAT), 4);
}

TEST(R32FloatFormatTest, SingleChannelConverterClampsValues)
{
    const std::vector<std::uint8_t> raw =
        PackFloats({ 0.0f, 1.0f, 0.5f, -3.0f, 7.0f, std::numeric_limits<float>::quiet_NaN() });
    std::vector<std::uint8_t> rgba(6 * 4, 0);
    ASSERT_TRUE(Convert1ChannelToGrayscaleRgba8(raw.data(), PixelFormat::R32_Float, 6, 1, rgba.data()));

    const std::uint8_t expected[6] = { 0, 255, 128, 0, 255, 0 };
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(rgba[i * 4 + 0], expected[i]) << i;
        EXPECT_EQ(rgba[i * 4 + 1], expected[i]) << i;
        EXPECT_EQ(rgba[i * 4 + 2], expected[i]) << i;
        EXPECT_EQ(rgba[i * 4 + 3], 255) << i;
    }
}

TEST(R32FloatFormatTest, CapturedConversionTreatsR32AsGrayscale)
{
    CapturedPixels captured;
    captured.format = PixelFormat::R32_Float;
    captured.width = 2;
    captured.height = 1;
    captured.pixels = PackFloats({ 1.0f, 0.0f });

    const std::vector<std::uint8_t> rgba = ConvertCapturedPixelsToRgba8(captured, HdrVisualization::SceneReferred);
    ASSERT_EQ(rgba.size(), 8u);
    EXPECT_EQ(rgba[0], 255);
    EXPECT_EQ(rgba[3], 255);
    EXPECT_EQ(rgba[4], 0);
    EXPECT_EQ(rgba[7], 255);
}

} // namespace
} // namespace gte::Encoding
