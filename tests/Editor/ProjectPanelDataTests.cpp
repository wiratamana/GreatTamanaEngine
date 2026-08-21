// Unit tests for the Editor's "Project" panel data-shaping/filesystem logic
// (src/Editor/ProjectPanelData.h) - ScanProjectDirectory()/
// EnsureProjectRootExists()/ResolveDropTargetDirectory()/
// MakeUniqueDestinationPath()/PathToUtf8()/Utf8ToPath() are deliberately
// free of ImGui/Renderer/live-Vulkan-device/SDL-video knowledge, so they're
// Tier-1-testable exactly like MemoryPanelData (see
// tests/Editor/MemoryPanelDataTests.cpp) despite living under src/Editor/ -
// see AGENTS.md, "Testability & Regression Safety". These DO touch a real
// temp directory (created/torn down by the fixture below), which is still
// "Tier 1" per tests/CMakeLists.txt's own taxonomy: no GPU/SDL window/live
// Vulkan device is needed, just the filesystem. Only built when
// GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are both ON, since
// ProjectPanelData.h/.cpp are only compiled into gte_core then (see the
// root CMakeLists.txt).

#include "Editor/ProjectPanelData.h"

#include <cstddef>
#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

class ProjectPanelDataTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteProjectPanelDataTest_") + info->test_suite_name() + "_" + info->name());

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

// --- PathToUtf8() / Utf8ToPath() --------------------------------------------

TEST(ProjectPanelDataUtf8Test, RoundTripsAsciiName)
{
    const std::string original = "hello_world.txt";
    EXPECT_EQ(PathToUtf8(Utf8ToPath(original)), original);
}

TEST(ProjectPanelDataUtf8Test, RoundTripsNonAsciiName)
{
    // Built via \u escapes (rather than raw non-ASCII source bytes) so this
    // test's meaning doesn't depend on the source file's own encoding -
    // "café_日本語.txt".
    const char8_t kNonAscii[] = u8"caf\u00e9_\u65e5\u672c\u8a9e.txt";
    const std::string original(reinterpret_cast<const char*>(kNonAscii), sizeof(kNonAscii) / sizeof(char8_t) - 1);

    EXPECT_EQ(PathToUtf8(Utf8ToPath(original)), original);
}

// --- EnsureProjectRootExists() ----------------------------------------------

TEST_F(ProjectPanelDataTest, EnsureProjectRootExists_CreatesMissingDirectory)
{
    const std::filesystem::path project = m_root / "Project";
    ASSERT_FALSE(std::filesystem::exists(project));

    EXPECT_TRUE(EnsureProjectRootExists(project));

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::is_directory(project, ec));
}

TEST_F(ProjectPanelDataTest, EnsureProjectRootExists_ReturnsTrueWhenAlreadyExists)
{
    const std::filesystem::path project = m_root / "Project";
    std::filesystem::create_directories(project);

    EXPECT_TRUE(EnsureProjectRootExists(project));
}

TEST_F(ProjectPanelDataTest, EnsureProjectRootExists_ReturnsFalseWhenParentIsActuallyAFile)
{
    const std::filesystem::path blocker = m_root / "blocker";
    WriteFile(blocker, "not a directory");

    const std::filesystem::path project = blocker / "Project"; // Parent ("blocker") is a file, not a directory.
    EXPECT_FALSE(EnsureProjectRootExists(project));
}

// --- ScanProjectDirectory() --------------------------------------------------

TEST_F(ProjectPanelDataTest, ScanProjectDirectory_EmptyForNonExistentRoot)
{
    const std::vector<ProjectEntry> entries = ScanProjectDirectory(m_root / "DoesNotExist");
    EXPECT_TRUE(entries.empty());
}

TEST_F(ProjectPanelDataTest, ScanProjectDirectory_EmptyForAnEmptyDirectory)
{
    const std::vector<ProjectEntry> entries = ScanProjectDirectory(m_root);
    EXPECT_TRUE(entries.empty());
}

TEST_F(ProjectPanelDataTest, ScanProjectDirectory_ListsDirectoriesFirstThenFilesCaseInsensitiveAlphabetical)
{
    WriteFile(m_root / "beta.txt", "b");
    WriteFile(m_root / "alpha.txt", "a");
    std::filesystem::create_directories(m_root / "Beta");
    std::filesystem::create_directories(m_root / "Alpha");

    const std::vector<ProjectEntry> entries = ScanProjectDirectory(m_root);

    ASSERT_EQ(entries.size(), 4u);
    EXPECT_TRUE(entries[0].isDirectory);
    EXPECT_EQ(entries[0].name, "Alpha");
    EXPECT_TRUE(entries[1].isDirectory);
    EXPECT_EQ(entries[1].name, "Beta");
    EXPECT_FALSE(entries[2].isDirectory);
    EXPECT_EQ(entries[2].name, "alpha.txt");
    EXPECT_FALSE(entries[3].isDirectory);
    EXPECT_EQ(entries[3].name, "beta.txt");
}

