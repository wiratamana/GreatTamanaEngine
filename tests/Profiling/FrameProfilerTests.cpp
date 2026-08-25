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
        EXPECT_EQ(sample.status, GpuSampleStatus::Absent);
    }
    EXPECT_EQ(frame.memory.status, GpuSampleStatus::Absent);
}

TEST_F(FrameProfilerTest, SetGpuPassSampleAndMemorySnapshotAreRecorded)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 3.5, 10, 200);
    profiler.SetGpuPassSample(GpuPass::SceneView, GpuSampleStatus::Unsupported);

    MemorySnapshot memory;
    memory.status = GpuSampleStatus::Present;
    memory.totalBytes = 1024;
    profiler.SetMemorySnapshot(memory);
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);

    const GpuPassSample& gameView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];
    EXPECT_EQ(gameView.status, GpuSampleStatus::Present);
    EXPECT_DOUBLE_EQ(gameView.milliseconds, 3.5);
    EXPECT_EQ(gameView.drawCallCount, 10u);
    EXPECT_EQ(gameView.triangleCount, 200u);

    const GpuPassSample& sceneView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::SceneView)];
    EXPECT_EQ(sceneView.status, GpuSampleStatus::Unsupported);

    EXPECT_EQ(frame.memory.status, GpuSampleStatus::Present);
    EXPECT_EQ(frame.memory.totalBytes, 1024u);
}

TEST_F(FrameProfilerTest, GpuPassSampleOutsideFrameBracketIsNoOp)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetGpuPassSample(GpuPass::Present, GpuSampleStatus::Present, 1.0); // No BeginFrame() yet.

    profiler.BeginFrame();
    profiler.EndFrame();

    const GpuPassSample& sample = profiler.HistoryAt(0).gpuPasses[static_cast<std::size_t>(GpuPass::Present)];
    EXPECT_EQ(sample.status, GpuSampleStatus::Absent);
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
