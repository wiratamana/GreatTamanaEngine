// Unit tests for PixelConversion (src/Encoding/PixelConversion.h) - a small,
// hand-built 2x2 BGRA buffer with known distinct R/G/B/A values per pixel,
// plus a 0x0/degenerate no-crash case. Pure logic, no GPU involved.

#include "Encoding/PixelConversion.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte::Encoding {
namespace {

TEST(PixelConversionTest, TwoByTwoBuffer_SwapsRedAndBlueOnly)
{
    // 2x2 BGRA pixels, each channel distinct so a wrong swap is obvious.
    // Pixel 0: B=10 G=20 R=30 A=40
    // Pixel 1: B=50 G=60 R=70 A=80
    // Pixel 2: B=90 G=100 R=110 A=120
    // Pixel 3: B=130 G=140 R=150 A=160
    std::vector<std::uint8_t> pixels{
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
        130, 140, 150, 160,
    };

    ConvertBgraToRgbaInPlace(pixels.data(), 2, 2);

    const std::vector<std::uint8_t> expectedRgba{
        30, 20, 10, 40,
        70, 60, 50, 80,
        110, 100, 90, 120,
        150, 140, 130, 160,
    };
    EXPECT_EQ(pixels, expectedRgba);
}

TEST(PixelConversionTest, ZeroSizedBuffer_IsSafeNoOp)
{
    // width == 0
    ConvertBgraToRgbaInPlace(nullptr, 0, 5);
    // height == 0
    ConvertBgraToRgbaInPlace(nullptr, 5, 0);
    // both zero, with a real (unused) non-null pointer to prove it's never
    // dereferenced.
    std::uint8_t dummy = 0xAB;
    ConvertBgraToRgbaInPlace(&dummy, 0, 0);
    EXPECT_EQ(dummy, 0xAB);
}

TEST(PixelConversionTest, NullPixelsWithPositiveDimensions_DoesNotCrash)
{
    ConvertBgraToRgbaInPlace(nullptr, 4, 4);
    SUCCEED();
}

TEST(PixelConversionTest, SinglePixel_SwapsCorrectly)
{
    std::vector<std::uint8_t> pixels{1, 2, 3, 4};
    ConvertBgraToRgbaInPlace(pixels.data(), 1, 1);
    const std::vector<std::uint8_t> expected{3, 2, 1, 4};
    EXPECT_EQ(pixels, expected);
}

} // namespace
} // namespace gte::Encoding
