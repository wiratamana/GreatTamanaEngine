// Unit tests for src/Assets/Ktx2Decoder.h - DecodeKtx2ToRgba8() round-
// tripping against Ktx2Encoder.h's own EncodeImageBytesToKtx2() output, plus
// its rejection of malformed/empty input. No GPU device/Renderer/ImGui
// involved - genuinely Tier 1, always built (src/Assets/ has no
// GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency).

#include "Assets/Ktx2Decoder.h"

#include "Assets/Ktx2Encoder.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Same minimal-BMP builder as Ktx2EncoderTests.cpp/AssetImporterTests.cpp -
// duplicated rather than shared, matching this test suite's existing
// convention of small, self-contained fixtures. A 2x2 image with four
// distinct solid colors (bottom-up BMP order: blue, green / red, white),
// exactly matching stb_image's own decoded top-row-first RGBA8 layout once
// through EncodeImageBytesToKtx2() (whose output this test decodes back).
std::vector<std::uint8_t> BuildMinimal2x2Bmp()
{
    constexpr std::uint32_t width = 2;
    constexpr std::uint32_t height = 2;
    constexpr std::uint32_t rowSizeUnpadded = width * 3;
    constexpr std::uint32_t rowSizePadded = (rowSizeUnpadded + 3) & ~3u;
    constexpr std::uint32_t pixelDataSize = rowSizePadded * height;
    constexpr std::uint32_t pixelDataOffset = 14 + 40;
    constexpr std::uint32_t fileSize = pixelDataOffset + pixelDataSize;

    std::vector<std::uint8_t> bytes;
    auto putU16 = [&](std::uint16_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    };
    auto putU32 = [&](std::uint32_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };

    bytes.push_back('B');
    bytes.push_back('M');
    putU32(fileSize);
    putU32(0);
    putU32(pixelDataOffset);
    putU32(40);
    putU32(width);
    putU32(height);
    putU16(1);
    putU16(24);
    putU32(0);
    putU32(pixelDataSize);
    putU32(0);
    putU32(0);
    putU32(0);
    putU32(0);

    const std::uint8_t bottomRow[] = { 255, 0, 0, 0, 255, 0 }; // BGR blue, BGR green.
    const std::uint8_t topRow[] = { 0, 0, 255, 255, 255, 255 }; // BGR red, BGR white.
    bytes.insert(bytes.end(), bottomRow, bottomRow + sizeof(bottomRow));
    bytes.insert(bytes.end(), rowSizePadded - rowSizeUnpadded, 0);
    bytes.insert(bytes.end(), topRow, topRow + sizeof(topRow));
    bytes.insert(bytes.end(), rowSizePadded - rowSizeUnpadded, 0);
    return bytes;
}

TEST(Ktx2DecoderTest, RoundTripsEncodedPixelsExactly)
{
    const std::optional<Ktx2EncodeResult> encoded = EncodeImageBytesToKtx2(BuildMinimal2x2Bmp());
    ASSERT_TRUE(encoded.has_value());

    const std::optional<Ktx2DecodeResult> decoded = DecodeKtx2ToRgba8(encoded->ktx2Bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, encoded->width);
    EXPECT_EQ(decoded->height, encoded->height);
    ASSERT_EQ(decoded->rgba8Pixels.size(), static_cast<std::size_t>(decoded->width) * decoded->height * 4);

    // Top-row-first RGBA8: row 0 is BMP's top row (red, white), row 1 is
    // BMP's bottom row (blue, green) - stb_image already flips BMP's
    // bottom-up storage on decode, and EncodeImageBytesToKtx2() just
    // passes those pixels straight through.
    const auto pixelAt = [&](int x, int y) {
        const std::size_t i = (static_cast<std::size_t>(y) * decoded->width + static_cast<std::size_t>(x)) * 4;
        return std::array<std::uint8_t, 4>{ decoded->rgba8Pixels[i], decoded->rgba8Pixels[i + 1],
            decoded->rgba8Pixels[i + 2], decoded->rgba8Pixels[i + 3] };
    };

    EXPECT_EQ(pixelAt(0, 0), (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 })); // Red.
    EXPECT_EQ(pixelAt(1, 0), (std::array<std::uint8_t, 4>{ 255, 255, 255, 255 })); // White.
    EXPECT_EQ(pixelAt(0, 1), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 })); // Blue.
    EXPECT_EQ(pixelAt(1, 1), (std::array<std::uint8_t, 4>{ 0, 255, 0, 255 })); // Green.
}

TEST(Ktx2DecoderTest, EmptyBytesFailToDecode)
{
    EXPECT_FALSE(DecodeKtx2ToRgba8({}).has_value());
}

TEST(Ktx2DecoderTest, GarbageBytesFailToDecode)
{
    const std::vector<std::uint8_t> garbage = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    EXPECT_FALSE(DecodeKtx2ToRgba8(garbage).has_value());
}

TEST(Ktx2DecoderTest, TruncatedKtx2FailsToDecode)
{
    const std::optional<Ktx2EncodeResult> encoded = EncodeImageBytesToKtx2(BuildMinimal2x2Bmp());
    ASSERT_TRUE(encoded.has_value());

    std::vector<std::uint8_t> truncated = encoded->ktx2Bytes;
    truncated.resize(truncated.size() / 2);

    EXPECT_FALSE(DecodeKtx2ToRgba8(truncated).has_value());
}

} // namespace
} // namespace gte
