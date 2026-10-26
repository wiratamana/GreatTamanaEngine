#include "Profiling/ProfilingClock.h"
#include "Profiling/WorkerRing.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace gte::Profiling {
namespace {

constexpr std::uint64_t kForever = ~std::uint64_t{ 0 };

WorkerEvent MakeEvent(std::uint64_t start, std::uint64_t end, NameId name = 2, std::uint16_t worker = 1)
{
    return WorkerEvent{ start, end, name, worker, 0, 0 };
}

class WorkerRingTest : public ::testing::Test {
protected:
    std::unique_ptr<WorkerRing> ring = std::make_unique<WorkerRing>();
    std::unique_ptr<WorkerEventBuffer> frame = std::make_unique<WorkerEventBuffer>();
};

TEST_F(WorkerRingTest, PushedEventsDrainInOrderWithTheirFields)
{
    ring->Push(MakeEvent(1, 10, 5, 3));
    ring->Push(MakeEvent(2, 20, 6, 4));
    ring->Push(MakeEvent(3, 30, 7, 5));

    const WorkerDrainStats stats = ring->Drain(0, 100, *frame);
    EXPECT_EQ(stats.drained, 3u);
    EXPECT_EQ(stats.carried, 0u);
    EXPECT_EQ(stats.lost, 0u);
    EXPECT_FALSE(stats.pending);

    const auto events = frame->Events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].name, 5u);
    EXPECT_EQ(events[1].worker, 4u);
    EXPECT_EQ(events[2].endTicks, 30u);
    EXPECT_EQ(events[2].flags, 0u);

    frame->Clear();
    EXPECT_EQ(ring->Drain(0, 100, *frame).drained, 0u); // nothing is delivered twice
}

TEST_F(WorkerRingTest, EventStartedBeforeTheFrameIsFlaggedCarried)
{
    ring->Push(MakeEvent(5, 50));
    const WorkerDrainStats stats = ring->Drain(10, 100, *frame);

    EXPECT_EQ(stats.carried, 1u);
    ASSERT_EQ(frame->Size(), 1u);
    EXPECT_EQ(frame->Events()[0].flags & kWorkerEventCarried, kWorkerEventCarried);
}

TEST_F(WorkerRingTest, EventEndingAfterTheFrameWaitsForTheNextDrain)
{
    ring->Push(MakeEvent(150, 200));

    WorkerDrainStats stats = ring->Drain(0, 100, *frame);
    EXPECT_EQ(stats.drained, 0u);
    EXPECT_TRUE(stats.pending);

    stats = ring->Drain(100, 300, *frame);
    EXPECT_EQ(stats.drained, 1u);
    EXPECT_EQ(stats.carried, 0u);
    EXPECT_FALSE(stats.pending);
}

TEST_F(WorkerRingTest, EventsBeyondTheFrameCapacityAreCounted)
{
    for (std::uint64_t i = 0; i <= kMaxWorkerEventsPerFrame; ++i) { // one more than fits
        ring->Push(MakeEvent(i, i + 1));
    }
    const WorkerDrainStats stats = ring->Drain(0, kForever, *frame);

    EXPECT_EQ(stats.drained, kMaxWorkerEventsPerFrame);
    EXPECT_EQ(stats.droppedFull, 1u);
    EXPECT_EQ(frame->Dropped(), 1u);
    EXPECT_TRUE(frame->Partial());
}

TEST_F(WorkerRingTest, LappingTheConsumerOverwritesTheOldestAndCountsThem)
{
    constexpr std::uint64_t kPushed = kWorkerRingSlots + 6;
    for (std::uint64_t i = 0; i < kPushed; ++i) {
        ring->Push(MakeEvent(i, i + 1));
    }
    const WorkerDrainStats stats = ring->Drain(0, kForever, *frame);

    EXPECT_EQ(stats.lost, 6u);
    EXPECT_EQ(ring->LapCount(), 6u);
    EXPECT_EQ(stats.drained + stats.droppedFull, kWorkerRingSlots);

    // The oldest surviving event is ticket 6.
    ASSERT_GT(frame->Size(), 0u);
    EXPECT_EQ(frame->Events()[0].startTicks, 6u);
}

