#include "Profiling/FrameProfiler.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte::Profiling {
namespace {

// FrameProfiler::Instance() is a process-wide singleton (see its own class
// comment) - every test here resets it to a known baseline before AND after
// running, mirroring the "capture state BEFORE each test's own calls and
// never assume a pristine baseline" convention AGENTS.md documents for
// SdlMemoryTracker/ImGuiMemoryTracker, just via an outright reset (see
// FrameProfiler::ResetForTesting()'s own doc comment for why that's safe
// here specifically).
class FrameProfilerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
    }

    void TearDown() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
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
    EXPECT_GE(profiler.HistoryAt(0).cpuFrameMilliseconds, 0.0);
    EXPECT_EQ(profiler.HistoryAt(0).frameIndex, 0u);
}

TEST_F(FrameProfilerTest, EndFrameWithoutBeginFrameIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.EndFrame(); // No matching BeginFrame() - must not crash or record anything.
    EXPECT_EQ(profiler.HistoryCount(), 0u);
}

TEST_F(FrameProfilerTest, CpuScopesAggregateByNameFlatly)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.RecordCpuScope("A", 1.0);
    profiler.RecordCpuScope("B", 2.0);
    profiler.RecordCpuScope("A", 1.0);
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    ASSERT_EQ(frame.cpuScopeCount, 2u);

    bool foundA = false;
    bool foundB = false;
    for (std::size_t i = 0; i < frame.cpuScopeCount; ++i) {
        const CpuScopeSample& sample = frame.cpuScopes[i];
        if (std::string(sample.name) == "A") {
            foundA = true;
            EXPECT_DOUBLE_EQ(sample.totalMilliseconds, 2.0);
            EXPECT_EQ(sample.callCount, 2u);
        } else if (std::string(sample.name) == "B") {
            foundB = true;
            EXPECT_DOUBLE_EQ(sample.totalMilliseconds, 2.0);
            EXPECT_EQ(sample.callCount, 1u);
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
}

TEST_F(FrameProfilerTest, CpuScopeOverflowIsDroppedNotCrashed)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    // Distinct, stable-storage names outliving RecordCpuScope() calls (a
    // std::string's c_str() pointer stays valid for as long as the string
    // itself does - here, the whole test function - even though it isn't a
    // literal; see ScopeTimer.h's own comment on why a literal is required
    // for the REAL ScopeTimer/GTE_PROFILE_SCOPE call sites specifically).
    std::vector<std::string> names;
    for (std::size_t i = 0; i < kMaxCpuScopesPerFrame + 8; ++i) {
        names.push_back("Scope" + std::to_string(i));
    }

    profiler.BeginFrame();
    for (const std::string& name : names) {
        profiler.RecordCpuScope(name.c_str(), 1.0);
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.HistoryAt(0).cpuScopeCount, kMaxCpuScopesPerFrame);
}

TEST_F(FrameProfilerTest, RingBufferWrapsAndKeepsMostRecentFrames)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const std::uint64_t totalFrames = static_cast<std::uint64_t>(kMaxFrameHistory) + 5;

    for (std::uint64_t i = 0; i < totalFrames; ++i) {
        profiler.BeginFrame();
        profiler.EndFrame();
    }

    ASSERT_EQ(profiler.HistoryCount(), kMaxFrameHistory);
    EXPECT_EQ(profiler.HistoryAt(0).frameIndex, totalFrames - kMaxFrameHistory);
    EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory - 1).frameIndex, totalFrames - 1);
    EXPECT_EQ(profiler.CompletedFrameCount(), totalFrames);
}

TEST_F(FrameProfilerTest, CaptureDisabledSkipsCapturingEntirely)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);

    profiler.BeginFrame();
    profiler.RecordCpuScope("Whatever", 5.0);
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryCount(), 0u);
    EXPECT_EQ(profiler.CompletedFrameCount(), 0u);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryCount(), 1u);
}

