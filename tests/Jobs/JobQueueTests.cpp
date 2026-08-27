// Tier 1: gte::Jobs::detail::JobQueue is a plain, self-contained
// fixed-capacity ring buffer - no JobSystem singleton, no real worker
// threads, and (per JobQueue.h's own comment) it compiles unconditionally
// regardless of GTE_ENABLE_JOB_SYSTEM, so these tests always run in every
// build configuration. Every WaitAndPop() call below is only ever made
// when the queue is known, by construction, to already contain an entry
// (or to already be shutting down) - never on an empty, still-open queue -
// so nothing here can ever block.
#include "Jobs/JobQueue.h"

#include <gtest/gtest.h>

namespace gte::Jobs::detail {
namespace {

void NoOpJobFunction(void* /*payload*/) { }

TEST(JobQueueTests, TryPushSucceedsUpToCapacityThenFails)
{
    JobQueue queue(3);

    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;

    EXPECT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &a, nullptr }));
    EXPECT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &b, nullptr }));
    EXPECT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &c, nullptr }));

    // Queue is now at its fixed capacity of 3 - a fourth push must fail
    // gracefully (never block/grow/assert) per JobQueue's own documented
    // full-queue contract.
    EXPECT_FALSE(queue.TryPush(JobEntry{ &NoOpJobFunction, &d, nullptr }));
}

TEST(JobQueueTests, WaitAndPopReturnsEntriesInFifoOrder)
{
    JobQueue queue(4);

    int first = 1;
    int second = 2;
    int third = 3;

    ASSERT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &first, nullptr }));
    ASSERT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &second, nullptr }));
    ASSERT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &third, nullptr }));

    JobEntry outFirst;
    JobEntry outSecond;
    JobEntry outThird;
    ASSERT_TRUE(queue.WaitAndPop(outFirst));
    ASSERT_TRUE(queue.WaitAndPop(outSecond));
    ASSERT_TRUE(queue.WaitAndPop(outThird));

    EXPECT_EQ(outFirst.payload, &first);
    EXPECT_EQ(outSecond.payload, &second);
    EXPECT_EQ(outThird.payload, &third);
}

TEST(JobQueueTests, CapacityIsReusableAfterDraining)
{
    JobQueue queue(2);

    int a = 0;
    int b = 0;
    ASSERT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &a, nullptr }));
    ASSERT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &b, nullptr }));
    EXPECT_FALSE(queue.TryPush(JobEntry{ &NoOpJobFunction, &a, nullptr }));

    JobEntry out;
    ASSERT_TRUE(queue.WaitAndPop(out));

    // Draining one slot must make room for exactly one more push - proves
    // the ring buffer's head/tail bookkeeping wraps correctly rather than
    // permanently losing capacity once the tail index has wrapped once.
    int c = 0;
    EXPECT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &c, nullptr }));
}

TEST(JobQueueTests, ShutdownMakesWaitAndPopReturnFalseOnceEmpty)
{
    JobQueue queue(2);
    queue.Shutdown();

    // The queue was never pushed to at all - Shutdown() plus an empty
    // queue is exactly the worker's own "please exit your loop" signal, so
    // this must return false immediately without blocking.
    JobEntry out;
    EXPECT_FALSE(queue.WaitAndPop(out));
}

TEST(JobQueueTests, ShutdownStillDrainsAlreadyQueuedEntriesFirst)
{
    JobQueue queue(2);

    int a = 0;
    ASSERT_TRUE(queue.TryPush(JobEntry{ &NoOpJobFunction, &a, nullptr }));

    queue.Shutdown();

    // An entry that was already queued before Shutdown() was called must
    // still be handed back once - Shutdown() only affects behavior once the
    // queue is actually empty, never discards already-queued work.
    JobEntry out;
    ASSERT_TRUE(queue.WaitAndPop(out));
    EXPECT_EQ(out.payload, &a);

    JobEntry outAfter;
    EXPECT_FALSE(queue.WaitAndPop(outAfter));
}

} // namespace
} // namespace gte::Jobs::detail
