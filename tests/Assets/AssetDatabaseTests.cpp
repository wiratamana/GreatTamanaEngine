// Unit tests for src/Assets/AssetDatabase.h - RefreshFromDirectory()'s
// directory scan, ImportAsset()/ImportRawFile()'s write+register behavior,
// and FindByGuid()/FindByPath()/GetAssetsOfType() lookups. Touches a real
// temp directory (created/torn down by the fixture below) but no GPU/SDL/
// ImGui at all - "Tier 1" per tests/CMakeLists.txt's own taxonomy, same as
// Editor/ProjectPanelDataTests.cpp. Always built - src/Assets/ has no
// GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency.

#include "Assets/AssetDatabase.h"

#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

class AssetDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteAssetDatabaseTest_") + info->test_suite_name() + "_" + info->name());

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

    std::filesystem::path m_root;
    AssetDatabase m_db;
};

// --- RefreshFromDirectory() -------------------------------------------------

TEST_F(AssetDatabaseTest, EmptyDirectoryTracksNothing)
{
    EXPECT_EQ(m_db.RefreshFromDirectory(m_root), 0u);
    EXPECT_EQ(m_db.Count(), 0u);
}

TEST_F(AssetDatabaseTest, NonExistentDirectoryTracksNothing)
{
    EXPECT_EQ(m_db.RefreshFromDirectory(m_root / "DoesNotExist"), 0u);
}

TEST_F(AssetDatabaseTest, FindsGtaFilesAcrossNestedSubdirectories)
{
    ASSERT_TRUE(WriteGtaFile(m_root / "top.gta", AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, { 1 }));
    std::filesystem::create_directories(m_root / "Sub");
    ASSERT_TRUE(
        WriteGtaFile(m_root / "Sub" / "nested.gta", AssetType::Mesh, Guid::Generate(), AssetFlags::None, {}, { 2 }));

    EXPECT_EQ(m_db.RefreshFromDirectory(m_root), 2u);
    EXPECT_EQ(m_db.GetAllAssets().size(), 2u);
}

