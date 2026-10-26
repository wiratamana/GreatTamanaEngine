// Tier 1: GTE_PROFILE_JOB_SCOPE inside real job bodies, with the real JobSystem and FrameProfiler.
#include "Profiling/FrameProfiler.h"
#include "Profiling/JobScopeTimer.h"
#include "Profiling/NameTable.h"

#include "Jobs/JobDispatch.h"
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte::Profiling {
namespace {

// FrameProfiler is a process-wide singleton, so each test starts from a clean state.
class JobScopeTimerTest : public ::testing::Test {
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

// One scope per item, so the scope count equals the Dispatch item count however the items are batched.
void RunNamedJobScopePerItem(std::uint32_t beginIndex, std::uint32_t endIndex, void*)
{
    for (std::uint32_t i = beginIndex; i < endIndex; ++i) {
        GTE_PROFILE_JOB_SCOPE("UnitTestJobScope");
        volatile int spin = 0;
        for (int j = 0; j < 200; ++j) {
            spin += j;
        }
        (void)spin;
    }
}

TEST_F(JobScopeTimerTest, RealDispatchRecordsExactlyOneEventPerItem)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    Jobs::JobHandle handle;
    Jobs::Dispatch(&RunNamedJobScopePerItem, /*itemCount=*/64, nullptr, handle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(handle);

    profiler.EndFrame();

    const FrameRecord& frame = profiler.LastCompletedFrame();
    ASSERT_EQ(frame.workers.Size(), 64u);

    for (const WorkerEvent& event : frame.workers.Events()) {
        EXPECT_STREQ(ResolveName(event.name), "UnitTestJobScope");
        EXPECT_GE(event.endTicks, event.startTicks);
        EXPECT_LT(event.worker, Jobs::JobSystem::Instance().WorkerCount());
    }
}

TEST_F(JobScopeTimerTest, RecordsNothingWhenCaptureIsOff)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);

    profiler.BeginFrame();
    Jobs::JobHandle handle;
    Jobs::Dispatch(&RunNamedJobScopePerItem, /*itemCount=*/8, nullptr, handle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(handle);
    profiler.EndFrame();
    EXPECT_EQ(profiler.HistoryCount(), 0u);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    EXPECT_EQ(profiler.LastCompletedFrame().workers.Size(), 0u);
}

void RunOverflowJobScopePerItem(std::uint32_t beginIndex, std::uint32_t endIndex, void*)
{
    for (std::uint32_t i = beginIndex; i < endIndex; ++i) {
        GTE_PROFILE_JOB_SCOPE("OverflowScope");
    }
}

TEST_F(JobScopeTimerTest, OverflowIsClampedAndReportedAsPartial)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    Jobs::JobHandle handle;
    const std::uint32_t itemCount = static_cast<std::uint32_t>(kMaxWorkerEventsPerFrame) + 600;
    Jobs::Dispatch(&RunOverflowJobScopePerItem, itemCount, nullptr, handle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(handle);

    profiler.EndFrame();

    const FrameRecord& frame = profiler.LastCompletedFrame();
    EXPECT_EQ(frame.workers.Size(), kMaxWorkerEventsPerFrame);
    EXPECT_TRUE(frame.workers.Partial() || frame.summary.lostWorkerEvents != 0);
}

#if GTE_ENABLE_JOB_SYSTEM

// With a real worker pool the main thread is not a worker, so a direct call records nothing.
TEST_F(JobScopeTimerTest, RecordsNothingWhenCalledFromTheMainThreadDirectly)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_JOB_SCOPE("ShouldNeverBeRecorded");
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.LastCompletedFrame().workers.Size(), 0u);
}

#endif // GTE_ENABLE_JOB_SYSTEM

#else // !GTE_ENABLE_PROFILER

TEST_F(JobScopeTimerTest, CompiledOutJobScopeRecordsNothing)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_JOB_SCOPE("Whatever");
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.LastCompletedFrame().workers.Size(), 0u);
}

#endif // GTE_ENABLE_PROFILER

} // namespace
} // namespace gte::Profiling
