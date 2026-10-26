// Tier 1: the snapshot says what the newest frame measured and why a number is missing.
#include "Profiling/FrameProfiler.h"
#include "Profiling/NameTable.h"
#include "Profiling/ProfilerSnapshotBuilder.h"

#include <gtest/gtest.h>

#include <string>

namespace gte::Profiling {
namespace {

class ProfilerSnapshotBuilderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
        FrameProfiler::Instance().SetDisplayPaused(false);
    }

    void TearDown() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
    }

    static void RunScope(const char* name, ScopeKind kind = ScopeKind::Work)
    {
        FrameProfiler& profiler = FrameProfiler::Instance();
        const ScopeStart start = profiler.BeginScope();
        profiler.EndScope(start, InternName(name), kind, ViewSlot::Shared);
    }

    static void OneFrame(const GpuFrameMetrics* gpu = nullptr)
    {
        FrameProfiler& profiler = FrameProfiler::Instance();
        profiler.BeginFrame();
        RunScope("SnapshotTestScope");
        if (gpu != nullptr) {
            profiler.SetGpuMetrics(*gpu);
        }
        profiler.EndFrame();
    }
};

TEST_F(ProfilerSnapshotBuilderTest, NoFrameYetMeansEveryMeasuredNumberIsAbsent)
{
    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), SnapshotInputs{});
    EXPECT_FALSE(snapshot.hasFrame);
    EXPECT_EQ(snapshot.cpuFrame.status, SampleStatus::Absent);
    EXPECT_EQ(snapshot.cpuFrame.reason, Reason::NotReady);
    EXPECT_EQ(snapshot.gpuSpan.status, SampleStatus::Absent);
    EXPECT_EQ(snapshot.scopeCount, 0u);
    EXPECT_EQ(snapshot.frameCount, 0u);
    EXPECT_EQ(SnapshotText(snapshot.verdict), "unavailable");
    EXPECT_EQ(snapshot.checks.size(), kCheckCount);
}

TEST_F(ProfilerSnapshotBuilderTest, ACapturedFrameFillsCpuNumbersAndScopeRows)
{
    OneFrame();
    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), SnapshotInputs{});
    EXPECT_TRUE(snapshot.hasFrame);
    EXPECT_EQ(snapshot.cpuFrame.status, SampleStatus::Present);
    EXPECT_EQ(snapshot.untracked.kind, MetricKind::Derived);
    ASSERT_EQ(snapshot.scopeCount, 1u);
    EXPECT_EQ(SnapshotText(snapshot.scopes[0].name), "SnapshotTestScope");
    EXPECT_DOUBLE_EQ(snapshot.scopes[0].calls.value, 1.0);
    EXPECT_LE(snapshot.scopes[0].self.value, snapshot.scopes[0].total.value);
    EXPECT_EQ(snapshot.scopes[0].self.kind, MetricKind::Derived);
}

TEST_F(ProfilerSnapshotBuilderTest, SameNameTopLevelScopesAreSummedAndCounted)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    RunScope("RepeatedScope");
    RunScope("RepeatedScope");
    RunScope("OtherScope");
    profiler.EndFrame();

    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(profiler, SnapshotInputs{});
    ASSERT_EQ(snapshot.scopeCount, 2u);
    for (std::uint32_t i = 0; i < snapshot.scopeCount; ++i) {
        const std::string name(SnapshotText(snapshot.scopes[i].name));
        EXPECT_DOUBLE_EQ(snapshot.scopes[i].calls.value, name == "RepeatedScope" ? 2.0 : 1.0) << name;
    }
}

TEST_F(ProfilerSnapshotBuilderTest, BlockedTimeIsSplitIntoGpuWorkersAndOther)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    RunScope("GpuWait(Offscreen:fence)", ScopeKind::Blocked);
    RunScope("Wait.Workers", ScopeKind::Blocked);
    RunScope("SomethingElse", ScopeKind::Blocked);
    profiler.EndFrame();

    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(profiler, SnapshotInputs{});
    EXPECT_EQ(snapshot.blockedGpu.status, SampleStatus::Present);
    EXPECT_EQ(snapshot.blockedWorkers.status, SampleStatus::Present);
    EXPECT_EQ(snapshot.blockedOther.status, SampleStatus::Present);
    EXPECT_GE(snapshot.blockedGpu.value, 0.0);
}

TEST_F(ProfilerSnapshotBuilderTest, CaptureOffMakesEveryMeasuredNumberAbsentButKeepsTheHistory)
{
    OneFrame();
    FrameProfiler::Instance().SetCaptureEnabled(false);

    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), SnapshotInputs{});
    EXPECT_FALSE(snapshot.captureOn);
    EXPECT_EQ(snapshot.cpuFrame.status, SampleStatus::Absent);
    EXPECT_EQ(snapshot.cpuFrame.reason, Reason::CaptureOff);
    for (const Metric& view : snapshot.gpuView) {
        EXPECT_EQ(view.status, SampleStatus::Absent);
    }
    EXPECT_EQ(snapshot.scopeCount, 0u);
    EXPECT_EQ(snapshot.frameCount, 1u);
    EXPECT_EQ(snapshot.checks[static_cast<std::size_t>(CheckId::CaptureOn)].status, CheckStatus::Warn);
}

