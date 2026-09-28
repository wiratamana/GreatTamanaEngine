// Tier-1 tests for src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp's new
// ResolveProjectAssemblySourceRootDirectory()/RunPlainCMakeReconfigureAndWait()
// additions (editor-core-separation-16 campaign, On-Engine Project Workflow
// plan, BIG-STEP 2, PHASE1 -
// PHASE1_LIVE_SAFE_RECONFIGURE_HELPER_AND_SOURCE_ROOT_RESOLVER.md). Per
// PHASE0_MASTER_STRATEGY.md's own Correction 1: no existing sibling test file
// for ResolveCMakeBuildDirectory() exists to "extend" - this is a genuinely
// new file, mirroring ProjectAssemblyBuildRunnerBackupRestoreTests.cpp's own
// exact TempOutputDirectory/WriteFile() style rather than re-inventing it.

#include "Core/Plugins/ProjectAssemblyBuildRunner.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace gte {
namespace {

// A unique-per-test-run temp directory, cleaned up (best-effort) at the end
// of each test via its own destructor - never touches the real, production
// build/ tree.
class TempOutputDirectory {
public:
    explicit TempOutputDirectory(const std::string& uniqueSuffix)
        : m_path(std::filesystem::temp_directory_path() / ("gte_source_root_tests_" + uniqueSuffix))
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

} // namespace

TEST(ProjectAssemblyBuildRunnerSourceRootTest, ResolvesProjectsFolderFromARealCMakeHomeDirectoryLine)
{
    TempOutputDirectory buildDirectory("SourceRoot_Basic");
    WriteFile(buildDirectory.Path() / "CMakeCache.txt",
        "// some comment\nCMAKE_HOME_DIRECTORY:INTERNAL=C:/fake/repo/root\nOTHER:VALUE=1\n");

    const std::filesystem::path result = ResolveProjectAssemblySourceRootDirectory(buildDirectory.Path());
    EXPECT_EQ(result, std::filesystem::path("C:/fake/repo/root") / "Projects");
}

TEST(ProjectAssemblyBuildRunnerSourceRootTest, ReturnsEmptyPathWhenCMakeCacheIsMissing)
{
    TempOutputDirectory buildDirectory("SourceRoot_NoCache");
    // Deliberately writes no CMakeCache.txt at all.
    EXPECT_TRUE(ResolveProjectAssemblySourceRootDirectory(buildDirectory.Path()).empty());
}

TEST(ProjectAssemblyBuildRunnerSourceRootTest, ReturnsEmptyPathWhenCMakeHomeDirectoryLineIsMissing)
{
    TempOutputDirectory buildDirectory("SourceRoot_NoLine");
    WriteFile(buildDirectory.Path() / "CMakeCache.txt", "// nothing useful here\nSOME_OTHER_VAR:STRING=x\n");
    EXPECT_TRUE(ResolveProjectAssemblySourceRootDirectory(buildDirectory.Path()).empty());
}

TEST(ProjectAssemblyBuildRunnerSourceRootTest, PlainReconfigureFailsCleanlyAgainstANonExistentSourceDirectory)
{
    // A real cmake.exe genuinely runs here (this test needs cmake on PATH,
    // exactly like ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive
    // already requires it), pointed at a source directory that does not
    // exist - proves the false-return path is reachable and clean (no
    // crash, no thrown exception) without needing a real, multi-second
    // successful reconfigure to also be tested here (that is proven live,
    // for real, in PHASE5's own end-to-end verification instead).
    TempOutputDirectory buildDirectory("Reconfigure_BadSource");
    const std::filesystem::path bogusSource = buildDirectory.Path() / "does_not_exist_at_all";
    EXPECT_FALSE(RunPlainCMakeReconfigureAndWait(bogusSource, buildDirectory.Path()));
}

} // namespace gte