TEST_F(FrameProfilerTest, RecordCpuScopeOutsideFrameBracketIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.RecordCpuScope("Stray", 1.0); // No BeginFrame() yet this test.

    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryAt(0).cpuScopeCount, 0u);
}

TEST_F(FrameProfilerTest, GpuPassAndMemorySamplesDefaultToAbsent)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    for (const GpuPassSample& sample : frame.gpuPasses) {
        EXPECT_EQ(sample.timingStatus, GpuSampleStatus::Absent);
        EXPECT_EQ(sample.countStatus, GpuSampleStatus::Absent);
    }
    EXPECT_EQ(frame.memory.status, GpuSampleStatus::Absent);
}

TEST_F(FrameProfilerTest, SetGpuPassTimingAndDrawStatsAndMemorySnapshotAreRecorded)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassTiming(GpuPass::GameView, GpuSampleStatus::Present, 3.5);
    profiler.SetGpuPassDrawStats(GpuPass::GameView, GpuSampleStatus::Present, 10, 200);
    profiler.SetGpuPassTiming(GpuPass::SceneView, GpuSampleStatus::Unsupported);

    MemorySnapshot memory;
    memory.status = GpuSampleStatus::Present;
    memory.totalBytes = 1024;
    profiler.SetMemorySnapshot(memory);
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);

    const GpuPassSample& gameView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];
    EXPECT_EQ(gameView.timingStatus, GpuSampleStatus::Present);
    EXPECT_DOUBLE_EQ(gameView.milliseconds, 3.5);
    EXPECT_EQ(gameView.countStatus, GpuSampleStatus::Present);
    EXPECT_EQ(gameView.drawCallCount, 10u);
    EXPECT_EQ(gameView.triangleCount, 200u);

    const GpuPassSample& sceneView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::SceneView)];
    EXPECT_EQ(sceneView.timingStatus, GpuSampleStatus::Unsupported);
    // SetGpuPassTiming() on SceneView must never have touched its countStatus.
    EXPECT_EQ(sceneView.countStatus, GpuSampleStatus::Absent);

    EXPECT_EQ(frame.memory.status, GpuSampleStatus::Present);
    EXPECT_EQ(frame.memory.totalBytes, 1024u);
}

TEST_F(FrameProfilerTest, SetGpuPassTimingNeverTouchesCountStatusOrViceVersa)
{
    // Direct regression coverage for the timingStatus/countStatus split
    // itself (see ProfilingTypes.h's own comment, and
    // PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md, Step 2.4/3.6):
    // calling one setter must never have any observable effect on the
    // OTHER tri-state/its sibling fields.
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassTiming(GpuPass::GameView, GpuSampleStatus::Present, 7.0);
    profiler.EndFrame();

    const GpuPassSample& afterTimingOnly = profiler.HistoryAt(0).gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];
    EXPECT_EQ(afterTimingOnly.timingStatus, GpuSampleStatus::Present);
    EXPECT_DOUBLE_EQ(afterTimingOnly.milliseconds, 7.0);
    EXPECT_EQ(afterTimingOnly.countStatus, GpuSampleStatus::Absent);
    EXPECT_EQ(afterTimingOnly.drawCallCount, 0u);
    EXPECT_EQ(afterTimingOnly.triangleCount, 0u);

    profiler.BeginFrame();
    profiler.SetGpuPassDrawStats(GpuPass::SceneView, GpuSampleStatus::Present, 4, 60);
    profiler.EndFrame();

    const GpuPassSample& afterCountOnly = profiler.HistoryAt(1).gpuPasses[static_cast<std::size_t>(GpuPass::SceneView)];
    EXPECT_EQ(afterCountOnly.countStatus, GpuSampleStatus::Present);
    EXPECT_EQ(afterCountOnly.drawCallCount, 4u);
    EXPECT_EQ(afterCountOnly.triangleCount, 60u);
    EXPECT_EQ(afterCountOnly.timingStatus, GpuSampleStatus::Absent);
    EXPECT_DOUBLE_EQ(afterCountOnly.milliseconds, 0.0);
}

