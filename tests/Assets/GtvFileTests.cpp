// Tier-1 tests for the GTV1 baked texel block reader.

#include "Assets/GtvFile.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

void PutU32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
    }
}

std::vector<std::byte> MakeFile(std::uint32_t w, std::uint32_t h, std::uint32_t d, std::uint32_t format,
    std::uint32_t flags, std::size_t dataBytes)
{
    std::vector<std::byte> bytes(kGtvHeaderBytes + dataBytes, std::byte{ 0 });
    std::memcpy(bytes.data(), "GTV1", 4);
    PutU32(bytes, 4, w);
    PutU32(bytes, 8, h);
    PutU32(bytes, 12, d);
    PutU32(bytes, 16, format);
    PutU32(bytes, 20, flags);
    for (std::size_t i = 0; i < dataBytes; ++i) {
        bytes[kGtvHeaderBytes + i] = static_cast<std::byte>(i & 0xFFu);
    }
    return bytes;
}

TEST(GtvFileTest, ParsesAVolume)
{
    const GtvParseResult result = ParseGtv(MakeFile(4, 3, 2, 0, kGtvFlagTileable, 4 * 3 * 2 * 4));
    ASSERT_TRUE(result.Ok());
    EXPECT_EQ(result.image.width, 4u);
    EXPECT_EQ(result.image.height, 3u);
    EXPECT_EQ(result.image.depth, 2u);
    EXPECT_EQ(result.image.format, GtvFormat::Rgba8Unorm);
    EXPECT_TRUE(result.image.Tileable());
    EXPECT_TRUE(result.image.IsVolume());
    ASSERT_EQ(result.image.texels.size(), 96u);
    EXPECT_EQ(result.image.texels[5], 5u);
}

TEST(GtvFileTest, DepthOneIsATexture)
{
    const GtvParseResult result = ParseGtv(MakeFile(8, 8, 1, 1, 0, 64));
    ASSERT_TRUE(result.Ok());
    EXPECT_FALSE(result.image.IsVolume());
    EXPECT_FALSE(result.image.Tileable());
    EXPECT_EQ(result.image.format, GtvFormat::R8Unorm);
    EXPECT_EQ(result.image.texels.size(), 64u);
}

TEST(GtvFileTest, ReadsFieldsLittleEndian)
{
    const GtvParseResult result = ParseGtv(MakeFile(0x0102, 3, 1, 1, 0, std::size_t{ 0x0102 } * 3));
    ASSERT_TRUE(result.Ok());
    EXPECT_EQ(result.image.width, 0x0102u);
}

TEST(GtvFileTest, RejectsShortInput)
{
    const std::vector<std::byte> bytes(kGtvHeaderBytes - 1, std::byte{ 0 });
    EXPECT_EQ(ParseGtv(bytes).error, GtvError::TooSmall);
    EXPECT_EQ(ParseGtv({}).error, GtvError::TooSmall);
}

TEST(GtvFileTest, RejectsBadMagic)
{
    std::vector<std::byte> bytes = MakeFile(2, 2, 1, 1, 0, 4);
    bytes[3] = static_cast<std::byte>('2');
    EXPECT_EQ(ParseGtv(bytes).error, GtvError::BadMagic);
}

TEST(GtvFileTest, RejectsZeroDimensions)
{
    EXPECT_EQ(ParseGtv(MakeFile(0, 2, 1, 1, 0, 0)).error, GtvError::ZeroDimension);
    EXPECT_EQ(ParseGtv(MakeFile(2, 0, 1, 1, 0, 0)).error, GtvError::ZeroDimension);
    EXPECT_EQ(ParseGtv(MakeFile(2, 2, 0, 1, 0, 0)).error, GtvError::ZeroDimension);
}

TEST(GtvFileTest, RejectsAbsurdDimensions)
{
    EXPECT_EQ(ParseGtv(MakeFile(kGtvMaxAxis + 1, 1, 1, 1, 0, 0)).error, GtvError::DimensionTooLarge);
    EXPECT_EQ(ParseGtv(MakeFile(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0)).error, GtvError::DimensionTooLarge);
}

