// Unit tests for the Editor Inspector's asset-metadata logic
// (src/Editor/AssetInspectorData.h) - BuildAssetMetadata()/
// IsSupportedImageExtension() are deliberately free of ImGui/Renderer/
// live-Vulkan-device/SDL knowledge, so they're Tier-1-testable exactly like
// ProjectPanelData (see tests/Editor/ProjectPanelDataTests.cpp) despite
// living under src/Editor/ - see AGENTS.md, "Testability & Regression
// Safety". BuildAssetMetadata() DOES touch a real temp directory (created/
// torn down by the fixture below), which is still "Tier 1" per
// tests/CMakeLists.txt's own taxonomy: no GPU/SDL window/live Vulkan device
// is needed, just the filesystem. Only built when GTE_ENABLE_EDITOR AND
// GTE_ENABLE_PROJECT_PANEL are both ON, since AssetInspectorData.h/.cpp are
// only compiled into gte_core then (see the root CMakeLists.txt).

#include "Editor/AssetInspectorData.h"

#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

class AssetInspectorDataTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteAssetInspectorDataTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec); // Leftover from a previous crashed run, if any.
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
};

// --- BuildAssetMetadata() -----------------------------------------------

TEST_F(AssetInspectorDataTest, ReportsNotExistingForAMissingPath)
{
    const AssetMetadata metadata = BuildAssetMetadata(m_root / "Ghost.txt");
    EXPECT_FALSE(metadata.exists);
}

TEST_F(AssetInspectorDataTest, ReportsFileNameExtensionAndSize)
{
    WriteFile(m_root / "rock.PNG", "0123456789"); // Exactly 10 bytes; uppercase extension on disk.

    const AssetMetadata metadata = BuildAssetMetadata(m_root / "rock.PNG");

    EXPECT_TRUE(metadata.exists);
    EXPECT_FALSE(metadata.isDirectory);
    EXPECT_EQ(metadata.name, "rock.PNG");
    // extension must come back LOWERCASE regardless of the on-disk casing -
    // this is what lets IsSupportedImageExtension() below use a single
    // lowercase allow-list.
    EXPECT_EQ(metadata.extension, ".png");
    EXPECT_EQ(metadata.sizeBytes, 10u);
}

TEST_F(AssetInspectorDataTest, ReportsDirectoryWithoutASizeOrExtension)
{
    std::filesystem::create_directories(m_root / "Sub");

    const AssetMetadata metadata = BuildAssetMetadata(m_root / "Sub");

    EXPECT_TRUE(metadata.exists);
    EXPECT_TRUE(metadata.isDirectory);
    EXPECT_EQ(metadata.name, "Sub");
    EXPECT_EQ(metadata.sizeBytes, 0u);
}

TEST_F(AssetInspectorDataTest, PopulatesAHumanReadableLastWriteTime)
{
    WriteFile(m_root / "fresh.txt", "x");

    const AssetMetadata metadata = BuildAssetMetadata(m_root / "fresh.txt");

    EXPECT_TRUE(metadata.hasLastWriteTime);
    EXPECT_FALSE(metadata.lastWriteTimeText.empty());
}

// --- IsSupportedImageExtension() ----------------------------------------

TEST(IsSupportedImageExtensionTest, AcceptsEveryDocumentedStbImageExtension)
{
    EXPECT_TRUE(IsSupportedImageExtension(".png"));
    EXPECT_TRUE(IsSupportedImageExtension(".jpg"));
    EXPECT_TRUE(IsSupportedImageExtension(".jpeg"));
    EXPECT_TRUE(IsSupportedImageExtension(".bmp"));
    EXPECT_TRUE(IsSupportedImageExtension(".tga"));
    EXPECT_TRUE(IsSupportedImageExtension(".gif"));
    EXPECT_TRUE(IsSupportedImageExtension(".psd"));
    EXPECT_TRUE(IsSupportedImageExtension(".hdr"));
    EXPECT_TRUE(IsSupportedImageExtension(".pic"));
    EXPECT_TRUE(IsSupportedImageExtension(".pnm"));
    EXPECT_TRUE(IsSupportedImageExtension(".ppm"));
    EXPECT_TRUE(IsSupportedImageExtension(".pgm"));
}

TEST(IsSupportedImageExtensionTest, RejectsAnUnsupportedOrEmptyExtension)
{
    EXPECT_FALSE(IsSupportedImageExtension(".txt"));
    EXPECT_FALSE(IsSupportedImageExtension(".obj"));
    EXPECT_FALSE(IsSupportedImageExtension(""));
}

TEST(IsSupportedImageExtensionTest, IsCaseSensitiveAndExpectsAlreadyLowercasedInput)
{
    // BuildAssetMetadata() above always lowercases its own `extension`
    // field before this is ever called with it - this test documents that
    // an UPPERCASE extension passed directly is NOT matched, so a future
    // caller can't skip that normalization step.
    EXPECT_FALSE(IsSupportedImageExtension(".PNG"));
}

} // namespace
} // namespace gte