// The exact regression test that would have caught this whole phase's own
// most important defect had it existed before Phase 3 was implemented (see
// PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md, Step 3.6): reporting a
// real draw-call/triangle count for a pass must NEVER imply real GPU timing
// data exists for that same pass.
TEST_F(FrameProfilerTest, DrawStatsAloneDoNotImplyRealTimingData)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassDrawStats(GpuPass::GameView, GpuSampleStatus::Present, 12, 400);
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    const GpuPassSample& gameView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];

    EXPECT_EQ(gameView.countStatus, GpuSampleStatus::Present);
    EXPECT_EQ(gameView.drawCallCount, 12u);
    EXPECT_EQ(gameView.triangleCount, 400u);
    EXPECT_EQ(gameView.timingStatus, GpuSampleStatus::Absent);
}

TEST_F(FrameProfilerTest, SetGpuPassTimingOutsideFrameBracketIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetGpuPassTiming(GpuPass::Present, GpuSampleStatus::Present, 1.0); // No BeginFrame() yet.

    profiler.BeginFrame();
    profiler.EndFrame();

    const GpuPassSample& sample = profiler.HistoryAt(0).gpuPasses[static_cast<std::size_t>(GpuPass::Present)];
    EXPECT_EQ(sample.timingStatus, GpuSampleStatus::Absent);
}

TEST_F(FrameProfilerTest, SetGpuPassDrawStatsOutsideFrameBracketIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetGpuPassDrawStats(GpuPass::Present, GpuSampleStatus::Present, 1, 10); // No BeginFrame() yet.

    profiler.BeginFrame();
    profiler.EndFrame();

    const GpuPassSample& sample = profiler.HistoryAt(0).gpuPasses[static_cast<std::size_t>(GpuPass::Present)];
    EXPECT_EQ(sample.countStatus, GpuSampleStatus::Absent);
}

// --- Phase 5 (GPU memory usage over time) - see PHASE5_GPU_MEMORY_HISTORY_
// STRATEGY_v2.md - tests below -----------------------------------------

TEST_F(FrameProfilerTest, SetMemorySnapshotRecordsEveryFieldExactly)
{
    // Unlike SetGpuPassTimingAndDrawStatsAndMemorySnapshotAreRecorded
    // above (which only checks status/totalBytes as part of a broader,
    // multi-setter test), this is a dedicated, exhaustive check of every
    // single MemorySnapshot field at the FrameProfiler storage level. Note
    // this deliberately does NOT go through BuildMemorySnapshot() - that
    // function has its own separate test,
    // tests/Application/MemorySnapshotBuilderTests.cpp - this test is
    // purely about FrameProfiler's own storage/retrieval correctness.
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    MemorySnapshot memory;
    memory.status = GpuSampleStatus::Present;
    memory.totalBytes = 1000;
    memory.bufferBytes = 200;
    memory.textureBytes = 800;
    memory.gpuOnlyBytes = 600;
    memory.cpuOnlyBytes = 150;
    memory.sharedBytes = 250;
    memory.bufferCount = 3;
    memory.textureCount = 5;
    profiler.SetMemorySnapshot(memory);
    profiler.EndFrame();

    const MemorySnapshot& recorded = profiler.HistoryAt(0).memory;
    EXPECT_EQ(recorded.status, GpuSampleStatus::Present);
    EXPECT_EQ(recorded.totalBytes, 1000u);
    EXPECT_EQ(recorded.bufferBytes, 200u);
    EXPECT_EQ(recorded.textureBytes, 800u);
    EXPECT_EQ(recorded.gpuOnlyBytes, 600u);
    EXPECT_EQ(recorded.cpuOnlyBytes, 150u);
    EXPECT_EQ(recorded.sharedBytes, 250u);
    EXPECT_EQ(recorded.bufferCount, 3u);
    EXPECT_EQ(recorded.textureCount, 5u);
}