TEST(GtvFileTest, RejectsTexelDataOverTheCap)
{
    // 2048 x 2048 x 2048 x 4 bytes is far over 256 MiB and must be refused before any allocation.
    EXPECT_EQ(ParseGtv(MakeFile(2048, 2048, 2048, 0, 0, 0)).error, GtvError::SizeTooLarge);
}

TEST(GtvFileTest, RejectsUnknownFormat)
{
    EXPECT_EQ(ParseGtv(MakeFile(2, 2, 1, 2, 0, 4)).error, GtvError::UnknownFormat);
    EXPECT_EQ(ParseGtv(MakeFile(2, 2, 1, 0xFFFFFFFFu, 0, 4)).error, GtvError::UnknownFormat);
}

TEST(GtvFileTest, RejectsTruncatedData)
{
    EXPECT_EQ(ParseGtv(MakeFile(4, 4, 4, 0, 0, 4 * 4 * 4 * 4 - 1)).error, GtvError::TruncatedData);
    EXPECT_EQ(ParseGtv(MakeFile(4, 4, 4, 0, 0, 0)).error, GtvError::TruncatedData);
}

TEST(GtvFileTest, RejectsTrailingData)
{
    EXPECT_EQ(ParseGtv(MakeFile(2, 2, 1, 1, 0, 5)).error, GtvError::TrailingData);
}

TEST(GtvFileTest, IgnoresReservedHeaderBytes)
{
    std::vector<std::byte> bytes = MakeFile(2, 2, 1, 1, 0, 4);
    PutU32(bytes, 24, 0xDEADBEEFu);
    PutU32(bytes, 28, 0x12345678u);
    EXPECT_TRUE(ParseGtv(bytes).Ok());
}

TEST(GtvFileTest, TexelBytesPerFormat)
{
    EXPECT_EQ(GtvTexelBytes(GtvFormat::Rgba8Unorm), 4u);
    EXPECT_EQ(GtvTexelBytes(GtvFormat::R8Unorm), 1u);
}

TEST(GtvFileTest, EveryErrorHasAName)
{
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(GtvError::TrailingData); ++i) {
        EXPECT_NE(std::string(GtvErrorName(static_cast<GtvError>(i))), "unknown") << int(i);
    }
}

TEST(GtvFileTest, AssetPathIsRelativeToTheWorkingDirectory)
{
    EXPECT_EQ(EngineAssetPath("clouds/shape.gtvol"), "assets/clouds/shape.gtvol");
}

class GtvFileOnDiskTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir = std::filesystem::temp_directory_path() / "GteGtvFileTest";
        std::filesystem::create_directories(m_dir);
    }
    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }
    std::filesystem::path Write(const std::string& name, const std::vector<std::byte>& bytes) const
    {
        const std::filesystem::path path = m_dir / name;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }
    std::filesystem::path m_dir;
};

TEST_F(GtvFileOnDiskTest, ReadsAWrittenFile)
{
    const std::filesystem::path path = Write("ok.gtvol", MakeFile(4, 4, 4, 0, 1, 256));
    const GtvParseResult result = ReadGtvFile(path);
    ASSERT_TRUE(result.Ok());
    EXPECT_EQ(result.image.texels.size(), 256u);
}

TEST_F(GtvFileOnDiskTest, MissingFileIsReported)
{
    EXPECT_EQ(ReadGtvFile(m_dir / "none.gtvol").error, GtvError::FileUnreadable);
}

TEST_F(GtvFileOnDiskTest, DetectsTruncationAndTrailingBytesOnDisk)
{
    EXPECT_EQ(ReadGtvFile(Write("short.gtvol", MakeFile(4, 4, 4, 0, 0, 255))).error, GtvError::TruncatedData);
    EXPECT_EQ(ReadGtvFile(Write("long.gtvol", MakeFile(4, 4, 4, 0, 0, 257))).error, GtvError::TrailingData);
    EXPECT_EQ(ReadGtvFile(Write("tiny.gtvol", std::vector<std::byte>(10))).error, GtvError::TooSmall);
}

TEST_F(GtvFileOnDiskTest, HugeHeaderIsRefusedWithoutReadingTexels)
{
    EXPECT_EQ(ReadGtvFile(Write("huge.gtvol", MakeFile(2048, 2048, 2048, 0, 0, 0))).error, GtvError::SizeTooLarge);
}

} // namespace
} // namespace gte
