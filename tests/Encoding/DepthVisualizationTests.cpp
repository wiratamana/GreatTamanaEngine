// Tier-1 tests for DepthVisualization: hand-built 2x2 depth buffers for each supported format,
// plus unrecognized-format, null-pointer and zero-size cases.

#include "Encoding/DepthVisualization.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace gte::Encoding {
namespace {

// Packs a little-endian 32-bit word into 4 raw bytes, matching how
// vkCmdCopyImageToBuffer would actually lay out a depth-aspect-only copy.
void AppendWord(std::vector<std::uint8_t>& bytes, std::uint32_t word)
{
    std::uint8_t raw[4];
    std::memcpy(raw, &word, 4);
    bytes.insert(bytes.end(), raw, raw + 4);
}

void AppendFloat(std::vector<std::uint8_t>& bytes, float value)
{
    std::uint32_t word = 0;
    std::memcpy(&word, &value, 4);
    AppendWord(bytes, word);
}

TEST(DepthVisualizationTest, D32Sfloat_ConvertsFloatValuesToGrayscale)
{
    std::vector<std::uint8_t> raw;
    AppendFloat(raw, 1.0f); // -> 255
    AppendFloat(raw, 0.0f); // -> 0
    AppendFloat(raw, 0.5f); // -> ~128 (0.5*255+0.5 = 128)
    AppendFloat(raw, 1.0f); // -> 255

    std::vector<std::uint8_t> out(4 * 4, 0xAB);
    ASSERT_TRUE(ConvertDepthToGrayscaleRgba8(raw.data(), PixelFormat::D32_Float, 2, 2, out.data()));

    const std::vector<std::uint8_t> expected{
        255, 255, 255, 255,
        0, 0, 0, 255,
        128, 128, 128, 255,
        255, 255, 255, 255,
    };
    EXPECT_EQ(out, expected);
}

TEST(DepthVisualizationTest, D32SfloatS8Uint_IdenticalToD32SfloatForDepthOnlyCopy)
{
    std::vector<std::uint8_t> raw;
    AppendFloat(raw, 1.0f);
    AppendFloat(raw, 0.0f);
    AppendFloat(raw, 0.25f);
    AppendFloat(raw, 0.75f);

    std::vector<std::uint8_t> out(4 * 4, 0);
    ASSERT_TRUE(ConvertDepthToGrayscaleRgba8(raw.data(), PixelFormat::D32_Float_S8_UInt, 2, 2, out.data()));

    EXPECT_EQ(out[0], 255);
    EXPECT_EQ(out[3], 255); // alpha
    EXPECT_EQ(out[4], 0);
    EXPECT_EQ(out[8], static_cast<std::uint8_t>(0.25f * 255.0f + 0.5f));
    EXPECT_EQ(out[12], static_cast<std::uint8_t>(0.75f * 255.0f + 0.5f));
}

TEST(DepthVisualizationTest, D24UnormS8Uint_UsesLow24BitsIgnoringStencilByte)
{
    std::vector<std::uint8_t> raw;
    AppendWord(raw, 0x00FFFFFFu); // full-scale depth -> 255, top byte (stencil) already 0
    AppendWord(raw, 0x00000000u); // zero depth -> 0
    // Top byte (bits 24-31) must be ignored entirely, even if non-zero -
    // never present in a real depth-aspect-only copy, but this proves the
    // mask is actually applied rather than accidentally reading the whole word.
    AppendWord(raw, 0xFF000000u); // stencil-like garbage in top byte, depth bits all 0 -> 0
    AppendWord(raw, 0x00FFFFFFu);

    std::vector<std::uint8_t> out(4 * 4, 0);
    ASSERT_TRUE(ConvertDepthToGrayscaleRgba8(raw.data(), PixelFormat::D24_UNorm_S8_UInt, 2, 2, out.data()));

    const std::vector<std::uint8_t> expected{
        255, 255, 255, 255,
        0, 0, 0, 255,
        0, 0, 0, 255,
        255, 255, 255, 255,
    };
    EXPECT_EQ(out, expected);
}

TEST(DepthVisualizationTest, UnrecognizedFormat_ReturnsFalseAndLeavesOutputUntouched)
{
    std::vector<std::uint8_t> raw(4, 0);
    std::vector<std::uint8_t> out(4, 0xCD); // sentinel fill
    EXPECT_FALSE(ConvertDepthToGrayscaleRgba8(raw.data(), PixelFormat::RGBA8_UNorm, 1, 1, out.data()));

    const std::vector<std::uint8_t> expectedUntouched(4, 0xCD);
    EXPECT_EQ(out, expectedUntouched);
}

TEST(DepthVisualizationTest, NullRawDepthWithPositiveDimensions_ReturnsFalseAndLeavesOutputUntouched)
{
    std::vector<std::uint8_t> out(4, 0xCD);
    EXPECT_FALSE(ConvertDepthToGrayscaleRgba8(nullptr, PixelFormat::D32_Float, 1, 1, out.data()));

    const std::vector<std::uint8_t> expectedUntouched(4, 0xCD);
    EXPECT_EQ(out, expectedUntouched);
}

TEST(DepthVisualizationTest, NullOutRgba8WithPositiveDimensions_ReturnsFalse)
{
    std::vector<std::uint8_t> raw(4, 0);
    EXPECT_FALSE(ConvertDepthToGrayscaleRgba8(raw.data(), PixelFormat::D32_Float, 1, 1, nullptr));
}

TEST(DepthVisualizationTest, ZeroSizedBuffer_IsSafeNoOpAndReturnsTrue)
{
    EXPECT_TRUE(ConvertDepthToGrayscaleRgba8(nullptr, PixelFormat::D32_Float, 0, 5, nullptr));
    EXPECT_TRUE(ConvertDepthToGrayscaleRgba8(nullptr, PixelFormat::D32_Float, 5, 0, nullptr));

    std::uint8_t dummy = 0xAB;
    EXPECT_TRUE(ConvertDepthToGrayscaleRgba8(&dummy, PixelFormat::D32_Float, 0, 0, &dummy));
    EXPECT_EQ(dummy, 0xAB);
}

} // namespace
} // namespace gte::Encoding