TEST_F(ProjectPanelDataTest, ScanProjectDirectory_RecursesIntoSubdirectoriesWithSlashJoinedRelativePaths)
{
    std::filesystem::create_directories(m_root / "Sub");
    WriteFile(m_root / "Sub" / "nested.txt", "n");

    const std::vector<ProjectEntry> entries = ScanProjectDirectory(m_root);

    ASSERT_EQ(entries.size(), 1u);
    ASSERT_TRUE(entries[0].isDirectory);
    EXPECT_EQ(entries[0].relativePath, "Sub");
    ASSERT_EQ(entries[0].children.size(), 1u);
    EXPECT_EQ(entries[0].children[0].name, "nested.txt");
    EXPECT_EQ(entries[0].children[0].relativePath, "Sub/nested.txt");
    EXPECT_FALSE(entries[0].children[0].isDirectory);
}

TEST_F(ProjectPanelDataTest, ScanProjectDirectory_ReportsCorrectFileSize)
{
    WriteFile(m_root / "sized.bin", "0123456789"); // Exactly 10 bytes.

    const std::vector<ProjectEntry> entries = ScanProjectDirectory(m_root);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].sizeBytes, 10u);
}

// --- ResolveDropTargetDirectory() -------------------------------------------

TEST_F(ProjectPanelDataTest, ResolveDropTargetDirectory_EmptySelectionReturnsRoot)
{
    EXPECT_EQ(ResolveDropTargetDirectory(m_root, std::string()), m_root);
}

TEST_F(ProjectPanelDataTest, ResolveDropTargetDirectory_DirectorySelectionReturnsThatDirectory)
{
    std::filesystem::create_directories(m_root / "Sub");
    EXPECT_EQ(ResolveDropTargetDirectory(m_root, "Sub"), m_root / "Sub");
}

TEST_F(ProjectPanelDataTest, ResolveDropTargetDirectory_FileSelectionReturnsParentDirectory)
{
    WriteFile(m_root / "file.txt", "x");
    EXPECT_EQ(ResolveDropTargetDirectory(m_root, "file.txt"), m_root);
}

TEST_F(ProjectPanelDataTest, ResolveDropTargetDirectory_StaleSelectionFallsBackGracefullyRatherThanThrowing)
{
    // Nothing named "Ghost" actually exists - the selection is stale (e.g.
    // deleted the instant after being selected). Must still resolve to
    // somewhere sensible under m_root, never throw.
    const std::filesystem::path resolved = ResolveDropTargetDirectory(m_root, "Ghost");
    EXPECT_EQ(resolved, m_root);
}

// --- MakeUniqueDestinationPath() --------------------------------------------

TEST_F(ProjectPanelDataTest, MakeUniqueDestinationPath_ReturnsDesiredNameWhenFree)
{
    EXPECT_EQ(MakeUniqueDestinationPath(m_root, "fresh.txt"), m_root / "fresh.txt");
}

TEST_F(ProjectPanelDataTest, MakeUniqueDestinationPath_AppendsNumericSuffixWhenNameTaken)
{
    WriteFile(m_root / "taken.txt", "x");
    EXPECT_EQ(MakeUniqueDestinationPath(m_root, "taken.txt"), m_root / "taken (1).txt");
}

TEST_F(ProjectPanelDataTest, MakeUniqueDestinationPath_IncrementsSuffixUntilFree)
{
    WriteFile(m_root / "taken.txt", "x");
    WriteFile(m_root / "taken (1).txt", "x");
    WriteFile(m_root / "taken (2).txt", "x");

    EXPECT_EQ(MakeUniqueDestinationPath(m_root, "taken.txt"), m_root / "taken (3).txt");
}

TEST_F(ProjectPanelDataTest, MakeUniqueDestinationPath_WorksForExtensionlessNamesLikeAFolder)
{
    std::filesystem::create_directories(m_root / "New Folder");
    EXPECT_EQ(MakeUniqueDestinationPath(m_root, "New Folder"), m_root / "New Folder (1)");
}

// --- FindEntryByRelativePath() ----------------------------------------------

TEST_F(ProjectPanelDataTest, FindEntryByRelativePath_EmptyPathReturnsNullptr)
{
    const std::vector<ProjectEntry> tree = ScanProjectDirectory(m_root);
    EXPECT_EQ(FindEntryByRelativePath(tree, std::string()), nullptr);
}

TEST_F(ProjectPanelDataTest, FindEntryByRelativePath_FindsTopLevelEntry)
{
    WriteFile(m_root / "top.txt", "x");
    const std::vector<ProjectEntry> tree = ScanProjectDirectory(m_root);

    const ProjectEntry* found = FindEntryByRelativePath(tree, "top.txt");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "top.txt");
}

TEST_F(ProjectPanelDataTest, FindEntryByRelativePath_FindsDeeplyNestedEntry)
{
    std::filesystem::create_directories(m_root / "A" / "B");
    WriteFile(m_root / "A" / "B" / "nested.txt", "x");
    const std::vector<ProjectEntry> tree = ScanProjectDirectory(m_root);

    const ProjectEntry* found = FindEntryByRelativePath(tree, "A/B/nested.txt");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "nested.txt");
    EXPECT_FALSE(found->isDirectory);
}

