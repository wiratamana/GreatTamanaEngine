// Tier 1: FrameProfiler frame bracket, scope log, worker events and history ring.
#include "Profiling/FrameProfiler.h"
#include "Profiling/NameTable.h"
#include "Profiling/ProfilingClock.h"

#include <gtest/gtest.h>

#include <thread>

namespace gte::Profiling {
namespace {

// FrameProfiler is a process-wide singleton, so each test starts from a clean state.
class FrameProfilerTest : public ::testing::Test {
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
        FrameProfiler::Instance().SetDisplayPaused(false);
    }
};

TEST_F(FrameProfilerTest, FreshHistoryIsEmpty)
{
    EXPECT_EQ(FrameProfiler::Instance().HistoryCount(), 0u);
    EXPECT_EQ(FrameProfiler::Instance().CompletedFrameCount(), 0u);
}

TEST_F(FrameProfilerTest, BeginEndFrameProducesOneHistoryEntry)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();

    ASSERT_EQ(profiler.HistoryCount(), 1u);
    EXPECT_EQ(profiler.CompletedFrameCount(), 1u);
    EXPECT_EQ(profiler.HistoryAt(0).frameIndex, 0u);
    EXPECT_GE(profiler.HistoryAt(0).cpuMilliseconds, 0.0);
    EXPECT_GE(profiler.HistoryAt(0).endTicks, profiler.HistoryAt(0).startTicks);
}

TEST_F(FrameProfilerTest, FrameIndicesAreSequential)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    for (int i = 0; i < 3; ++i) {
        profiler.BeginFrame();
        profiler.EndFrame();
    }

    ASSERT_EQ(profiler.HistoryCount(), 3u);
    EXPECT_EQ(profiler.HistoryAt(0).frameIndex, 0u);
    EXPECT_EQ(profiler.HistoryAt(1).frameIndex, 1u);
    EXPECT_EQ(profiler.HistoryAt(2).frameIndex, 2u);
}

TEST_F(FrameProfilerTest, EndFrameWithoutBeginFrameRecordsNothing)
{
    FrameProfiler::Instance().EndFrame();
    EXPECT_EQ(FrameProfiler::Instance().HistoryCount(), 0u);
}

TEST_F(FrameProfilerTest, CaptureOffRecordsNothingAndKeepsTheFrameIndex)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);
    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryCount(), 0u);
    EXPECT_EQ(profiler.CompletedFrameCount(), 0u);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    ASSERT_EQ(profiler.HistoryCount(), 1u);
    EXPECT_EQ(profiler.HistoryAt(0).frameIndex, 0u);
}

TEST_F(FrameProfilerTest, RingKeepsTheNewestFrames)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const std::size_t total = kMaxFrameHistory + 10;
    for (std::size_t i = 0; i < total; ++i) {
        profiler.BeginFrame();
        profiler.EndFrame();
    }

    ASSERT_EQ(profiler.HistoryCount(), kMaxFrameHistory);
    EXPECT_EQ(profiler.HistoryAt(0).frameIndex, total - kMaxFrameHistory);
    EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory - 1).frameIndex, total - 1);
    EXPECT_EQ(profiler.CompletedFrameCount(), total);
}

TEST_F(FrameProfilerTest, ScopeEventsAreStoredWithTheirNameAndKind)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const NameId outer = InternName("ProfilerTestOuter");
    const NameId inner = InternName("ProfilerTestInner");

    profiler.BeginFrame();
    const ScopeStart a = profiler.BeginScope();
    const ScopeStart b = profiler.BeginScope();
    profiler.EndScope(b, inner, ScopeKind::Blocked, ViewSlot::Primary);
    profiler.EndScope(a, outer, ScopeKind::Work, ViewSlot::Shared);
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].name, inner);
    EXPECT_EQ(events[0].depth, 1u);
    EXPECT_EQ(events[0].kind, ScopeKind::Blocked);
    EXPECT_EQ(events[0].view, ViewSlot::Primary);
    EXPECT_EQ(events[1].name, outer);
    EXPECT_EQ(events[1].depth, 0u);
}