TEST_F(FrameProfilerTest, SetMemorySnapshotOutsideFrameBracketIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    MemorySnapshot stray;
    stray.status = GpuSampleStatus::Present;
    stray.totalBytes = 999;
    profiler.SetMemorySnapshot(stray); // No BeginFrame() yet this test.

    profiler.BeginFrame();
    profiler.EndFrame();

    const MemorySnapshot& recorded = profiler.HistoryAt(0).memory;
    EXPECT_EQ(recorded.status, GpuSampleStatus::Absent);
    EXPECT_EQ(recorded.totalBytes, 0u);
}

TEST_F(FrameProfilerTest, MemorySnapshotStaysCorrectAcrossMultipleFrames)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    MemorySnapshot first;
    first.status = GpuSampleStatus::Present;
    first.totalBytes = 100;
    first.bufferCount = 1;
    profiler.BeginFrame();
    profiler.SetMemorySnapshot(first);
    profiler.EndFrame();

    MemorySnapshot second;
    second.status = GpuSampleStatus::Present;
    second.totalBytes = 5000;
    second.bufferCount = 9;
    profiler.BeginFrame();
    profiler.SetMemorySnapshot(second);
    profiler.EndFrame();

    ASSERT_EQ(profiler.HistoryCount(), 2u);
    EXPECT_EQ(profiler.HistoryAt(0).memory.totalBytes, 100u);
    EXPECT_EQ(profiler.HistoryAt(0).memory.bufferCount, 1u);
    EXPECT_EQ(profiler.HistoryAt(1).memory.totalBytes, 5000u);
    EXPECT_EQ(profiler.HistoryAt(1).memory.bufferCount, 9u);
}

TEST_F(FrameProfilerTest, MemorySnapshotSurvivesRingBufferWraparound)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const std::uint64_t totalFrames = static_cast<std::uint64_t>(kMaxFrameHistory) + 5;

    for (std::uint64_t i = 0; i < totalFrames; ++i) {
        profiler.BeginFrame();
        MemorySnapshot snapshot;
        snapshot.status = GpuSampleStatus::Present;
        snapshot.totalBytes = i * 10;
        profiler.SetMemorySnapshot(snapshot);
        profiler.EndFrame();
    }

    ASSERT_EQ(profiler.HistoryCount(), kMaxFrameHistory);
    const std::uint64_t oldestRetainedFrameIndex = totalFrames - kMaxFrameHistory;
    EXPECT_EQ(profiler.HistoryAt(0).memory.totalBytes, oldestRetainedFrameIndex * 10);
    EXPECT_EQ(profiler.HistoryAt(0).memory.status, GpuSampleStatus::Present);
    EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory - 1).memory.totalBytes, (totalFrames - 1) * 10);
    EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory - 1).memory.status, GpuSampleStatus::Present);
}

// The single most important test in this whole phase, directly proving
// the "never use 0 bytes to mean 'no data'" rule - see
// PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md, Step 2.4.
TEST_F(FrameProfilerTest, AbsentMemorySnapshotIsDistinctFromRealZeroBytes)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    // Frame 0: SetMemorySnapshot() is never called at all - this is what
    // "no snapshot captured" looks like.
    profiler.BeginFrame();
    profiler.EndFrame();

    // Frame 1: SetMemorySnapshot() IS called, reporting a GENUINELY EMPTY
    // GPU memory total (status = Present, every byte/count field
    // legitimately 0). This is a real, valid, meaningful measurement, not
    // a missing one - see MemorySnapshotBuilderTests.cpp's own
    // AllZeroTotalsStillReportsPresent for the production-code-level
    // equivalent of this same case.
    MemorySnapshot genuinelyEmpty;
    genuinelyEmpty.status = GpuSampleStatus::Present;
    profiler.BeginFrame();
    profiler.SetMemorySnapshot(genuinelyEmpty);
    profiler.EndFrame();

    ASSERT_EQ(profiler.HistoryCount(), 2u);

    const MemorySnapshot& absentFrame = profiler.HistoryAt(0).memory;
    const MemorySnapshot& presentZeroFrame = profiler.HistoryAt(1).memory;

    // Both frames report totalBytes == 0 numerically...
    EXPECT_EQ(absentFrame.totalBytes, 0u);
    EXPECT_EQ(presentZeroFrame.totalBytes, 0u);

    // ...but their STATUS is what actually tells them apart - this is the
    // entire point of the tri-state, and the entire point of this test.
    EXPECT_EQ(absentFrame.status, GpuSampleStatus::Absent);
    EXPECT_EQ(presentZeroFrame.status, GpuSampleStatus::Present);
    EXPECT_NE(absentFrame.status, presentZeroFrame.status);
}

