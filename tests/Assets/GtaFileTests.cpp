// Unit tests for src/Assets/GtaFile.h - WriteGtaFile()/ReadGtaHeader()/
// ReadGtaFile() round-tripping the 64-byte common header plus arbitrary
// metadata/payload byte ranges. Touches a real temp directory (created/
// torn down by the fixture below) but no GPU/SDL/ImGui at all - "Tier 1"
// per tests/CMakeLists.txt's own taxonomy, same as
// Editor/ProjectPanelDataTests.cpp. Always built - src/Assets/ has no
// GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency.

#include "Assets/GtaFile.h"

#include <cstddef>
#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

class GtaFileTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteGtaFileTest_") + info->test_suite_name() + "_" + info->name());

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

TEST_F(GtaFileTest, WriteThenReadHeaderRoundTripsAllFields)
{
    const std::filesystem::path path = m_root / "asset.gta";
    const Guid guid = Guid::Generate();

    ASSERT_TRUE(WriteGtaFile(path, AssetType::Texture, guid, AssetFlags::Compressed, {}, { 1, 2, 3 }));

    const std::optional<GtaHeader> header = ReadGtaHeader(path);
    ASSERT_TRUE(header.has_value());
    EXPECT_TRUE(header->IsMagicValid());
    EXPECT_EQ(header->Type(), AssetType::Texture);
    EXPECT_EQ(header->Id(), guid);
    EXPECT_TRUE(HasFlag(header->Flags(), AssetFlags::Compressed));
    EXPECT_EQ(header->version, kGtaCurrentVersion);
}

TEST_F(GtaFileTest, WriteThenReadFullFileRoundTripsMetadataAndPayload)
{
    const std::filesystem::path path = m_root / "asset.gta";
    const Guid guid = Guid::Generate();
    const std::vector<std::uint8_t> metadata = { 'm', 'e', 't', 'a' };
    const std::vector<std::uint8_t> payload = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };

    ASSERT_TRUE(WriteGtaFile(path, AssetType::Mesh, guid, AssetFlags::None, metadata, payload));

    const std::optional<GtaFileData> data = ReadGtaFile(path);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->header.Id(), guid);
    EXPECT_EQ(data->header.Type(), AssetType::Mesh);
    EXPECT_EQ(data->metadata, metadata);
    EXPECT_EQ(data->payload, payload);
}

TEST_F(GtaFileTest, WriteThenReadWithEmptyMetadataAndPayload)
{
    const std::filesystem::path path = m_root / "empty.gta";
    const Guid guid = Guid::Generate();

    ASSERT_TRUE(WriteGtaFile(path, AssetType::Unknown, guid, AssetFlags::None, {}, {}));

    const std::optional<GtaFileData> data = ReadGtaFile(path);
    ASSERT_TRUE(data.has_value());
    EXPECT_TRUE(data->metadata.empty());
    EXPECT_TRUE(data->payload.empty());
    EXPECT_EQ(data->header.payloadOffset, sizeof(GtaHeader));
}

TEST_F(GtaFileTest, WriteCreatesMissingParentDirectories)
{
    const std::filesystem::path path = m_root / "Nested" / "Deeper" / "asset.gta";
    EXPECT_TRUE(WriteGtaFile(path, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, { 1 }));

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(path, ec));
}

TEST_F(GtaFileTest, ReadHeaderReturnsNulloptForNonExistentFile)
{
    EXPECT_FALSE(ReadGtaHeader(m_root / "DoesNotExist.gta").has_value());
}

TEST_F(GtaFileTest, ReadHeaderReturnsNulloptForFileShorterThan64Bytes)
{
    const std::filesystem::path path = m_root / "tooshort.gta";
    std::ofstream out(path, std::ios::binary);
    out << "short";
    out.close();

    EXPECT_FALSE(ReadGtaHeader(path).has_value());
}

TEST_F(GtaFileTest, ReadHeaderReturnsNulloptForBadMagic)
{
    const std::filesystem::path path = m_root / "badmagic.gta";
    std::ofstream out(path, std::ios::binary);
    std::vector<char> junk(64, 'X');
    out.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    out.close();

    EXPECT_FALSE(ReadGtaHeader(path).has_value());
}

TEST_F(GtaFileTest, ReadFullFileReturnsNulloptForCorruptPayloadOffset)
{
    const std::filesystem::path path = m_root / "corrupt.gta";
    ASSERT_TRUE(WriteGtaFile(path, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, { 1, 2, 3 }));

    // Corrupt payloadOffset to point past the end of the file.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        const std::uint64_t bogusOffset = 999999;
        file.seekp(offsetof(GtaHeader, payloadOffset));
        file.write(reinterpret_cast<const char*>(&bogusOffset), sizeof(bogusOffset));
    }

    EXPECT_FALSE(ReadGtaFile(path).has_value());
}

TEST_F(GtaFileTest, GtaHeaderIsExactly64Bytes)
{
    EXPECT_EQ(sizeof(GtaHeader), 64u);
}

} // namespace
} // namespace gte
