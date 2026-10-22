// Tier-1 tests for the atomic file writer, the temp sweep and the metadata-only *.gta read.

#include "Assets/AtomicFile.h"
#include "Assets/GtaFile.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

class AtomicFileTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteAtomicFileTest_") + info->test_suite_name() + "_" + info->name());
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    static bool Write(const std::filesystem::path& path, const std::string& text, const FileIdentity* expected = nullptr,
        std::string* error = nullptr)
    {
        const std::array<ConstByteSpan, 1> parts = { std::as_bytes(std::span<const char>(text)) };
        return WriteFileAtomically(path, parts, expected, error);
    }

    static std::string Read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::size_t FileCount() const
    {
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(m_root)) {
            (void)entry;
            ++count;
        }
        return count;
    }

    void Touch(const std::string& name, std::chrono::hours age) const
    {
        std::ofstream(m_root / name, std::ios::binary) << "x";
        std::filesystem::last_write_time(m_root / name, std::filesystem::file_time_type::clock::now() - age);
    }

    std::filesystem::path m_root;
};

TEST_F(AtomicFileTest, SecondWriteReplacesFirstAndLeavesNoTemp)
{
    ASSERT_TRUE(Write(m_root / "a.gta", "first"));
    ASSERT_TRUE(Write(m_root / "a.gta", "second"));
    EXPECT_EQ(Read(m_root / "a.gta"), "second");
    EXPECT_EQ(FileCount(), 1u);
}

TEST_F(AtomicFileTest, IdentityMismatchKeepsOldBytesAndLeavesNoTemp)
{
    ASSERT_TRUE(Write(m_root / "a.gta", "first"));
    const std::optional<FileIdentity> identity = ReadFileIdentity(m_root / "a.gta");
    ASSERT_TRUE(identity.has_value());

    ASSERT_TRUE(Write(m_root / "a.gta", "changed by someone else"));

    std::string error;
    EXPECT_FALSE(Write(m_root / "a.gta", "late", &*identity, &error));
    EXPECT_EQ(error, "changed on disk");
    EXPECT_EQ(Read(m_root / "a.gta"), "changed by someone else");
    EXPECT_EQ(FileCount(), 1u);

    const std::optional<FileIdentity> fresh = ReadFileIdentity(m_root / "a.gta");
    EXPECT_TRUE(Write(m_root / "a.gta", "ok", &*fresh));
    EXPECT_EQ(Read(m_root / "a.gta"), "ok");
}

TEST_F(AtomicFileTest, ClaimedTargetIsBusyAlsoWhenCaseDiffers)
{
    EXPECT_EQ(WriterRegistryKey(m_root / "A.gta"), WriterRegistryKey(m_root / "a.GTA"));

    const std::optional<WriterClaim> claim = WriterClaim::TryAcquire(m_root / "A.gta");
    ASSERT_TRUE(claim.has_value());
    EXPECT_FALSE(WriterClaim::TryAcquire(m_root / "a.gta").has_value());

    std::string error;
    EXPECT_FALSE(Write(m_root / "a.GTA", "data", nullptr, &error));
    EXPECT_EQ(error, "busy");
    EXPECT_EQ(FileCount(), 0u);
}

TEST_F(AtomicFileTest, ClaimIsReleasedOnDestruction)
{
    {
        const std::optional<WriterClaim> claim = WriterClaim::TryAcquire(m_root / "a.gta");
        ASSERT_TRUE(claim.has_value());
        EXPECT_TRUE(WriterClaim::IsClaimed(m_root / "a.gta"));
    }
    EXPECT_FALSE(WriterClaim::IsClaimed(m_root / "a.gta"));
    EXPECT_TRUE(Write(m_root / "a.gta", "data"));
}

