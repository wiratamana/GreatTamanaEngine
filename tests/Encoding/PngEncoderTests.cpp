// Unit tests for PngEncoder (src/Encoding/PngEncoder.h) - the strongest
// regression test this module can have: encode a small, hand-built RGBA8
// buffer via EncodeRgba8ToPng(), then decode the resulting bytes back via
// the ALREADY-vendored stb_image.h decoder (stbi_load_from_memory()) and
// assert the decoded width/height/pixel bytes are IDENTICAL to the original
// input. This proves the whole encode pipeline produces genuinely valid,
// correctly-shaped PNG data without needing any external tool or golden
// file.
//
// stb_image.h's STB_IMAGE_IMPLEMENTATION is already compiled exactly once,
// unconditionally, into gte_core (see src/Assets/StbImageImpl.cpp) - this
// test binary links gte_core (see tests/CMakeLists.txt), so it can safely
// #include <stb_image.h> here WITHOUT defining STB_IMAGE_IMPLEMENTATION
// itself (defining it again would be an ODR violation).

#include "Encoding/PngEncoder.h"

#include <gtest/gtest.h>

#include <stb_image.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gte::Encoding {
namespace {

TEST(PngEncoderTest, RoundTrip_4x4SolidColorBlocks_DecodesToIdenticalPixels)
{
    const int width = 4;
    const int height = 4;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);

    // Four distinct solid-color 2x2 quadrants, so a channel-order or
    // row-order bug would be obvious.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            const bool leftHalf = x < width / 2;
            const bool topHalf = y < height / 2;
            std::uint8_t r = 0, g = 0, b = 0, a = 255;
            if (topHalf && leftHalf) { r = 255; g = 0; b = 0; }
            else if (topHalf && !leftHalf) { r = 0; g = 255; b = 0; }
            else if (!topHalf && leftHalf) { r = 0; g = 0; b = 255; }
            else { r = 255; g = 255; b = 0; }
            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = a;
        }
    }

    const std::vector<std::uint8_t> png = EncodeRgba8ToPng(rgba.data(), width, height);
    ASSERT_FALSE(png.empty());

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        png.data(),
        static_cast<int>(png.size()),
        &decodedWidth,
        &decodedHeight,
        &decodedChannels,
        4 /* force RGBA */);

    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decodedWidth, width);
    EXPECT_EQ(decodedHeight, height);

    const std::vector<std::uint8_t> decodedBytes(decoded, decoded + (static_cast<std::size_t>(width) * height * 4));
    stbi_image_free(decoded);

    EXPECT_EQ(decodedBytes, rgba);
}

TEST(PngEncoderTest, InvalidDimensions_Throws)
{
    std::vector<std::uint8_t> rgba(16, 0);
    EXPECT_THROW(EncodeRgba8ToPng(rgba.data(), 0, 4), std::runtime_error);
    EXPECT_THROW(EncodeRgba8ToPng(rgba.data(), 4, 0), std::runtime_error);
    EXPECT_THROW(EncodeRgba8ToPng(nullptr, 4, 4), std::runtime_error);
}

TEST(PngEncoderTest, RoundTrip_SinglePixel_ProducesValidPng)
{
    const std::vector<std::uint8_t> rgba{200, 100, 50, 255};
    const std::vector<std::uint8_t> png = EncodeRgba8ToPng(rgba.data(), 1, 1);
    ASSERT_FALSE(png.empty());

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        png.data(), static_cast<int>(png.size()), &decodedWidth, &decodedHeight, &decodedChannels, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decodedWidth, 1);
    EXPECT_EQ(decodedHeight, 1);
    EXPECT_EQ(decoded[0], rgba[0]);
    EXPECT_EQ(decoded[1], rgba[1]);
    EXPECT_EQ(decoded[2], rgba[2]);
    EXPECT_EQ(decoded[3], rgba[3]);
    stbi_image_free(decoded);
}

} // namespace
} // namespace gte::Encoding
