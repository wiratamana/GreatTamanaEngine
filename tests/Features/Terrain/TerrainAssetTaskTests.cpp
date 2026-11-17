// Background save and load of a terrain asset.

#include "Features/Terrain/TerrainAssetTask.h"

#include <gtest/gtest.h>

#include <chrono>
#include <random>
#include <thread>

namespace gte::terrain {

namespace {

TerrainSettings TaskSettings()
{
    TerrainSettings s;
    s.seed = 77;
    return Sanitize(s);
}

std::filesystem::path TempFile(const char* name)
{
    return std::filesystem::temp_directory_path() / (std::string("gte_task_") + std::to_string(std::random_device{}()) + name);
}

bool WaitIdle(const TerrainAssetTask& task, int seconds = 60)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (task.Busy()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace

TEST(TerrainAssetTaskTest, SaveThenLoadRoundTripsInTheBackground)
{
    const std::filesystem::path file = TempFile("_a.gta");
    TerrainAssetTask task;
    ASSERT_TRUE(task.StartSave(TaskSettings(), SaveRegion{ -1, -1, 3, 2 }, file));
    EXPECT_FALSE(task.StartLoad(file)); // Busy: a second task is refused.
    ASSERT_TRUE(WaitIdle(task));
    AssetTaskStatus saved = task.Status();
    EXPECT_EQ(saved.state, AssetTaskState::Done) << saved.message;
    EXPECT_EQ(saved.kind, AssetTaskKind::Save);
    EXPECT_FLOAT_EQ(saved.progress, 1.0f);
    EXPECT_TRUE(std::filesystem::exists(file));
    task.Reset();
    EXPECT_EQ(task.Status().state, AssetTaskState::Idle);

    ASSERT_TRUE(task.StartLoad(file));
    ASSERT_TRUE(WaitIdle(task));
    EXPECT_EQ(task.Status().state, AssetTaskState::Done);
    const std::optional<BakedTerrain> loaded = task.TakeLoaded();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->countX, 3u);
    EXPECT_EQ(loaded->countZ, 2u);
    EXPECT_EQ(loaded->firstX, -1);
    EXPECT_EQ(loaded->settings.seed, 77u);
    EXPECT_EQ(task.Status().state, AssetTaskState::Idle);
    EXPECT_FALSE(task.TakeLoaded().has_value()); // Moved out once.
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(TerrainAssetTaskTest, ALoadOfAMissingFileFails)
{
    TerrainAssetTask task;
    ASSERT_TRUE(task.StartLoad(TempFile("_missing.gta")));
    ASSERT_TRUE(WaitIdle(task));
    const AssetTaskStatus status = task.Status();
    EXPECT_EQ(status.state, AssetTaskState::Failed);
    EXPECT_FALSE(status.message.empty());
    EXPECT_FALSE(task.TakeLoaded().has_value());
}

TEST(TerrainAssetTaskTest, ABadRegionFailsAndCancelStopsABigBake)
{
    TerrainAssetTask task;
    ASSERT_TRUE(task.StartSave(TaskSettings(), SaveRegion{ 0, 0, 0, 4 }, TempFile("_bad.gta")));
    ASSERT_TRUE(WaitIdle(task));
    EXPECT_EQ(task.Status().state, AssetTaskState::Failed);
    task.Reset();

    const std::filesystem::path file = TempFile("_cancel.gta");
    ASSERT_TRUE(task.StartSave(TaskSettings(), SaveRegion{ 0, 0, 64, 64 }, file));
    task.Cancel();
    ASSERT_TRUE(WaitIdle(task));
    EXPECT_EQ(task.Status().state, AssetTaskState::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(file));
}

TEST(TerrainAssetTaskTest, TheDestructorJoinsARunningTask)
{
    {
        TerrainAssetTask task;
        ASSERT_TRUE(task.StartSave(TaskSettings(), SaveRegion{ 0, 0, 64, 64 }, TempFile("_dtor.gta")));
    } // Cancels and joins: must neither hang nor crash.
    SUCCEED();
}

} // namespace gte::terrain
