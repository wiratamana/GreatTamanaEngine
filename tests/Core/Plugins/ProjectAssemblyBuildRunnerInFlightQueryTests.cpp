// Tier-1 tests for src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp's new
// IsProjectAssemblyBuildInFlight() addition (editor-core-separation-19
// campaign, On-Engine Project Workflow plan, BIG-STEP 5, "Compile" Menu
// Item, PHASE1/PHASE2). A plain, read-only query over the SAME
// g_inFlightProjects set TryMarkInFlight()/ClearInFlight()
// (ProjectAssemblyBuildRunner.cpp's own anonymous namespace) already guard
// TriggerProjectAssemblyCompile()/TryRunProjectAssemblyBuildSynchronously()
// with - never mutates anything.
//
// Uses the EXACT SAME deterministic `onIdleTick` technique as
// ProjectAssemblyBuildRunnerBackupRestoreTests.cpp's own
// ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive test: a
// real, EXISTING temp directory with no CMakeCache.txt in it, so a real
// child `cmake` process genuinely spawns and fails fast, but takes
// non-zero wall-clock time to even begin executing - guaranteeing
// RunOneBuildTarget()'s own poll loop invokes `onIdleTick` at least once
// while the outer TryRunProjectAssemblyBuildSynchronously() call's own
// TryMarkInFlight() is still held.

#include "Core/Plugins/ProjectAssemblyBuildRunner.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <string>

namespace gte {
namespace {

// A unique-per-test-run temp directory, cleaned up (best-effort) at the end
// of each test via its own destructor - never touches the real, production
// project_assemblies/ folder.
class TempOutputDirectory {
public:
    explicit TempOutputDirectory(const std::string& uniqueSuffix)
        : m_path(std::filesystem::temp_directory_path() / ("gte_inflight_query_tests_" + uniqueSuffix))
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

} // namespace

TEST(ProjectAssemblyBuildRunnerInFlightQueryTest, ReturnsFalseForAProjectNameThatHasNeverBeenTriggered)
{
    EXPECT_FALSE(IsProjectAssemblyBuildInFlight("GteNeverTriggeredProjectXyz"));
}

TEST(ProjectAssemblyBuildRunnerInFlightQueryTest, ReturnsTrueWhileASynchronousBuildIsGenuinelyInFlightForThatExactName)
{
    TempOutputDirectory buildDirectory("ReturnsTrue_BuildDir");
    const std::string projectName = "GteInFlightQueryTestProjectAlpha";

    // Sanity check - never in-flight before this test's own build starts.
    ASSERT_FALSE(IsProjectAssemblyBuildInFlight(projectName));

    bool observedTrueDuringBuild = false;
    const std::function<void()> onIdleTick = [&]() {
        if (IsProjectAssemblyBuildInFlight(projectName)) {
            observedTrueDuringBuild = true;
        }
    };

    BuildOutcome outerOutcome;
    const bool outerResult =
        TryRunProjectAssemblyBuildSynchronously(projectName, buildDirectory.Path().string(), outerOutcome, onIdleTick);

    EXPECT_TRUE(outerResult);
    // No real CMakeCache.txt exists at this path - the build itself must fail.
    EXPECT_FALSE(outerOutcome.success);
    EXPECT_TRUE(observedTrueDuringBuild)
        << "onIdleTick never observed IsProjectAssemblyBuildInFlight(projectName) == true - either "
           "onIdleTick was never invoked (see the sibling BackupRestoreTests file's own comment for "
           "why that should not happen in practice), or the guard genuinely does not reflect an "
           "in-flight synchronous build.";

    // ClearInFlight() must have genuinely run by the time the outer call returns.
    EXPECT_FALSE(IsProjectAssemblyBuildInFlight(projectName));
}

TEST(ProjectAssemblyBuildRunnerInFlightQueryTest, IsIndependentAcrossTwoDifferentProjectNamesSimultaneously)
{
    TempOutputDirectory buildDirectory("Independent_BuildDir");
    const std::string projectName = "GteInFlightQueryTestProjectBeta";
    const std::string otherProjectName = "GteSomeOtherUnrelatedProjectName";

    ASSERT_FALSE(IsProjectAssemblyBuildInFlight(projectName));
    ASSERT_FALSE(IsProjectAssemblyBuildInFlight(otherProjectName));

    bool observedThisProjectTrue = false;
    bool observedOtherProjectFalse = false;
    const std::function<void()> onIdleTick = [&]() {
        if (IsProjectAssemblyBuildInFlight(projectName)) {
            observedThisProjectTrue = true;
        }
        if (!IsProjectAssemblyBuildInFlight(otherProjectName)) {
            observedOtherProjectFalse = true;
        }
    };

    BuildOutcome outerOutcome;
    const bool outerResult =
        TryRunProjectAssemblyBuildSynchronously(projectName, buildDirectory.Path().string(), outerOutcome, onIdleTick);

    EXPECT_TRUE(outerResult);
    EXPECT_TRUE(observedThisProjectTrue);
    EXPECT_TRUE(observedOtherProjectFalse)
        << "IsProjectAssemblyBuildInFlight() incorrectly reported an unrelated project name as "
           "in-flight - this must be a per-project flag, not a single global one.";

    EXPECT_FALSE(IsProjectAssemblyBuildInFlight(projectName));
    EXPECT_FALSE(IsProjectAssemblyBuildInFlight(otherProjectName));
}

} // namespace gte
