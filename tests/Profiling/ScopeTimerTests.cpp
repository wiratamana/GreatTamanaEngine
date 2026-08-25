#include "Profiling/FrameProfiler.h"
#include "Profiling/ScopeTimer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

namespace gte::Profiling {
namespace {

// See tests/Profiling/FrameProfilerTests.cpp's own class comment for why
// every test resets the shared FrameProfiler::Instance() singleton before
// AND after running.
class ScopeTimerTest : public ::testing::Test {
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

#if GTE_ENABLE_PROFILER

TEST_F(ScopeTimerTest, ScopeTimerRecordsAPositiveDurationIntoTheCurrentFrame)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_SCOPE("UnitTestScope");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    ASSERT_EQ(frame.cpuScopeCount, 1u);
    EXPECT_STREQ(frame.cpuScopes[0].name, "UnitTestScope");
    EXPECT_GT(frame.cpuScopes[0].totalMilliseconds, 0.0);
    EXPECT_EQ(frame.cpuScopes[0].callCount, 1u);
}

TEST_F(ScopeTimerTest, NestedScopesEachGetTheirOwnFlatEntry)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_SCOPE("Outer");
        {
            GTE_PROFILE_SCOPE("Inner");
        }
    }
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    ASSERT_EQ(frame.cpuScopeCount, 2u);

    bool foundOuter = false;
    bool foundInner = false;
    for (std::size_t i = 0; i < frame.cpuScopeCount; ++i) {
        if (std::string(frame.cpuScopes[i].name) == "Outer") {
            foundOuter = true;
        } else if (std::string(frame.cpuScopes[i].name) == "Inner") {
            foundInner = true;
        }
    }
    EXPECT_TRUE(foundOuter);
    EXPECT_TRUE(foundInner);
}

TEST_F(ScopeTimerTest, ScopeTimerRecordsNothingWhenCaptureIsDisabled)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);
    profiler.BeginFrame(); // No-op while disabled.
    {
        GTE_PROFILE_SCOPE("ShouldNotBeRecorded");
    }
    profiler.EndFrame(); // Also a no-op - nothing pushed to history.
    EXPECT_EQ(profiler.HistoryCount(), 0u);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    ASSERT_EQ(profiler.HistoryCount(), 1u);
    EXPECT_EQ(profiler.HistoryAt(0).cpuScopeCount, 0u);
}

#else

TEST_F(ScopeTimerTest, CompiledOutScopeTimerConstructsWithoutRecordingAnything)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        ScopeTimer timer("Whatever");
        (void)timer;
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.HistoryAt(0).cpuScopeCount, 0u);
}

#endif

} // namespace
} // namespace gte::Profiling