TEST_F(AssetDatabaseTest, IgnoresNonGtaFiles)
{
    WriteFile(m_root / "readme.txt", "not an asset");
    ASSERT_TRUE(WriteGtaFile(m_root / "real.gta", AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    EXPECT_EQ(m_db.RefreshFromDirectory(m_root), 1u);
}

TEST_F(AssetDatabaseTest, SkipsCorruptGtaFilesWithoutFailingTheWholeScan)
{
    WriteFile(m_root / "corrupt.gta", "not a real gta header at all");
    ASSERT_TRUE(WriteGtaFile(m_root / "real.gta", AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    EXPECT_EQ(m_db.RefreshFromDirectory(m_root), 1u);
}

TEST_F(AssetDatabaseTest, RescanFullyReplacesPreviousState)
{
    ASSERT_TRUE(WriteGtaFile(m_root / "a.gta", AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));
    ASSERT_EQ(m_db.RefreshFromDirectory(m_root), 1u);

    std::error_code ec;
    std::filesystem::remove(m_root / "a.gta", ec);
    ASSERT_TRUE(WriteGtaFile(m_root / "b.gta", AssetType::Mesh, Guid::Generate(), AssetFlags::None, {}, {}));

    EXPECT_EQ(m_db.RefreshFromDirectory(m_root), 1u);
    EXPECT_EQ(m_db.GetAllAssets()[0].type, AssetType::Mesh);
}

TEST_F(AssetDatabaseTest, DeduplicatesAssetsSharingTheSameGuid)
{
    const Guid sharedGuid = Guid::Generate();
    ASSERT_TRUE(WriteGtaFile(m_root / "first.gta", AssetType::Texture, sharedGuid, AssetFlags::None, {}, {}));
    ASSERT_TRUE(WriteGtaFile(m_root / "copy.gta", AssetType::Texture, sharedGuid, AssetFlags::None, {}, {}));

    EXPECT_EQ(m_db.RefreshFromDirectory(m_root), 1u);
}

// --- ImportAsset() / ImportRawFile() ----------------------------------------

TEST_F(AssetDatabaseTest, ImportAssetWritesFileAndRegistersItImmediately)
{
    const std::filesystem::path gtaPath = m_root / "imported.gta";
    const std::optional<Guid> guid
        = m_db.ImportAsset(gtaPath, AssetType::Texture, {}, { 1, 2, 3 }, AssetFlags::Compressed);

    ASSERT_TRUE(guid.has_value());
    EXPECT_TRUE(guid->IsValid());

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(gtaPath, ec));
    EXPECT_EQ(m_db.Count(), 1u);

    const AssetRecord* record = m_db.FindByGuid(*guid);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->type, AssetType::Texture);
    EXPECT_TRUE(HasFlag(record->flags, AssetFlags::Compressed));
}

TEST_F(AssetDatabaseTest, ReimportingSamePathReusesTheExistingGuid)
{
    const std::filesystem::path gtaPath = m_root / "imported.gta";
    const std::optional<Guid> firstGuid = m_db.ImportAsset(gtaPath, AssetType::Texture, {}, { 1 });
    ASSERT_TRUE(firstGuid.has_value());

    const std::optional<Guid> secondGuid = m_db.ImportAsset(gtaPath, AssetType::Texture, {}, { 1, 2, 3, 4 });
    ASSERT_TRUE(secondGuid.has_value());

    EXPECT_EQ(*firstGuid, *secondGuid);
}

TEST_F(AssetDatabaseTest, ImportRawFilePassesSourceBytesThroughAsPayload)
{
    const std::filesystem::path sourcePath = m_root / "source.png";
    WriteFile(sourcePath, "fake-png-bytes");

    const std::filesystem::path gtaPath = m_root / "wrapped.gta";
    const std::optional<Guid> guid = m_db.ImportRawFile(sourcePath, gtaPath, AssetType::Texture);
    ASSERT_TRUE(guid.has_value());

    const std::optional<GtaFileData> data = ReadGtaFile(gtaPath);
    ASSERT_TRUE(data.has_value());
    const std::string payloadText(data->payload.begin(), data->payload.end());
    EXPECT_EQ(payloadText, "fake-png-bytes");
}

TEST_F(AssetDatabaseTest, ImportRawFileFailsGracefullyForMissingSource)
{
    const std::optional<Guid> guid
        = m_db.ImportRawFile(m_root / "DoesNotExist.png", m_root / "wrapped.gta", AssetType::Texture);
    EXPECT_FALSE(guid.has_value());
}

// --- FindByGuid() / FindByPath() / GetAssetsOfType() ------------------------

TEST_F(AssetDatabaseTest, FindByGuidReturnsNullptrWhenNotTracked)
{
    EXPECT_EQ(m_db.FindByGuid(Guid::Generate()), nullptr);
}

TEST_F(AssetDatabaseTest, FindByPathReturnsTheMatchingRecord)
{
    const std::filesystem::path gtaPath = m_root / "asset.gta";
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));
    m_db.RefreshFromDirectory(m_root);

    const AssetRecord* record = m_db.FindByPath(gtaPath);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->type, AssetType::Texture);
}

TEST_F(AssetDatabaseTest, FindByPathReturnsNullptrForUntrackedPath)
{
    EXPECT_EQ(m_db.FindByPath(m_root / "Ghost.gta"), nullptr);
}

TEST_F(AssetDatabaseTest, GetAssetsOfTypeFiltersCorrectly)
{
    ASSERT_TRUE(WriteGtaFile(m_root / "tex.gta", AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));
    ASSERT_TRUE(WriteGtaFile(m_root / "mesh.gta", AssetType::Mesh, Guid::Generate(), AssetFlags::None, {}, {}));
    m_db.RefreshFromDirectory(m_root);

    const std::vector<AssetRecord> textures = m_db.GetAssetsOfType(AssetType::Texture);
    ASSERT_EQ(textures.size(), 1u);
    EXPECT_EQ(textures[0].type, AssetType::Texture);
}

TEST_F(AssetDatabaseTest, ClearDropsEveryTrackedRecord)
{
    ASSERT_TRUE(WriteGtaFile(m_root / "asset.gta", AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));
    m_db.RefreshFromDirectory(m_root);
    ASSERT_EQ(m_db.Count(), 1u);

    m_db.Clear();
    EXPECT_EQ(m_db.Count(), 0u);
}

} // namespace
} // namespace gte
