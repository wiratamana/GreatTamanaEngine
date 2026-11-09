// Tier-1 tests: the retire queue frees an object only once the frame that tagged it is done.

#include "Renderer/RetireQueue.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace gte {
namespace {

// Counts how many of these are alive; stands in for a GPU object.
struct Tracked {
    explicit Tracked(int* aliveCounter) noexcept
        : alive(aliveCounter)
    {
        ++*alive;
    }
    Tracked(Tracked&& other) noexcept
        : alive(std::exchange(other.alive, nullptr))
    {
    }
    Tracked& operator=(Tracked&&) = delete;
    ~Tracked()
    {
        if (alive != nullptr) {
            --*alive;
        }
    }
    int* alive;
};

TEST(RetireQueueTest, NothingIsFreedBeforeItsFrameIsDone)
{
    int alive = 0;
    RetireQueue queue;
    queue.Push(3, Tracked(&alive));
    EXPECT_EQ(alive, 1);
    EXPECT_EQ(queue.Collect(2), 0u);
    EXPECT_EQ(alive, 1);
    EXPECT_EQ(queue.Pending(), 1u);
}

TEST(RetireQueueTest, AnEntryIsFreedWhenCompletedReachesItsTag)
{
    int alive = 0;
    RetireQueue queue;
    queue.Push(3, Tracked(&alive));
    EXPECT_EQ(queue.Collect(3), 1u);
    EXPECT_EQ(alive, 0);
    EXPECT_EQ(queue.Pending(), 0u);
}

TEST(RetireQueueTest, OnlyTheEntriesOfFinishedFramesAreFreed)
{
    int alive = 0;
    RetireQueue queue;
    queue.Push(1, Tracked(&alive));
    queue.Push(2, Tracked(&alive));
    queue.Push(3, Tracked(&alive));
    EXPECT_EQ(queue.Collect(2), 2u);
    EXPECT_EQ(alive, 1);
    EXPECT_EQ(queue.Pending(), 1u);
    EXPECT_EQ(queue.TotalFreed(), 2u);
}

TEST(RetireQueueTest, ASynchronousTagIsFreedByTheFirstCollect)
{
    int alive = 0;
    RetireQueue queue;
    queue.Push(0, Tracked(&alive));
    EXPECT_EQ(queue.Collect(0), 1u);
    EXPECT_EQ(alive, 0);
}

TEST(RetireQueueTest, ADestructorMayRetireAgainWhileCollecting)
{
    // Freeing the first entry retires another object; the walk must not be disturbed.
    struct Chain {
        RetireQueue* queue;
        int* alive;
        Chain(RetireQueue* owner, int* counter) noexcept
            : queue(owner)
            , alive(counter)
        {
            ++*alive;
        }
        Chain(Chain&& other) noexcept
            : queue(std::exchange(other.queue, nullptr))
            , alive(std::exchange(other.alive, nullptr))
        {
        }
        Chain& operator=(Chain&&) = delete;
        ~Chain()
        {
            if (queue != nullptr) {
                queue->Push(9, Tracked(alive));
                --*alive;
            }
        }
    };

    int alive = 0;
    RetireQueue queue;
    queue.Push(1, Chain(&queue, &alive));
    EXPECT_EQ(queue.Collect(1), 1u);
    EXPECT_EQ(alive, 1);          // The object the destructor retired.
    EXPECT_EQ(queue.Pending(), 1u);
    EXPECT_EQ(queue.Collect(9), 1u);
    EXPECT_EQ(alive, 0);
}

TEST(RetireQueueTest, ReleaseAllFreesEverythingWhateverTheTag)
{
    int alive = 0;
    RetireQueue queue;
    queue.Push(5, Tracked(&alive));
    queue.Push(99, Tracked(&alive));
    EXPECT_EQ(queue.ReleaseAll(), 2u);
    EXPECT_EQ(alive, 0);
    EXPECT_EQ(queue.Pending(), 0u);
}

TEST(RetireQueueTest, TakeAllHandsTheOwnersOverWithoutFreeingThem)
{
    int alive = 0;
    RetireQueue queue;
    queue.Push(5, Tracked(&alive));
    std::vector<std::shared_ptr<void>> owners = queue.TakeAll();
    EXPECT_EQ(owners.size(), 1u);
    EXPECT_EQ(queue.Pending(), 0u);
    EXPECT_EQ(alive, 1); // Still alive: the caller keeps it.
    owners.clear();
    EXPECT_EQ(alive, 0);
}

TEST(RetireQueueTest, TheDestructorOfTheQueueFreesWhatIsLeft)
{
    int alive = 0;
    {
        RetireQueue queue;
        queue.Push(7, Tracked(&alive));
        EXPECT_EQ(alive, 1);
    }
    EXPECT_EQ(alive, 0);
}

} // namespace
} // namespace gte