TEST_F(WorkerRingTest, DisabledRingRefusesEventsWithoutCountingThem)
{
    ring->SetEnabled(false);
    EXPECT_FALSE(ring->Enabled());
    EXPECT_FALSE(ring->Push(MakeEvent(1, 2)));
    EXPECT_EQ(ring->Drain(0, kForever, *frame).drained, 0u);
    EXPECT_EQ(ring->LapCount(), 0u);

    ring->SetEnabled(true);
    EXPECT_TRUE(ring->Push(MakeEvent(1, 2)));
    EXPECT_EQ(ring->Drain(0, kForever, *frame).drained, 1u);
}

TEST_F(WorkerRingTest, EightProducersAgainstFrameBracketsKeepCountsConsistent)
{
    constexpr unsigned kProducers = 8;
    constexpr std::uint64_t kPerProducer = 20000;

    std::atomic<std::uint64_t> pushed{ 0 };
    std::atomic<unsigned> running{ kProducers };
    std::vector<std::thread> producers;
    for (unsigned p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            std::uint64_t accepted = 0;
            for (std::uint64_t i = 0; i < kPerProducer; ++i) {
                const std::uint64_t start = GetProfilingPerformanceCounter();
                if (ring->Push(MakeEvent(start, GetProfilingPerformanceCounter(), 2 + p, static_cast<std::uint16_t>(p)))) {
                    ++accepted;
                }
            }
            pushed.fetch_add(accepted, std::memory_order_relaxed);
            running.fetch_sub(1, std::memory_order_release);
        });
    }

    std::uint64_t drained = 0;
    std::uint64_t droppedFull = 0;
    std::uint64_t lost = 0;
    bool bufferMatchesStats = true;
    const auto account = [&](const WorkerDrainStats& stats) {
        drained += stats.drained;
        droppedFull += stats.droppedFull;
        lost += stats.lost;
        bufferMatchesStats = bufferMatchesStats && frame->Size() == stats.drained;
    };

    std::uint64_t frameStart = GetProfilingPerformanceCounter();
    while (running.load(std::memory_order_acquire) > 0) {
        frame->Clear();
        const std::uint64_t frameEnd = GetProfilingPerformanceCounter();
        account(ring->Drain(frameStart, frameEnd, *frame));
        frameStart = frameEnd;
        std::this_thread::yield();
    }
    for (std::thread& t : producers) {
        t.join();
    }

    // All producers are done: drain until nothing moves any more.
    WorkerDrainStats last;
    do {
        frame->Clear();
        last = ring->Drain(frameStart, kForever, *frame);
        account(last);
    } while (last.drained + last.droppedFull + last.lost > 0);

    EXPECT_TRUE(bufferMatchesStats);
    EXPECT_FALSE(last.pending);
    EXPECT_EQ(pushed.load(), kProducers * kPerProducer);
    EXPECT_EQ(drained + droppedFull + lost, pushed.load()); // every event is read or counted as lost
    EXPECT_EQ(ring->LapCount(), lost);
}

TEST_F(WorkerRingTest, JobSpanningFramesIsCarriedInTheFrameWhereItEnded)
{
    const std::uint64_t jobStart = GetProfilingPerformanceCounter();
    std::atomic<bool> finish{ false };
    std::thread job([&] {
        while (!finish.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        ring->Push(MakeEvent(jobStart, GetProfilingPerformanceCounter(), 9, 2));
    });

    std::uint64_t frameStart = jobStart;
    for (int i = 0; i < 3; ++i) { // three frames pass while the job runs
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        frame->Clear();
        const std::uint64_t frameEnd = GetProfilingPerformanceCounter();
        EXPECT_EQ(ring->Drain(frameStart, frameEnd, *frame).drained, 0u);
        frameStart = frameEnd;
    }

    finish.store(true, std::memory_order_release);
    job.join();

    frame->Clear();
    const WorkerDrainStats stats = ring->Drain(frameStart, GetProfilingPerformanceCounter(), *frame);
    EXPECT_EQ(stats.drained, 1u);
    EXPECT_EQ(stats.carried, 1u);
    ASSERT_EQ(frame->Size(), 1u);
    EXPECT_EQ(frame->Events()[0].flags & kWorkerEventCarried, kWorkerEventCarried);
    EXPECT_EQ(frame->Events()[0].startTicks, jobStart);
}

} // namespace
} // namespace gte::Profiling