TEST_F(FrameProfilerTest, ScopeOutsideAFrameIsIgnored)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const ScopeStart start = profiler.BeginScope();
    EXPECT_EQ(start.token, 0u);
    profiler.EndScope(start, InternName("ProfilerTestOutside"), ScopeKind::Work, ViewSlot::Shared);

    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.LastCompletedFrame().scopes.Size(), 0u);
}

TEST_F(FrameProfilerTest, ScopeThatEndsAfterItsFrameIsCountedAsLate)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    const ScopeStart start = profiler.BeginScope();
    profiler.EndFrame();
    profiler.EndScope(start, InternName("ProfilerTestLate"), ScopeKind::Work, ViewSlot::Shared);

    profiler.BeginFrame();
    profiler.EndFrame();
    ASSERT_EQ(profiler.HistoryCount(), 2u);
    EXPECT_EQ(profiler.HistoryAt(1).lateScopes, 1u);
    EXPECT_EQ(profiler.LastCompletedFrame().scopes.Size(), 0u);
}

TEST_F(FrameProfilerTest, ScopeFinishedOnAnotherThreadIsDroppedAndCounted)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    const ScopeStart start = profiler.BeginScope();
    std::thread other([&] { profiler.EndScope(start, InternName("ProfilerTestForeign"), ScopeKind::Work, ViewSlot::Shared); });
    other.join();
    profiler.EndFrame();

    EXPECT_EQ(profiler.LastCompletedFrame().scopes.Size(), 0u);
    EXPECT_EQ(profiler.LastCompletedFrame().summary.foreignScopes, 1u);
}

TEST_F(FrameProfilerTest, MarkerIsAnInstantEvent)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const NameId name = InternName("ProfilerTestMarker");

    profiler.BeginFrame();
    profiler.RecordMarker(name, ViewSlot::Secondary);
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, ScopeKind::Marker);
    EXPECT_EQ(events[0].view, ViewSlot::Secondary);
    EXPECT_EQ(events[0].startTicks, events[0].endTicks);
}

TEST_F(FrameProfilerTest, WorkerScopesAreDrainedIntoTheFrame)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const NameId name = InternName("ProfilerTestWorkerJob");

    profiler.BeginFrame();
    std::thread worker([&] {
        const std::uint64_t start = GetProfilingPerformanceCounter();
        profiler.RecordWorkerScope(3, name, start, GetProfilingPerformanceCounter());
    });
    worker.join();
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().workers.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].worker, 3u);
    EXPECT_EQ(events[0].name, name);
}

TEST_F(FrameProfilerTest, WorkerScopesAreIgnoredWhileCaptureIsOff)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);
    profiler.RecordWorkerScope(0, InternName("ProfilerTestWorkerOff"), 1, 2);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.LastCompletedFrame().workers.Size(), 0u);
}

TEST_F(FrameProfilerTest, MemorySnapshotBelongsToTheOpenFrameOnly)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    MemorySnapshot snapshot;
    snapshot.status = SampleStatus::Present;
    snapshot.totalBytes = 4096;

    profiler.SetMemorySnapshot(snapshot); // no frame is open
    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryAt(0).memory.status, SampleStatus::Absent);

    profiler.BeginFrame();
    profiler.SetMemorySnapshot(snapshot);
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryAt(1).memory.status, SampleStatus::Present);
    EXPECT_EQ(profiler.HistoryAt(1).memory.totalBytes, 4096u);
}

TEST_F(FrameProfilerTest, DisplayPauseIsIndependentOfCapture)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetDisplayPaused(true);
    EXPECT_TRUE(profiler.IsCaptureEnabled());

    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryCount(), 1u);
    EXPECT_TRUE(profiler.IsDisplayPaused());
}

TEST_F(FrameProfilerTest, ResetForTestingClearsHistoryAndKeepsTheSwitches)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.SetCaptureEnabled(false);

    profiler.ResetForTesting();
    EXPECT_EQ(profiler.HistoryCount(), 0u);
    EXPECT_EQ(profiler.CompletedFrameCount(), 0u);
    EXPECT_FALSE(profiler.IsCaptureEnabled());
}

} // namespace
} // namespace gte::Profiling
