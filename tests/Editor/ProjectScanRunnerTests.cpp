// Tier-1 tests for src/Editor/ProjectScanRunner.h: job result, throttle and dirty-mark logic.
// Real temp directories; no GPU, window or ImGui. Runs the same with the job system ON or OFF.

#include "Editor/ProjectScanRunner.h"

#include <chrono>
#include <fstream>
#include <optional>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

namespace gte {
namespace {

using Clock = ProjectScanRunner::Clock;

class ProjectScanRunnerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteProjectScanRunnerTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec); // Leftover from a crashed run, if any.
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

    // Polls until the scan finishes; nullopt after 10 s.
    static std::optional<ProjectScanResult> WaitForResult(ProjectScanRunner& runner)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (Clock::now() < deadline) {
            if (std::optional<ProjectScanResult> result = runner.Poll(Clock::now())) {
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return std::nullopt;
    }

    static ProjectScanRequest RequestFor(const std::filesystem::path& root)
    {
        ProjectScanRequest request;
        request.rootPath = root;
        return request;
    }

    std::filesystem::path m_root;
};

TEST_F(ProjectScanRunnerTest, IsDueAtConstruction)
{
    ProjectScanRunner runner;
    EXPECT_TRUE(runner.IsDue(Clock::now()));
}

TEST_F(ProjectScanRunnerTest, BusyAfterStartUntilPolled)
{
    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(m_root)));
    EXPECT_TRUE(runner.Busy());
    EXPECT_FALSE(runner.IsDue(Clock::now()));

    ASSERT_TRUE(WaitForResult(runner).has_value());
    EXPECT_FALSE(runner.Busy());
}

TEST_F(ProjectScanRunnerTest, ResultHasTreeAndRootExists)
{
    WriteFile(m_root / "file.txt", "x");
    std::filesystem::create_directories(m_root / "Sub");

    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(m_root)));
    const std::optional<ProjectScanResult> result = WaitForResult(runner);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->rootExists);
    EXPECT_FALSE(result->failed);
    ASSERT_EQ(result->tree.size(), 2u);
    EXPECT_EQ(result->tree[0].name, "Sub");
    EXPECT_EQ(result->tree[1].name, "file.txt");
}

TEST_F(ProjectScanRunnerTest, MissingParentIsAFileGivesRootMissingNotFailed)
{
    const std::filesystem::path blocker = m_root / "blocker";
    WriteFile(blocker, "not a directory");

    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(blocker / "Project")));
    const std::optional<ProjectScanResult> result = WaitForResult(runner);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->rootExists);
    EXPECT_FALSE(result->failed);
}

TEST_F(ProjectScanRunnerTest, LinkedFolderListsSortedFilesOnly)
{
    const std::filesystem::path linked = m_root / "Linked";
    std::filesystem::create_directories(linked / "Folder");
    WriteFile(linked / "b.txt", "b");
    WriteFile(linked / "a.txt", "a");

    ProjectScanRunner runner;
    ProjectScanRequest request = RequestFor(m_root / "Project");
    request.linkedFolderName = "Demo";
    request.linkedFolderPath = linked;
    ASSERT_TRUE(runner.Start(std::move(request)));
    const std::optional<ProjectScanResult> result = WaitForResult(runner);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->linkedFolderName, "Demo");
    const std::vector<std::string> expected = { "a.txt", "b.txt" };
    EXPECT_EQ(result->linkedFileNames, expected);
}

TEST_F(ProjectScanRunnerTest, EmptyLinkedFolderNameSkipsTheLinkedListing)
{
    const std::filesystem::path linked = m_root / "Linked";
    std::filesystem::create_directories(linked);
    WriteFile(linked / "a.txt", "a");

    ProjectScanRunner runner;
    ProjectScanRequest request = RequestFor(m_root / "Project");
    request.linkedFolderPath = linked; // Name stays empty.
    ASSERT_TRUE(runner.Start(std::move(request)));
    const std::optional<ProjectScanResult> result = WaitForResult(runner);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->linkedFolderName.empty());
    EXPECT_TRUE(result->linkedFileNames.empty());
}

TEST_F(ProjectScanRunnerTest, ThrottleAfterPoll)
{
    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(m_root)));

    std::optional<ProjectScanResult> result;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    const Clock::time_point t = Clock::now();
    while (!result && Clock::now() < deadline) {
        result = runner.Poll(t);
        if (!result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ASSERT_TRUE(result.has_value());

    EXPECT_FALSE(runner.IsDue(t));
    EXPECT_TRUE(runner.IsDue(t + kProjectRescanInterval));
}

TEST_F(ProjectScanRunnerTest, MarkDirtyWhileBusySurvivesThePoll)
{
    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(m_root)));
    runner.MarkDirty();

    std::optional<ProjectScanResult> result;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    const Clock::time_point t = Clock::now();
    while (!result && Clock::now() < deadline) {
        result = runner.Poll(t);
        if (!result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(runner.IsDue(t));
}

TEST_F(ProjectScanRunnerTest, StartWhileBusyReturnsFalse)
{
    const std::filesystem::path first = m_root / "First";
    const std::filesystem::path second = m_root / "Second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    WriteFile(first / "first.txt", "1");
    WriteFile(second / "second.txt", "2");

    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(first)));
    EXPECT_FALSE(runner.Start(RequestFor(second)));

    const std::optional<ProjectScanResult> result = WaitForResult(runner);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->tree.size(), 1u);
    EXPECT_EQ(result->tree[0].name, "first.txt");
}

TEST_F(ProjectScanRunnerTest, PollReturnsResultExactlyOnce)
{
    ProjectScanRunner runner;
    ASSERT_TRUE(runner.Start(RequestFor(m_root)));
    ASSERT_TRUE(WaitForResult(runner).has_value());

    EXPECT_FALSE(runner.Poll(Clock::now()).has_value());
}

TEST_F(ProjectScanRunnerTest, DestroyWithScanInFlightDoesNotHang)
{
    for (int d = 0; d < 20; ++d) {
        const std::filesystem::path dir = m_root / ("dir" + std::to_string(d));
        std::filesystem::create_directories(dir);
        for (int f = 0; f < 10; ++f) {
            WriteFile(dir / ("file" + std::to_string(f) + ".txt"), "x");
        }
    }

    {
        ProjectScanRunner runner;
        ASSERT_TRUE(runner.Start(RequestFor(m_root)));
    } // Destroyed without polling: must cancel and wait.

    SUCCEED();
}

} // namespace
} // namespace gte
