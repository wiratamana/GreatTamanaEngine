// Tier-1 tests for the import stamp file and the removal guard.

#include "Assets/GltfLoader.h"
#include "Assets/ImportStamp.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace gte {
namespace {

class ImportStampTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteImportStampTest_") + info->test_suite_name() + "_" + info->name());
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    void WriteText(const std::filesystem::path& path, const std::string& text) const
    {
        std::ofstream(path, std::ios::binary) << text;
    }

    std::filesystem::path m_root;
};

TEST_F(ImportStampTest, WriteThenReadRoundTrips)
{
    const ImportStamp written{ .importerVersion = 3, .sourceBytes = 123456789012ULL, .materialCount = 25, .textureCount = 68 };
    ASSERT_TRUE(WriteImportStamp(m_root / "a.stamp", written));

    const std::optional<ImportStamp> read = ReadImportStamp(m_root / "a.stamp");
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->importerVersion, 3);
    EXPECT_EQ(read->sourceBytes, 123456789012ULL);
    EXPECT_EQ(read->materialCount, 25u);
    EXPECT_EQ(read->textureCount, 68u);
}

TEST_F(ImportStampTest, WriteReplacesAnExistingStampAndLeavesNoTempFile)
{
    ASSERT_TRUE(WriteImportStamp(m_root / "a.stamp", { .importerVersion = 1, .sourceBytes = 10 }));
    ASSERT_TRUE(WriteImportStamp(m_root / "a.stamp", { .importerVersion = 2, .sourceBytes = 20 }));

    EXPECT_EQ(ReadImportStamp(m_root / "a.stamp")->importerVersion, 2);
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(m_root / "a.stamp.tmp", ec));
}

TEST_F(ImportStampTest, MissingFileGivesNullopt)
{
    EXPECT_FALSE(ReadImportStamp(m_root / "none.stamp").has_value());
}

TEST_F(ImportStampTest, GarbageFileGivesNullopt)
{
    WriteText(m_root / "g.stamp", "this is not a stamp\n\x01\x02 binary junk");
    EXPECT_FALSE(ReadImportStamp(m_root / "g.stamp").has_value());

    WriteText(m_root / "bad.stamp", "importerVersion=abc\nsourceBytes=5\n");
    EXPECT_FALSE(ReadImportStamp(m_root / "bad.stamp").has_value());

    WriteText(m_root / "partial.stamp", "importerVersion=1\n"); // sourceBytes missing.
    EXPECT_FALSE(ReadImportStamp(m_root / "partial.stamp").has_value());
}

TEST_F(ImportStampTest, UnknownKeysAreIgnored)
{
    WriteText(m_root / "u.stamp", "future=yes\r\nimporterVersion=4\r\nsourceBytes=99\r\n");
    const std::optional<ImportStamp> read = ReadImportStamp(m_root / "u.stamp");
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->importerVersion, 4);
    EXPECT_EQ(read->sourceBytes, 99u);
}

TEST(ImportStampCurrentTest, DetectsVersionAndSizeMismatch)
{
    const ImportStamp stamp{ .importerVersion = 2, .sourceBytes = 500 };
    EXPECT_TRUE(IsImportStampCurrent(stamp, 2, 500));
    EXPECT_FALSE(IsImportStampCurrent(stamp, 3, 500)); // Older importer.
    EXPECT_FALSE(IsImportStampCurrent(stamp, 2, 501)); // Source changed.
    EXPECT_FALSE(IsImportStampCurrent(std::nullopt, 2, 500));
}

TEST(ImportStampCurrentTest, StampFromAnOlderGltfImporterIsNotCurrent)
{
    const ImportStamp oldStamp{ .importerVersion = 2, .sourceBytes = 500 };
    EXPECT_GT(kGltfImporterVersion, 2);
    EXPECT_FALSE(IsImportStampCurrent(oldStamp, kGltfImporterVersion, 500));

    const ImportStamp currentStamp{ .importerVersion = kGltfImporterVersion, .sourceBytes = 500 };
    EXPECT_TRUE(IsImportStampCurrent(currentStamp, kGltfImporterVersion, 500));
}

TEST_F(ImportStampTest, RemovalGuardAcceptsOnlyPathsStrictlyInsideTheDirectory)
{
    const std::filesystem::path dir = m_root / "Sponza";
    std::filesystem::create_directories(dir);

    EXPECT_TRUE(IsPathStrictlyInsideDirectory(dir, dir / "Sponza.gta"));
    EXPECT_TRUE(IsPathStrictlyInsideDirectory(dir, dir / "Sponza_Textures" / "x.gta"));
    EXPECT_FALSE(IsPathStrictlyInsideDirectory(dir, dir));
    EXPECT_FALSE(IsPathStrictlyInsideDirectory(dir, m_root));
    EXPECT_FALSE(IsPathStrictlyInsideDirectory(dir, dir / ".." / "Other"));
    EXPECT_FALSE(IsPathStrictlyInsideDirectory(dir, m_root / "SponzaEvil" / "x"));
}

} // namespace
} // namespace gte