TEST_F(ProfilerSnapshotBuilderTest, GpuNumbersKeepTheirStatusPerView)
{
    GpuFrameMetrics gpu;
    gpu.views[static_cast<std::size_t>(ViewSlot::Primary)] = { 2.0f, SampleStatus::Present, 0 };
    gpu.views[static_cast<std::size_t>(ViewSlot::Secondary)] = { 0.0f, SampleStatus::Inactive, 0 };
    gpu.span = { 3.0f, SampleStatus::Present, 0 };
    gpu.indirectDraws[static_cast<std::size_t>(ViewSlot::Primary)] = 9;
    gpu.declaredPasses = 12;
    gpu.executedPasses = 10;
    gpu.droppedFrames = 1;
    OneFrame(&gpu);

    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), SnapshotInputs{});
    const Metric& primary = snapshot.gpuView[static_cast<std::size_t>(ViewSlot::Primary)];
    const Metric& secondary = snapshot.gpuView[static_cast<std::size_t>(ViewSlot::Secondary)];
    EXPECT_EQ(primary.status, SampleStatus::Present);
    EXPECT_DOUBLE_EQ(primary.value, 2.0);
    EXPECT_EQ(secondary.status, SampleStatus::Inactive);
    EXPECT_EQ(secondary.reason, Reason::NoTarget);
    EXPECT_DOUBLE_EQ(snapshot.gpuSpan.value, 3.0);

    EXPECT_EQ(snapshot.indirectDraws[static_cast<std::size_t>(ViewSlot::Primary)].status, SampleStatus::Present);
    EXPECT_DOUBLE_EQ(snapshot.indirectDraws[static_cast<std::size_t>(ViewSlot::Primary)].value, 9.0);
    EXPECT_EQ(snapshot.indirectDraws[static_cast<std::size_t>(ViewSlot::Secondary)].status, SampleStatus::Inactive);
    EXPECT_DOUBLE_EQ(snapshot.gpuCoverage.declared.value, 12.0);
    EXPECT_DOUBLE_EQ(snapshot.drops.droppedGpuFrames.value, 1.0);
}

TEST_F(ProfilerSnapshotBuilderTest, AFrameThatNeverReportedGpuNumbersReadsAbsent)
{
    OneFrame();
    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), SnapshotInputs{});
    EXPECT_EQ(snapshot.gpuSpan.status, SampleStatus::Absent);
    EXPECT_EQ(snapshot.gpuCoverage.declared.status, SampleStatus::Absent);
    EXPECT_EQ(snapshot.drops.droppedGpuFrames.status, SampleStatus::Absent);
}

TEST_F(ProfilerSnapshotBuilderTest, FrameRowsAreOldestFirstAndCarryTheirAge)
{
    for (int i = 0; i < 3; ++i) {
        OneFrame();
    }
    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), SnapshotInputs{});
    ASSERT_EQ(snapshot.frameCount, 3u);
    EXPECT_LT(snapshot.frames[0].frameIndex, snapshot.frames[1].frameIndex);
    EXPECT_LT(snapshot.frames[1].frameIndex, snapshot.frames[2].frameIndex);
    EXPECT_EQ(snapshot.frames[0].cpu.ageFrames, 2u);
    EXPECT_EQ(snapshot.frames[2].cpu.ageFrames, 0u);
}

TEST_F(ProfilerSnapshotBuilderTest, DeviceStateIsCopiedFromTheInputs)
{
    OneFrame();
    SnapshotInputs inputs;
    inputs.deviceLost = true;
    inputs.consecutiveFrameTimeouts = 2;
    inputs.abandonedObjects = 5;
    const ProfilerSnapshot snapshot = BuildProfilerSnapshot(FrameProfiler::Instance(), inputs);
    EXPECT_TRUE(snapshot.device.deviceLost);
    EXPECT_DOUBLE_EQ(snapshot.device.consecutiveFrameTimeouts.value, 2.0);
    EXPECT_DOUBLE_EQ(snapshot.device.abandonedObjects.value, 5.0);
}

TEST_F(ProfilerSnapshotBuilderTest, LiveBlocksComeFromTheTableAtReadTime)
{
    LiveBlockTable table;
    table.Begin("PresentInFlight", "fence", 1000);

    ProfilerSnapshot snapshot;
    FillLiveBlocks(snapshot, table, 4000);
    ASSERT_EQ(snapshot.liveBlockCount, 1u);
    EXPECT_EQ(SnapshotText(snapshot.liveBlocks[0].site), "PresentInFlight");
    EXPECT_EQ(SnapshotText(snapshot.liveBlocks[0].detail), "fence");
    EXPECT_DOUBLE_EQ(snapshot.liveBlocks[0].elapsed.value, 3.0);

    table.End();
    FillLiveBlocks(snapshot, table, 5000);
    EXPECT_EQ(snapshot.liveBlockCount, 0u);
}

TEST(SnapshotTextTest, CopyIsCutToFitAndTerminated)
{
    std::array<char, 6> out{};
    EXPECT_EQ(CopySnapshotText(out, "abcdefghij"), 5u);
    EXPECT_EQ(SnapshotText(out), "abcde");
    EXPECT_EQ(CopySnapshotText(out, ""), 0u);
    EXPECT_EQ(SnapshotText(out), "");
}

} // namespace
} // namespace gte::Profiling