TEST_F(FrameProfilerTest, SetMemorySnapshotDoesNotAffectCpuScopesGpuTimingOrDrawStats)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    profiler.RecordCpuScope("SomeSystem::Update", 4.0);
    profiler.SetGpuPassTiming(GpuPass::GameView, GpuSampleStatus::Present, 2.5);
    profiler.SetGpuPassDrawStats(GpuPass::GameView, GpuSampleStatus::Present, 7, 150);

    MemorySnapshot memory;
    memory.status = GpuSampleStatus::Present;
    memory.totalBytes = 4096;
    profiler.SetMemorySnapshot(memory);

    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);

    EXPECT_EQ(frame.memory.status, GpuSampleStatus::Present);
    EXPECT_EQ(frame.memory.totalBytes, 4096u);

    ASSERT_EQ(frame.cpuScopeCount, 1u);
    EXPECT_EQ(std::string(frame.cpuScopes[0].name), "SomeSystem::Update");
    EXPECT_DOUBLE_EQ(frame.cpuScopes[0].totalMilliseconds, 4.0);

    const GpuPassSample& gameView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];
    EXPECT_EQ(gameView.timingStatus, GpuSampleStatus::Present);
    EXPECT_DOUBLE_EQ(gameView.milliseconds, 2.5);
    EXPECT_EQ(gameView.countStatus, GpuSampleStatus::Present);
    EXPECT_EQ(gameView.drawCallCount, 7u);
    EXPECT_EQ(gameView.triangleCount, 150u);
}

TEST_F(FrameProfilerTest, LastCompletedFrameMatchesMostRecentHistoryEntry)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.BeginFrame();
    profiler.EndFrame();

    EXPECT_EQ(profiler.LastCompletedFrame().frameIndex, profiler.HistoryAt(profiler.HistoryCount() - 1).frameIndex);
    EXPECT_EQ(profiler.LastCompletedFrame().frameIndex, 1u);
}

TEST_F(FrameProfilerTest, LastCompletedFrameOnEmptyHistoryIsSafeDefault)
{
    const FrameSample& frame = FrameProfiler::Instance().LastCompletedFrame();
    EXPECT_EQ(frame.frameIndex, 0u);
    EXPECT_EQ(frame.cpuScopeCount, 0u);
}

TEST_F(FrameProfilerTest, OverrideLastFrameCpuMillisecondsForTestingReplacesOnlyTheMostRecentEntry)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();
    const double firstFrameOriginalCpuMilliseconds = profiler.HistoryAt(0).cpuFrameMilliseconds;

    profiler.BeginFrame();
    profiler.EndFrame();

    profiler.OverrideLastFrameCpuMillisecondsForTesting(12.5);

    ASSERT_EQ(profiler.HistoryCount(), 2u);
    // The FIRST frame's real, measured value must be completely untouched -
    // only the most recently completed entry is ever overridden.
    EXPECT_DOUBLE_EQ(profiler.HistoryAt(0).cpuFrameMilliseconds, firstFrameOriginalCpuMilliseconds);
    EXPECT_DOUBLE_EQ(profiler.HistoryAt(1).cpuFrameMilliseconds, 12.5);
}

TEST_F(FrameProfilerTest, OverrideLastFrameCpuMillisecondsForTestingOnEmptyHistoryIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.OverrideLastFrameCpuMillisecondsForTesting(99.0); // No frame completed yet - must not crash.
    EXPECT_EQ(profiler.HistoryCount(), 0u);
}

} // namespace
} // namespace gte::Profiling
