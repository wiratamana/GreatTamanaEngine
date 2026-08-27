// Tier 1: exercises a REAL gte::Jobs::JobSystem::Instance() (real worker
// threads when GTE_ENABLE_JOB_SYSTEM is ON, or the inline-synchronous
// fallback when it's OFF - either way safe on any machine/CI runner, no
// live Vulkan device/SDL window involved) together with a REAL
// gte::Profiling::FrameProfiler::Instance() singleton - proves Job System
// Phase 5's GTE_PROFILE_JOB_SCOPE/JobScopeTimer (src/Profiling/JobScopeTimer.h)
// actually records real, concurrently-safe worker-job samples when called
// from inside a genuine job body. See
// task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md
// and AGENTS.md, "Job System".
#include "Profiling/FrameProfiler.h"
#include "Profiling/JobScopeTimer.h"

#include "Jobs/JobDispatch.h"
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte::Profiling {
namespace {

// See tests/Profiling/FrameProfilerTests.cpp's own class comment for why
// every test resets the shared FrameProfiler::Instance() singleton before
// AND after running.
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

// Called once per item in [beginIndex, endIndex) (NOT once per batch) - so
// the TOTAL number of GTE_PROFILE_JOB_SCOPE constructions across a whole
// Dispatch() call exactly equals the Dispatch()'s own itemCount, regardless
// of how many batches/workers that itemCount happens to be split across
// (see gte::Jobs::Dispatch()'s own batch-vs-item distinction - AGENTS.md,
// "Job System"). This is what lets the tests below assert an EXACT
// workerJobCount.
void RunNamedJobScopePerItem(std::uint32_t beginIndex, std::uint32_t endIndex, void*)
{
    for (std::uint32_t i = beginIndex; i < endIndex; ++i) {
        GTE_PROFILE_JOB_SCOPE("UnitTestJobScope");
        // A tiny amount of real work so the recorded duration is
        // meaningfully non-negative rather than relying purely on clock
        // resolution.
        volatile int spin = 0;
        for (int j = 0; j < 200; ++j) {
            spin += j;
        }
        (void)spin;
    }
}

TEST_F(JobScopeTimerTest, RealDispatchRecordsExactlyOneSamplePerItem)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    Jobs::JobHandle handle;
    Jobs::Dispatch(&RunNamedJobScopePerItem, /*itemCount=*/64, nullptr, handle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(handle);

    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    ASSERT_EQ(frame.workerJobCount, 64u);

    for (std::size_t i = 0; i < frame.workerJobCount; ++i) {
        const WorkerJobSample& sample = frame.workerJobs[i];
        EXPECT_STREQ(sample.name, "UnitTestJobScope");
        EXPECT_GE(sample.milliseconds, 0.0);
        // Every recorded sample must be attributed to a real, in-range
        // worker index - never a garbage/uninitialized value.
        EXPECT_LT(sample.workerIndex, Jobs::JobSystem::Instance().WorkerCount());
    }
}

TEST_F(JobScopeTimerTest, RecordsNothingWhenCaptureIsDisabled)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.SetCaptureEnabled(false);

    profiler.BeginFrame(); // No-op while disabled.
    Jobs::JobHandle handle;
    Jobs::Dispatch(&RunNamedJobScopePerItem, /*itemCount=*/8, nullptr, handle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(handle);
    profiler.EndFrame(); // Also a no-op - nothing pushed to history.
    EXPECT_EQ(profiler.HistoryCount(), 0u);

    profiler.SetCaptureEnabled(true);
    profiler.BeginFrame();
    profiler.EndFrame();
    ASSERT_EQ(profiler.HistoryCount(), 1u);
    EXPECT_EQ(profiler.HistoryAt(0).workerJobCount, 0u);
}

void RunOverflowJobScopePerItem(std::uint32_t beginIndex, std::uint32_t endIndex, void*)
{
    for (std::uint32_t i = beginIndex; i < endIndex; ++i) {
        GTE_PROFILE_JOB_SCOPE("OverflowScope");
    }
}

TEST_F(JobScopeTimerTest, OverflowingCapacityIsClampedNotCrashed)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();

    Jobs::JobHandle handle;
    const std::uint32_t itemCount = static_cast<std::uint32_t>(kMaxWorkerJobSamplesPerFrame) + 32;
    Jobs::Dispatch(&RunOverflowJobScopePerItem, itemCount, nullptr, handle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(handle);

    profiler.EndFrame();

    // Clamped to the fixed capacity - never crashes, never silently exceeds
    // it (see FrameProfiler::EndFrame()'s own std::min() clamp).
    EXPECT_EQ(profiler.HistoryAt(0).workerJobCount, kMaxWorkerJobSamplesPerFrame);
}

#if GTE_ENABLE_JOB_SYSTEM

// This specific assertion (a main-thread-direct call records NOTHING) only
// holds when GTE_ENABLE_JOB_SYSTEM is ON, where the main thread and a real
// worker thread are genuinely distinguishable - see
// gte::Jobs::JobSystem::WorkerIndexForCurrentThread()'s own comment for why
// a GTE_ENABLE_JOB_SYSTEM=OFF build cannot draw (and therefore does not
// attempt) this same distinction.
TEST_F(JobScopeTimerTest, RecordsNothingWhenCalledFromTheMainThreadDirectly)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        GTE_PROFILE_JOB_SCOPE("ShouldNeverBeRecorded");
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.HistoryAt(0).workerJobCount, 0u);
}

#endif // GTE_ENABLE_JOB_SYSTEM

#else // !GTE_ENABLE_PROFILER

TEST_F(JobScopeTimerTest, CompiledOutJobScopeTimerConstructsWithoutRecordingAnything)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    {
        JobScopeTimer timer("Whatever");
        (void)timer;
    }
    profiler.EndFrame();

    EXPECT_EQ(profiler.HistoryAt(0).workerJobCount, 0u);
}

#endif // GTE_ENABLE_PROFILER

} // namespace
} // namespace gte::Profiling
