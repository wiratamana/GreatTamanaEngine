#include "Editor/OverlayRaster.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

constexpr int kSize = 10;

OverlaySegment2D Line(float ax, float ay, float bx, float by, std::array<std::uint8_t, 4> rgba = { 255, 0, 0, 255 })
{
    OverlaySegment2D s;
    s.a = Vec2(ax, ay);
    s.b = Vec2(bx, by);
    s.rgba = rgba;
    return s;
}

bool IsRed(const std::vector<std::uint8_t>& image, int x, int y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 4;
    return image[i] == 255 && image[i + 1] == 0 && image[i + 2] == 0 && image[i + 3] == 255;
}

int CountRed(const std::vector<std::uint8_t>& image)
{
    int count = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            count += IsRed(image, x, y) ? 1 : 0;
        }
    }
    return count;
}

TEST(OverlayRasterTest, HorizontalSegmentHitsItsRow)
{
    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    const std::array<OverlaySegment2D, 1> segments{ Line(2.5f, 3.5f, 7.5f, 3.5f) };
    RasterizeSegments(image, kSize, kSize, segments);
    for (int x = 2; x <= 7; ++x) {
        EXPECT_TRUE(IsRed(image, x, 3)) << x;
    }
    EXPECT_EQ(CountRed(image), 6);
}

TEST(OverlayRasterTest, VerticalSegmentHitsItsColumn)
{
    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    const std::array<OverlaySegment2D, 1> segments{ Line(4.5f, 1.5f, 4.5f, 6.5f) };
    RasterizeSegments(image, kSize, kSize, segments);
    for (int y = 1; y <= 6; ++y) {
        EXPECT_TRUE(IsRed(image, 4, y)) << y;
    }
    EXPECT_EQ(CountRed(image), 6);
}

TEST(OverlayRasterTest, SegmentOutsideChangesNothing)
{
    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    const std::array<OverlaySegment2D, 2> segments{ Line(-20.0f, -5.0f, -1.0f, -5.0f), Line(30.0f, 0.0f, 30.0f, 9.0f) };
    RasterizeSegments(image, kSize, kSize, segments);
    EXPECT_EQ(CountRed(image), 0);
}

TEST(OverlayRasterTest, SegmentCrossingBorderIsClipped)
{
    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    const std::array<OverlaySegment2D, 1> segments{ Line(-1000.0f, 5.5f, 1000.0f, 5.5f) };
    RasterizeSegments(image, kSize, kSize, segments);
    EXPECT_EQ(CountRed(image), kSize);
}

TEST(OverlayRasterTest, HalfAlphaBlendsOverBackground)
{
    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    const std::array<OverlaySegment2D, 1> segments{ Line(1.5f, 1.5f, 1.5f, 1.5f, { 200, 100, 0, 128 }) };
    RasterizeSegments(image, kSize, kSize, segments);
    const std::size_t i = (1 * kSize + 1) * 4;
    EXPECT_NEAR(image[i], 100, 1);
    EXPECT_NEAR(image[i + 1], 50, 1);
}

TEST(OverlayRasterTest, ZeroSizeAndSmallSpanDoNothing)
{
    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    const std::array<OverlaySegment2D, 1> segments{ Line(0.0f, 0.0f, 9.0f, 9.0f) };
    RasterizeSegments(image, 0, kSize, segments);
    RasterizeSegments(image, kSize, 0, segments);
    RasterizeSegments(std::span<std::uint8_t>(image.data(), 16), kSize, kSize, segments);
    EXPECT_EQ(CountRed(image), 0);

    std::vector<std::uint8_t> empty;
    RasterizeSegments(empty, 0, 0, segments);
}

TEST(OverlayRasterTest, QuadFillsItsAreaAndZeroSizeIsNoOp)
{
    OverlayQuad2D quad;
    quad.p[0] = Vec2(2.0f, 2.0f);
    quad.p[1] = Vec2(8.0f, 2.0f);
    quad.p[2] = Vec2(8.0f, 8.0f);
    quad.p[3] = Vec2(2.0f, 8.0f);
    quad.rgba = { 255, 0, 0, 255 };
    const std::array<OverlayQuad2D, 1> quads{ quad };

    std::vector<std::uint8_t> image(kSize * kSize * 4, 0);
    RasterizeQuads(image, kSize, kSize, quads);
    EXPECT_TRUE(IsRed(image, 5, 5));
    EXPECT_FALSE(IsRed(image, 0, 0));
    EXPECT_EQ(CountRed(image), 36);

    std::vector<std::uint8_t> untouched(kSize * kSize * 4, 0);
    RasterizeQuads(untouched, 0, kSize, quads);
    EXPECT_EQ(CountRed(untouched), 0);
}
} // namespace
} // namespace gte