TEST_F(AtomicFileTest, FreeSpaceCheckKeepsTheMargin)
{
    EXPECT_TRUE(FitsInFreeSpace(100, 100 + kAtomicWriteSpaceMargin));
    EXPECT_FALSE(FitsInFreeSpace(100, 100 + kAtomicWriteSpaceMargin - 1));
    EXPECT_FALSE(FitsInFreeSpace(200, 100));
    EXPECT_FALSE(FitsInFreeSpace(UINT64_MAX, 100));
}

TEST_F(AtomicFileTest, TempNamePatternIsExact)
{
    std::string target;
    EXPECT_TRUE(IsOwnTempFileName("mesh.gta.12.3.tmp", &target));
    EXPECT_EQ(target, "mesh.gta");
    EXPECT_TRUE(IsOwnTempFileName("Sponza.import.stamp.1.2.tmp", &target));
    EXPECT_EQ(target, "Sponza.import.stamp");
    EXPECT_FALSE(IsOwnTempFileName("notes.1.2.tmp"));
    EXPECT_FALSE(IsOwnTempFileName("a.gta.tmp"));
    EXPECT_FALSE(IsOwnTempFileName("a.gta.1.tmp"));
    EXPECT_FALSE(IsOwnTempFileName("a.gta.x.2.tmp"));
    EXPECT_FALSE(IsOwnTempFileName(".gta.1.2.tmp"));
    EXPECT_FALSE(IsOwnTempFileName("a.gta.1.2.tmp.bak"));
}

TEST_F(AtomicFileTest, SweepRemovesOnlyOldOwnTempFilesThatAreNotInFlight)
{
    Touch("old.gta.123.4.tmp", std::chrono::hours(2));
    Touch("old.import.stamp.5.6.tmp", std::chrono::hours(2));
    Touch("fresh.gta.1.2.tmp", std::chrono::hours(0));
    Touch("a.1.2.tmp", std::chrono::hours(2));
    Touch("a.gta.tmp", std::chrono::hours(2));
    Touch("busy.gta.7.8.tmp", std::chrono::hours(2));
    Touch("keep.gta", std::chrono::hours(2));

    const std::optional<WriterClaim> claim = WriterClaim::TryAcquire(m_root / "busy.gta");
    ASSERT_TRUE(claim.has_value());

    EXPECT_EQ(SweepStaleTempFiles(m_root), 2u);
    EXPECT_FALSE(std::filesystem::exists(m_root / "old.gta.123.4.tmp"));
    EXPECT_FALSE(std::filesystem::exists(m_root / "old.import.stamp.5.6.tmp"));
    EXPECT_TRUE(std::filesystem::exists(m_root / "fresh.gta.1.2.tmp"));
    EXPECT_TRUE(std::filesystem::exists(m_root / "a.1.2.tmp"));
    EXPECT_TRUE(std::filesystem::exists(m_root / "a.gta.tmp"));
    EXPECT_TRUE(std::filesystem::exists(m_root / "busy.gta.7.8.tmp"));
    EXPECT_TRUE(std::filesystem::exists(m_root / "keep.gta"));
}

TEST_F(AtomicFileTest, MetadataOnlyMatchesFullRead)
{
    const std::vector<std::uint8_t> metadata = { 9, 8, 7, 6, 5 };
    const std::vector<std::uint8_t> payload = { 1, 2, 3, 4 };
    const Guid guid = Guid::Generate();
    std::string error;
    ASSERT_TRUE(WriteGtaFile(m_root / "m.gta", AssetType::Mesh, guid, AssetFlags::None, metadata, payload,
        kGtaCurrentVersion, &error))
        << error;
    EXPECT_EQ(FileCount(), 1u);

    const auto full = ReadGtaFile(m_root / "m.gta");
    const auto partial = ReadGtaMetadataOnly(m_root / "m.gta");
    ASSERT_TRUE(full.has_value());
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(partial->metadata, full->metadata);
    EXPECT_EQ(partial->metadata, metadata);
    EXPECT_EQ(partial->header.payloadOffset, full->header.payloadOffset);
    EXPECT_FALSE(ReadGtaMetadataOnly(m_root / "missing.gta").has_value());
}

} // namespace
} // namespace gte