TEST_F(ProjectPanelDataTest, FindEntryByRelativePath_NeverConfusesASimilarlyNamedSibling)
{
    std::filesystem::create_directories(m_root / "Sub");
    std::filesystem::create_directories(m_root / "Sub2" / "x");
    const std::vector<ProjectEntry> tree = ScanProjectDirectory(m_root);

    const ProjectEntry* found = FindEntryByRelativePath(tree, "Sub2/x");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->relativePath, "Sub2/x");
}

TEST_F(ProjectPanelDataTest, FindEntryByRelativePath_ReturnsNullptrForSomethingThatDoesNotExist)
{
    const std::vector<ProjectEntry> tree = ScanProjectDirectory(m_root);
    EXPECT_EQ(FindEntryByRelativePath(tree, "Ghost/Nested"), nullptr);
}

// --- ParentRelativePath() ----------------------------------------------------

TEST(ProjectPanelDataParentPathTest, EmptyInputReturnsEmpty)
{
    EXPECT_EQ(ParentRelativePath(""), "");
}

TEST(ProjectPanelDataParentPathTest, TopLevelEntryReturnsEmpty)
{
    EXPECT_EQ(ParentRelativePath("top.txt"), "");
}

TEST(ProjectPanelDataParentPathTest, OneLevelNestedReturnsItsParent)
{
    EXPECT_EQ(ParentRelativePath("Sub/nested.txt"), "Sub");
}

TEST(ProjectPanelDataParentPathTest, MultiLevelNestedReturnsImmediateParentOnly)
{
    EXPECT_EQ(ParentRelativePath("A/B/C.txt"), "A/B");
}

// --- ResolveDropTarget() -----------------------------------------------------

TEST(ProjectPanelDataResolveDropTargetTest, PrefersASpecificFolderZoneOverEitherPaneFallback)
{
    const std::vector<FolderDropZone> zones = {
        FolderDropZone{ "Sub", Rect{ 0.0f, 0.0f, 100.0f, 20.0f } },
    };
    const Rect leftPane{ 0.0f, 0.0f, 200.0f, 400.0f };
    const Rect rightPane{ 200.0f, 0.0f, 200.0f, 400.0f };

    const std::optional<std::string> target = ResolveDropTarget(zones, 10.0f, 10.0f, leftPane, rightPane, "Other");

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, "Sub");
}

TEST(ProjectPanelDataResolveDropTargetTest, FallsBackToCurrentFolderWhenInRightPaneButNotOnARow)
{
    const std::vector<FolderDropZone> zones = {
        FolderDropZone{ "Sub", Rect{ 0.0f, 0.0f, 100.0f, 20.0f } }, // Only exists in the left pane's x-range.
    };
    const Rect leftPane{ 0.0f, 0.0f, 200.0f, 400.0f };
    const Rect rightPane{ 200.0f, 0.0f, 200.0f, 400.0f };

    const std::optional<std::string> target
        = ResolveDropTarget(zones, 250.0f, 100.0f, leftPane, rightPane, "OpenFolder");

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, "OpenFolder");
}

TEST(ProjectPanelDataResolveDropTargetTest, FallsBackToRootWhenInLeftPaneButNotOnARow)
{
    const std::vector<FolderDropZone> zones;
    const Rect leftPane{ 0.0f, 0.0f, 200.0f, 400.0f };
    const Rect rightPane{ 200.0f, 0.0f, 200.0f, 400.0f };

    const std::optional<std::string> target = ResolveDropTarget(zones, 50.0f, 300.0f, leftPane, rightPane, "OpenFolder");

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, ""); // The Project root.
}

TEST(ProjectPanelDataResolveDropTargetTest, ReturnsNulloptWhenOutsideBothPanes)
{
    const std::vector<FolderDropZone> zones;
    const Rect leftPane{ 0.0f, 0.0f, 200.0f, 400.0f };
    const Rect rightPane{ 200.0f, 0.0f, 200.0f, 400.0f };

    const std::optional<std::string> target
        = ResolveDropTarget(zones, 500.0f, 500.0f, leftPane, rightPane, "OpenFolder");

    EXPECT_FALSE(target.has_value());
}

TEST(ProjectPanelDataResolveDropTargetTest, RectBoundaryIsInclusive)
{
    const std::vector<FolderDropZone> zones = {
        FolderDropZone{ "Sub", Rect{ 10.0f, 10.0f, 100.0f, 50.0f } },
    };
    const Rect leftPane{ 0.0f, 0.0f, 1.0f, 1.0f };
    const Rect rightPane{ 0.0f, 0.0f, 1.0f, 1.0f };

    // Exactly on the zone's bottom-right corner.
    const std::optional<std::string> target = ResolveDropTarget(zones, 110.0f, 60.0f, leftPane, rightPane, "");
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, "Sub");
}

} // namespace
} // namespace gte
