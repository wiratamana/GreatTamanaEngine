#include "Renderer/MipChain.h"
#include "Renderer/SamplerAnisotropy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte {
namespace {

TEST(MipChainTest, FourByFourGivesThreeLevels)
{
    const std::vector<std::uint8_t> pixels(4 * 4 * 4, 10);
    const MipChainRgba8 chain = BuildMipChainRgba8(pixels.data(), 4, 4);
    ASSERT_EQ(chain.levelCount, 3u);
    EXPECT_EQ(chain.levelWidths, (std::vector<std::uint32_t>{ 4, 2, 1 }));
    EXPECT_EQ(chain.levelHeights, (std::vector<std::uint32_t>{ 4, 2, 1 }));
    EXPECT_EQ(chain.pixels.size(), 4u * 4 * 4 + 2 * 2 * 4 + 4);
    EXPECT_EQ(chain.levelByteOffsets, (std::vector<std::uint64_t>{ 0, 64, 80 }));
}

TEST(MipChainTest, BoxFilterAveragesWithRounding)
{
    std::vector<std::uint8_t> pixels(2 * 2 * 4, 0);
    const std::uint8_t values[4] = { 0, 100, 200, 100 };
    for (int texel = 0; texel < 4; ++texel) {
        for (int c = 0; c < 4; ++c) {
            pixels[texel * 4 + c] = values[texel];
        }
    }
    const MipChainRgba8 chain = BuildMipChainRgba8(pixels.data(), 2, 2);
    ASSERT_EQ(chain.levelCount, 2u);
    for (int c = 0; c < 4; ++c) {
        EXPECT_EQ(chain.pixels[chain.levelByteOffsets[1] + c], 100);
    }
}

TEST(MipChainTest, NonPowerOfTwoSizesShrinkToOneByOne)
{
    const std::vector<std::uint8_t> pixels(5 * 3 * 4, 77);
    const MipChainRgba8 chain = BuildMipChainRgba8(pixels.data(), 5, 3);
    ASSERT_EQ(chain.levelCount, 3u);
    EXPECT_EQ(chain.levelWidths, (std::vector<std::uint32_t>{ 5, 2, 1 }));
    EXPECT_EQ(chain.levelHeights, (std::vector<std::uint32_t>{ 3, 1, 1 }));
    EXPECT_EQ(chain.pixels.size(), (5u * 3 + 2 * 1 + 1) * 4);
    for (const std::uint8_t value : chain.pixels) {
        EXPECT_EQ(value, 77); // A uniform image stays uniform at every level.
    }
}

TEST(MipChainTest, SrgbAveragesRgbInLinearLight)
{
    // Black and white texels: linear average 0.5 encodes to sRGB 188, not the plain average 128.
    const std::uint8_t pixels[16] = { 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255 };
    const MipChainRgba8 linear = BuildMipChainRgba8(pixels, 2, 2, 2, MipColorSpace::Linear);
    const MipChainRgba8 srgb = BuildMipChainRgba8(pixels, 2, 2, 2, MipColorSpace::Srgb);
    ASSERT_EQ(srgb.levelCount, 2u);
    const std::uint64_t offset = srgb.levelByteOffsets[1];
    for (int c = 0; c < 3; ++c) {
        EXPECT_EQ(linear.pixels[offset + c], 128);
        EXPECT_NEAR(srgb.pixels[offset + c], 188, 1);
    }
    EXPECT_EQ(srgb.pixels[offset + 3], 128); // Alpha stays a plain average.
}

TEST(MipChainTest, SrgbKeepsUniformImagesUnchanged)
{
    for (const std::uint8_t value : { 0, 1, 13, 77, 128, 254, 255 }) {
        const std::vector<std::uint8_t> pixels(4 * 4 * 4, value);
        const MipChainRgba8 chain = BuildMipChainRgba8(pixels.data(), 4, 4, 3, MipColorSpace::Srgb);
        for (const std::uint8_t out : chain.pixels) {
            EXPECT_NEAR(out, value, 1) << "value " << int(value);
        }
    }
}

TEST(MipChainTest, NullInputOrZeroSizeGivesEmptyChain)
{
    const std::uint8_t pixel[4] = { 1, 2, 3, 4 };
    EXPECT_EQ(BuildMipChainRgba8(nullptr, 4, 4).levelCount, 0u);
    EXPECT_EQ(BuildMipChainRgba8(pixel, 0, 1).levelCount, 0u);
    EXPECT_EQ(BuildMipChainRgba8(pixel, 1, 0).levelCount, 0u);
}

TEST(SamplerAnisotropyTest, CapsToEightAndFallsBackToOne)
{
    EXPECT_FLOAT_EQ(ChooseMaterialAnisotropy(true, 16.0f), 8.0f);
    EXPECT_FLOAT_EQ(ChooseMaterialAnisotropy(true, 4.0f), 4.0f);
    EXPECT_FLOAT_EQ(ChooseMaterialAnisotropy(false, 16.0f), 1.0f);
}

} // namespace
} // namespace gte
