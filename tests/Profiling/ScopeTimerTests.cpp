// Tier 1: the scope macros and ScopeTimer against the real FrameProfiler.
#include "Profiling/FrameProfiler.h"
#include "Profiling/NameTable.h"
#include "Profiling/ScopeTimer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

namespace gte::Profiling {
namespace {

// FrameProfiler is a process-wide singleton, so each test starts from a clean state.
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

TEST_F(ScopeTimerTest, ScopeMeasuresTheTimeItCovers)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_SCOPE("UnitTestScope");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_STREQ(ResolveName(events[0].name), "UnitTestScope");
    EXPECT_EQ(events[0].kind, ScopeKind::Work);

    const double milliseconds = static_cast<double>(DurationTicks(events[0])) / 1.0e6;
    EXPECT_GE(milliseconds, 9.5);
    EXPECT_LT(milliseconds, 200.0);
}

TEST_F(ScopeTimerTest, NestedScopesKeepTheirDepth)
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

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 2u); // children first
    EXPECT_STREQ(ResolveName(events[0].name), "Inner");
    EXPECT_EQ(events[0].depth, 1u);
    EXPECT_STREQ(ResolveName(events[1].name), "Outer");
    EXPECT_EQ(events[1].depth, 0u);
}

TEST_F(ScopeTimerTest, SameCallSiteUsesOneInternedName)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    for (int i = 0; i < 3; ++i) {
        GTE_PROFILE_SCOPE("LoopScope");
    }
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].name, events[1].name);
    EXPECT_EQ(events[1].name, events[2].name);
}

TEST_F(ScopeTimerTest, BlockedScopeAndMarkerKeepTheirKind)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_BLOCKED("WaitScope");
    }
    GTE_PROFILE_MARKER("MarkerPoint");
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, ScopeKind::Blocked);
    EXPECT_EQ(events[1].kind, ScopeKind::Marker);
}

TEST_F(ScopeTimerTest, DynamicNamesUseTheirInternedId)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const NameId id = InternName(std::string("Dynamic").append("Name").c_str());

    profiler.BeginFrame();
    {
        GTE_PROFILE_SCOPE_ID(id);
    }
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_STREQ(ResolveName(events[0].name), "DynamicName");
}

TEST_F(ScopeTimerTest, ScopeRecordsNothingWhenCaptureIsOff)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);
    profiler.BeginFrame();
    {
        GTE_PROFILE_SCOPE("ShouldNotBeRecorded");
    }
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryCount(), 0u);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.LastCompletedFrame().scopes.Size(), 0u);
}

#else

TEST_F(ScopeTimerTest, CompiledOutScopeRecordsNothing)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_SCOPE("Whatever");
        GTE_PROFILE_MARKER("Whatever");
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.LastCompletedFrame().scopes.Size(), 0u);
}

#endif

} // namespace
} // namespace gte::Profiling
