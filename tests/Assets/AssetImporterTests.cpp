// Unit tests for src/Assets/AssetImporter.h - IsImportableAsKtx2Texture()'s
// pure extension gating, and ImportAssetFile()'s PNG/JPG -> KTX2 -> *.gta
// conversion vs. plain-copy-fallback behavior. Touches a real temp
// directory (created/torn down by the fixture below) but no GPU/SDL/ImGui
// at all - "Tier 1" per tests/CMakeLists.txt's own taxonomy. Always built -
// src/Assets/ has no GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency.

#include "Assets/AssetImporter.h"

#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Same minimal-BMP builder as Ktx2EncoderTests.cpp - duplicated rather than
// shared across test files, matching this test suite's existing convention
// of small, self-contained fixtures.
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

    const std::uint8_t bottomRow[] = { 255, 0, 0, 0, 255, 0 };
    const std::uint8_t topRow[] = { 0, 0, 255, 255, 255, 255 };
    bytes.insert(bytes.end(), bottomRow, bottomRow + sizeof(bottomRow));
    bytes.insert(bytes.end(), rowSizePadded - rowSizeUnpadded, 0);
    bytes.insert(bytes.end(), topRow, topRow + sizeof(topRow));
    bytes.insert(bytes.end(), rowSizePadded - rowSizeUnpadded, 0);
    return bytes;
}

class AssetImporterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteAssetImporterTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    static void WriteFile(const std::filesystem::path& path, const std::string& contents)
    {
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }

    static void WriteBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    std::filesystem::path m_root;
    AssetDatabase m_db;
};

// --- IsImportableAsKtx2Texture() --------------------------------------------

TEST(IsImportableAsKtx2TextureTest, RecognizesCommonImageExtensions)
{
    EXPECT_TRUE(IsImportableAsKtx2Texture(".png"));
    EXPECT_TRUE(IsImportableAsKtx2Texture(".jpg"));
    EXPECT_TRUE(IsImportableAsKtx2Texture(".jpeg"));
    EXPECT_TRUE(IsImportableAsKtx2Texture(".bmp"));
}

TEST(IsImportableAsKtx2TextureTest, RejectsNonImageExtensions)
{
    EXPECT_FALSE(IsImportableAsKtx2Texture(".txt"));
    EXPECT_FALSE(IsImportableAsKtx2Texture(".obj"));
    EXPECT_FALSE(IsImportableAsKtx2Texture(".gta"));
    EXPECT_FALSE(IsImportableAsKtx2Texture(""));
}

// --- ImportAssetFile() ------------------------------------------------------

TEST_F(AssetImporterTest, ConvertsAValidImageToKtx2WrappedGta)
{
    const std::filesystem::path source = m_root / "photo.bmp";
    WriteBinaryFile(source, BuildMinimal2x2Bmp());

    const AssetImportResult result = ImportAssetFile(m_db, source, m_root / "Imported" / "photo.bmp");

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.convertedToKtx2);
    EXPECT_EQ(result.finalPath.extension(), ".gta");
    EXPECT_TRUE(result.guid.IsValid());

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(result.finalPath, ec));
    EXPECT_FALSE(std::filesystem::exists(m_root / "Imported" / "photo.bmp", ec)); // No plain-copy byproduct left behind.
}

TEST_F(AssetImporterTest, ConvertedAssetIsImmediatelyTrackedByTheDatabase)
{
    const std::filesystem::path source = m_root / "photo.bmp";
    WriteBinaryFile(source, BuildMinimal2x2Bmp());

    const AssetImportResult result = ImportAssetFile(m_db, source, m_root / "photo.bmp");
    ASSERT_TRUE(result.success);

    const AssetRecord* record = m_db.FindByGuid(result.guid);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->type, AssetType::Texture);
}

TEST_F(AssetImporterTest, NonImageExtensionIsCopiedAsIsWithNoGtaWrapping)
{
    const std::filesystem::path source = m_root / "notes.txt";
    WriteFile(source, "plain text content");

    const AssetImportResult result = ImportAssetFile(m_db, source, m_root / "Imported" / "notes.txt");

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.convertedToKtx2);
    EXPECT_EQ(result.finalPath, m_root / "Imported" / "notes.txt");
    EXPECT_FALSE(result.guid.IsValid());
    EXPECT_EQ(m_db.Count(), 0u); // Nothing gets registered in the AssetDatabase for a plain copy.

    std::ifstream in(result.finalPath, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "plain text content");
}

TEST_F(AssetImporterTest, CorruptImageExtensionFallsBackToPlainCopy)
{
    const std::filesystem::path source = m_root / "fake.png"; // Named like an image, but not really one.
    WriteFile(source, "this is not a real PNG file");

    const std::filesystem::path destination = m_root / "Imported" / "fake.png";
    const AssetImportResult result = ImportAssetFile(m_db, source, destination);

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.convertedToKtx2);
    EXPECT_EQ(result.finalPath, destination);

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(result.finalPath, ec));
}

TEST_F(AssetImporterTest, FailsGracefullyWhenSourceDoesNotExist)
{
    const AssetImportResult result = ImportAssetFile(m_db, m_root / "DoesNotExist.txt", m_root / "dest.txt");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
}

TEST_F(AssetImporterTest, CreatesMissingDestinationDirectoriesForAPlainCopy)
{
    const std::filesystem::path source = m_root / "notes.txt";
    WriteFile(source, "x");

    const AssetImportResult result = ImportAssetFile(m_db, source, m_root / "Nested" / "Deeper" / "notes.txt");
    ASSERT_TRUE(result.success);

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(result.finalPath, ec));
}

} // namespace
} // namespace gte
