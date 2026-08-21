// Unit tests for src/Assets/Ktx2Encoder.h - EncodeImageBytesToKtx2()/
// EncodeImageFileToKtx2() decoding a source image (via stb_image) and
// re-encoding it as an in-memory KTX2 container (via the statically-linked
// libktx). No GPU device/Renderer/ImGui involved anywhere in this path -
// genuinely Tier 1, always built (src/Assets/ has no GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL dependency).

#include "Assets/Ktx2Encoder.h"

#include <algorithm>
#include <cstdint>
#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Hand-builds a minimal, valid, uncompressed 24-bit-per-pixel BMP file (no
// external image-writing library needed - stb_image itself only DECODES,
// it never encodes) - a tiny 2x2 image with four distinct solid colors, one
// per pixel, so a successful round trip can be checked against known exact
// pixel values if ever needed. BMP's bottom-up, byte-aligned-per-row raw
// pixel layout is simple enough to construct by hand reliably, unlike PNG/
// JPEG's own compression.
std::vector<std::uint8_t> BuildMinimal2x2Bmp()
{
    constexpr std::uint32_t width = 2;
    constexpr std::uint32_t height = 2;
    constexpr std::uint32_t rowSizeUnpadded = width * 3; // 24bpp = 3 bytes/pixel.
    constexpr std::uint32_t rowSizePadded = (rowSizeUnpadded + 3) & ~3u; // Rows are padded to a multiple of 4 bytes.
    constexpr std::uint32_t pixelDataSize = rowSizePadded * height;
    constexpr std::uint32_t fileHeaderSize = 14;
    constexpr std::uint32_t infoHeaderSize = 40;
    constexpr std::uint32_t pixelDataOffset = fileHeaderSize + infoHeaderSize;
    constexpr std::uint32_t fileSize = pixelDataOffset + pixelDataSize;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(fileSize);

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
    auto putI32 = [&](std::int32_t v) { putU32(static_cast<std::uint32_t>(v)); };

    // BITMAPFILEHEADER.
    bytes.push_back('B');
    bytes.push_back('M');
    putU32(fileSize);
    putU32(0); // Reserved.
    putU32(pixelDataOffset);

    // BITMAPINFOHEADER.
    putU32(infoHeaderSize);
    putI32(static_cast<std::int32_t>(width));
    putI32(static_cast<std::int32_t>(height)); // Positive = bottom-up row order.
    putU16(1); // Planes.
    putU16(24); // Bits per pixel.
    putU32(0); // BI_RGB, no compression.
    putU32(pixelDataSize);
    putI32(0); // X pixels per meter.
    putI32(0); // Y pixels per meter.
    putU32(0); // Colors used.
    putU32(0); // Colors important.

    // Pixel data, bottom-up, BGR, row-padded. Bottom row first: blue, green.
    // Top row: red, white.
    const std::uint8_t bottomRow[] = { 255, 0, 0, /*BGR blue*/ 0, 255, 0 /*BGR green*/ };
    const std::uint8_t topRow[] = { 0, 0, 255, /*BGR red*/ 255, 255, 255 /*BGR white*/ };

    bytes.insert(bytes.end(), bottomRow, bottomRow + sizeof(bottomRow));
    bytes.insert(bytes.end(), rowSizePadded - rowSizeUnpadded, 0); // Row padding.
    bytes.insert(bytes.end(), topRow, topRow + sizeof(topRow));
    bytes.insert(bytes.end(), rowSizePadded - rowSizeUnpadded, 0); // Row padding.

    return bytes;
}

// The fixed 12-byte KTX2 magic identifier every valid KTX2 file/blob must
// start with (see third_party/ktx/lib/ktxint.h's KTX2_IDENTIFIER_REF) -
// checked here as a cheap sanity check that EncodeImageBytesToKtx2()
// really did produce a KTX2 container, without this test needing to
// reimplement libktx's own (much more thorough) validation.
const std::uint8_t kKtx2Magic[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };

class Ktx2EncoderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteKtx2EncoderTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::filesystem::path m_root;
};

TEST_F(Ktx2EncoderTest, EncodesAValidBmpIntoAKtx2Container)
{
    const std::vector<std::uint8_t> bmpBytes = BuildMinimal2x2Bmp();

    const std::optional<Ktx2EncodeResult> result = EncodeImageBytesToKtx2(bmpBytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width, 2u);
    EXPECT_EQ(result->height, 2u);
    ASSERT_GE(result->ktx2Bytes.size(), sizeof(kKtx2Magic));
    EXPECT_TRUE(std::equal(kKtx2Magic, kKtx2Magic + sizeof(kKtx2Magic), result->ktx2Bytes.begin()));
}

TEST_F(Ktx2EncoderTest, EmptyBytesFailToEncode)
{
    EXPECT_FALSE(EncodeImageBytesToKtx2({}).has_value());
}

TEST_F(Ktx2EncoderTest, GarbageBytesFailToEncode)
{
    const std::vector<std::uint8_t> garbage = { 1, 2, 3, 4, 5, 6, 7, 8 };
    EXPECT_FALSE(EncodeImageBytesToKtx2(garbage).has_value());
}

TEST_F(Ktx2EncoderTest, EncodeImageFileToKtx2ReadsAndEncodesARealFile)
{
    const std::filesystem::path bmpPath = m_root / "source.bmp";
    {
        std::ofstream out(bmpPath, std::ios::binary);
        const std::vector<std::uint8_t> bmpBytes = BuildMinimal2x2Bmp();
        out.write(reinterpret_cast<const char*>(bmpBytes.data()), static_cast<std::streamsize>(bmpBytes.size()));
    }

    const std::optional<Ktx2EncodeResult> result = EncodeImageFileToKtx2(bmpPath);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width, 2u);
    EXPECT_EQ(result->height, 2u);
}

TEST_F(Ktx2EncoderTest, EncodeImageFileToKtx2ReturnsNulloptForMissingFile)
{
    EXPECT_FALSE(EncodeImageFileToKtx2(m_root / "DoesNotExist.bmp").has_value());
}

} // namespace
} // namespace gte
