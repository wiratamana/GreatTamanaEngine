// Tier 1: renderer timing callbacks become profiler scopes; a real scope reads real time.
#include "Core/RenderTimingAdapter.h"
#include "Profiling/FrameProfiler.h"
#include "Profiling/NameTable.h"
#include "Profiling/ProfilingClock.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>

namespace gte {
namespace {

using Profiling::FrameProfiler;
using Profiling::ScopeKind;

class RenderTimingAdapterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
    }

    void TearDown() override { FrameProfiler::Instance().ResetForTesting(); }

    static std::string_view NameOf(const Profiling::ScopeEvent& event)
    {
        return Profiling::ResolveName(event.name);
    }
};

TEST(FormatNamesTest, WaitNamesCarryTheSiteAndDetail)
{
    std::array<char, 96> text{};
    EXPECT_EQ(FormatWaitName(text, BlockSite::Offscreen, "fence"), "GpuWait(Offscreen:fence)");
    EXPECT_EQ(FormatWaitName(text, BlockSite::AcquireImage, nullptr), "GpuWait(AcquireImage)");
    EXPECT_EQ(FormatWaitName(text, BlockSite::PresentQueue, ""), "GpuWait(PresentQueue)");
}

TEST(FormatNamesTest, LongNamesAreTruncatedNotOverrun)
{
    std::array<char, 12> text{};
    const std::string_view name = FormatWaitName(text, BlockSite::Offscreen, "a very long detail text");
    EXPECT_EQ(name.size(), text.size());
    EXPECT_EQ(name, "GpuWait(Offs");
}

TEST(FormatNamesTest, ScopeNamesJoinPrefixAndSuffix)
{
    std::array<char, 64> text{};
    EXPECT_EQ(FormatScopeName(text, "Offscreen", "Compile"), "Offscreen.Compile");
    EXPECT_EQ(FormatScopeName(text, "Declare", nullptr), "Declare.");
}

TEST_F(RenderTimingAdapterTest, FeatureScopeNamesCarryTheFeatureAndCallback)
{
    EXPECT_EQ(Profiling::ResolveName(FeatureScopeName("A")), std::string_view("Feature.A"));
    EXPECT_EQ(Profiling::ResolveName(FeatureScopeName("A", "cb")), std::string_view("Feature.A.cb"));
}

TEST_F(RenderTimingAdapterTest, FeatureScopeNamesAreCutAt127Bytes)
{
    const std::string longName(300, 'x');
    const std::string_view text = Profiling::ResolveName(FeatureScopeName(longName));
    EXPECT_EQ(text.size(), 127u);
    EXPECT_EQ(text.substr(0, 8), "Feature.");
}

TEST_F(RenderTimingAdapterTest, FeatureScopeNameIsInvalidWhileCaptureIsOff)
{
    FrameProfiler::Instance().SetCaptureEnabled(false);
    EXPECT_EQ(FeatureScopeName("A"), Profiling::kInvalidNameId);
}

TEST_F(RenderTimingAdapterTest, ABlockBecomesABlockedScopeWithItsSiteName)
{
    RenderTimingAdapter adapter;
    FrameProfiler& profiler = FrameProfiler::Instance();

    profiler.BeginFrame();
    adapter.OnBlockBegin(BlockSite::Offscreen, "fence");
    adapter.OnBlockEnd(BlockSite::Offscreen, BlockOutcome::Completed);
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, ScopeKind::Blocked);
    EXPECT_EQ(events[0].depth, 0u);
    EXPECT_EQ(NameOf(events[0]), "GpuWait(Offscreen:fence)");
}

TEST_F(RenderTimingAdapterTest, PhasesDeclaresAndPassesNestUnderTheGraphLabel)
{
    RenderTimingAdapter adapter;
    adapter.SetGraphLabel("Offscreen");
    FrameProfiler& profiler = FrameProfiler::Instance();

    profiler.BeginFrame();
    adapter.OnPhaseBegin(RenderPhase::Record);
    adapter.OnPassRecordBegin("Opaque");
    adapter.OnPassRecordEnd();
    adapter.OnPhaseEnd(RenderPhase::Record);
    adapter.OnDeclareBegin("Skinning");
    adapter.OnDeclareEnd();
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(NameOf(events[0]), "Pass.Opaque");
    EXPECT_EQ(events[0].depth, 1u);
    EXPECT_EQ(NameOf(events[1]), "Offscreen.Record");
    EXPECT_EQ(events[1].depth, 0u);
    EXPECT_EQ(NameOf(events[2]), "Declare.Skinning");
    EXPECT_EQ(events[2].kind, ScopeKind::Work);
}

TEST_F(RenderTimingAdapterTest, CallsFromOtherThreadsRecordNothingAndCountNothing)
{
    RenderTimingAdapter adapter;
    FrameProfiler& profiler = FrameProfiler::Instance();

    profiler.BeginFrame();
    std::thread worker([&] {
        adapter.OnBlockBegin(BlockSite::ImmediateSubmit, "buffer upload");
        adapter.OnBlockEnd(BlockSite::ImmediateSubmit, BlockOutcome::Completed);
    });
    worker.join();
    profiler.EndFrame();

    EXPECT_EQ(profiler.LastCompletedFrame().scopes.Size(), 0u);
    EXPECT_EQ(profiler.LastCompletedFrame().summary.foreignScopes, 0u);
}

TEST_F(RenderTimingAdapterTest, ScopesOutsideAFrameAreIgnoredAndStayBalanced)
{
    RenderTimingAdapter adapter;
    FrameProfiler& profiler = FrameProfiler::Instance();

    profiler.BeginFrame();
    profiler.EndFrame();
    adapter.OnBlockBegin(BlockSite::Offscreen, "late");
    adapter.OnBlockEnd(BlockSite::Offscreen, BlockOutcome::Completed);

    profiler.BeginFrame();
    adapter.OnBlockBegin(BlockSite::Offscreen, "early");
    adapter.OnBlockEnd(BlockSite::Offscreen, BlockOutcome::Completed);
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].depth, 0u);
}

TEST_F(RenderTimingAdapterTest, TooDeepNestingIsCountedNotRecordedAndUnwindsCleanly)
{
    RenderTimingAdapter adapter;
    FrameProfiler& profiler = FrameProfiler::Instance();

    profiler.BeginFrame();
    for (int i = 0; i < 40; ++i) {
        adapter.OnPassRecordBegin("Deep");
    }
    for (int i = 0; i < 40; ++i) {
        adapter.OnPassRecordEnd();
    }
    adapter.OnBlockBegin(BlockSite::Offscreen, "after");
    adapter.OnBlockEnd(BlockSite::Offscreen, BlockOutcome::Completed);
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 33u);
    EXPECT_EQ(events.back().depth, 0u);
}

TEST_F(RenderTimingAdapterTest, ARealScopeReadsRealTime)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const Profiling::NameId name = Profiling::InternName("AdapterTestSleep");

    profiler.BeginFrame();
    const Profiling::ScopeStart start = profiler.BeginScope();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    profiler.EndScope(start, name, ScopeKind::Work, Profiling::ViewSlot::Shared);
    profiler.EndFrame();

    const auto events = profiler.LastCompletedFrame().scopes.Events();
    ASSERT_EQ(events.size(), 1u);
    const double milliseconds = Profiling::TicksToMilliseconds(Profiling::DurationTicks(events[0]));
    EXPECT_GE(milliseconds, 9.5);
    EXPECT_LT(milliseconds, 200.0); // Sleep granularity on a loaded host is coarse.
}

} // namespace
} // namespace gte
