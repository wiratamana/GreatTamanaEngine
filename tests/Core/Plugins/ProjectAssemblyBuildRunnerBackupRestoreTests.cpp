// Tier-1 tests for src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp's new
// BackupProjectAssemblyBinaries()/RestoreProjectAssemblyBinariesFromBackup()
// additions (editor-core-separation-13 campaign, Project Assembly Hot
// Reload plan, BIG-STEP 2, PHASE4 -
// PHASE4_PROJECT_ASSEMBLY_HOST_UNLOAD_GPU_SAFETY_AND_BINARY_BACKUP.md,
// section 3.6). Uses a throwaway temp directory (never the real, production
// project_assemblies/ folder) - `outputDirectory` is a REQUIRED parameter on
// both functions (see this phase's own section 3.4 layering note for why),
// so injecting a temp path here needs no special-casing.

#include "Core/Plugins/ProjectAssemblyBuildRunner.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace gte {
namespace {

// A unique-per-test-run temp directory, cleaned up (best-effort) at the end
// of each test via its own destructor - never touches the real, production
// project_assemblies/ folder.
class TempOutputDirectory {
public:
    explicit TempOutputDirectory(const std::string& uniqueSuffix)
        : m_path(std::filesystem::temp_directory_path() / ("gte_backup_restore_tests_" + uniqueSuffix))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempOutputDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    const std::filesystem::path& Path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

void WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary);
    stream << content;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

} // namespace

TEST(ProjectAssemblyBuildRunnerBackupRestoreTest, BackupCopiesBothGameAndEditorDllsWithIdenticalByteContent)
{
    TempOutputDirectory outputDirectory("Backup_Both");
    WriteFile(outputDirectory.Path() / "Foo_Game.dll", "fake game dll bytes");
    WriteFile(outputDirectory.Path() / "Foo_Editor.dll", "fake editor dll bytes");

    EXPECT_TRUE(BackupProjectAssemblyBinaries("Foo", outputDirectory.Path()));

    const std::filesystem::path backupDirectory = outputDirectory.Path() / ".hotreload_backup";
    ASSERT_TRUE(std::filesystem::exists(backupDirectory / "Foo_Game.dll.bak"));
    ASSERT_TRUE(std::filesystem::exists(backupDirectory / "Foo_Editor.dll.bak"));
    EXPECT_EQ(ReadFile(backupDirectory / "Foo_Game.dll.bak"), "fake game dll bytes");
    EXPECT_EQ(ReadFile(backupDirectory / "Foo_Editor.dll.bak"), "fake editor dll bytes");
}

TEST(ProjectAssemblyBuildRunnerBackupRestoreTest, RestoreBringsBackTheOriginalGameDllAfterItWasDeleted)
{
    TempOutputDirectory outputDirectory("Restore_Game");
    WriteFile(outputDirectory.Path() / "Foo_Game.dll", "original game dll bytes");
    ASSERT_TRUE(BackupProjectAssemblyBinaries("Foo", outputDirectory.Path()));

    // Simulate a failed compile leaving nothing at the real path.
    std::filesystem::remove(outputDirectory.Path() / "Foo_Game.dll");
    ASSERT_FALSE(std::filesystem::exists(outputDirectory.Path() / "Foo_Game.dll"));

    EXPECT_TRUE(RestoreProjectAssemblyBinariesFromBackup("Foo", outputDirectory.Path()));

    ASSERT_TRUE(std::filesystem::exists(outputDirectory.Path() / "Foo_Game.dll"));
    EXPECT_EQ(ReadFile(outputDirectory.Path() / "Foo_Game.dll"), "original game dll bytes");
}

TEST(ProjectAssemblyBuildRunnerBackupRestoreTest, BackupSucceedsWithOnlyGameDllPresentNoEditorDll)
{
    TempOutputDirectory outputDirectory("Backup_GameOnly");
    WriteFile(outputDirectory.Path() / "Foo_Game.dll", "game only bytes");
    // Deliberately no Foo_Editor.dll written - a project with no Editor/
    // sources is a perfectly normal, valid case.

    EXPECT_TRUE(BackupProjectAssemblyBinaries("Foo", outputDirectory.Path()));

    const std::filesystem::path backupDirectory = outputDirectory.Path() / ".hotreload_backup";
    EXPECT_TRUE(std::filesystem::exists(backupDirectory / "Foo_Game.dll.bak"));
    EXPECT_FALSE(std::filesystem::exists(backupDirectory / "Foo_Editor.dll.bak"));
}

TEST(ProjectAssemblyBuildRunnerBackupRestoreTest, BackupFailsWhenGameDllItselfIsMissing)
{
    TempOutputDirectory outputDirectory("Backup_MissingGame");
    // Deliberately writes nothing at all - no Foo_Game.dll exists, so this
    // is not a valid, currently-loaded Project Assembly to be backing up.

    EXPECT_FALSE(BackupProjectAssemblyBinaries("Foo", outputDirectory.Path()));
}

TEST(ProjectAssemblyBuildRunnerBackupRestoreTest, RestoreFailsForAProjectWithNoExistingBackup)
{
    TempOutputDirectory outputDirectory("Restore_NoBackup");
    // No BackupProjectAssemblyBinaries() call ever made for this project in
    // this output directory - RestoreProjectAssemblyBinariesFromBackup()
    // must fail cleanly, never crash.

    EXPECT_FALSE(RestoreProjectAssemblyBinariesFromBackup("Foo", outputDirectory.Path()));
}

TEST(ProjectAssemblyBuildRunnerBackupRestoreTest, ResolveProjectAssemblyOutputDirectoryAppendsTheExpectedSubfolder)
{
    const std::filesystem::path executableDirectory = std::filesystem::path("C:") / "some" / "exe" / "dir";
    EXPECT_EQ(ResolveProjectAssemblyOutputDirectory(executableDirectory), executableDirectory / "project_assemblies");
}

} // namespace gte
