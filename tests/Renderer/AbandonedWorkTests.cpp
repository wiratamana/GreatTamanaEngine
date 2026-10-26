// Ownership rules of parked GPU objects (src/Renderer/AbandonedWork.h). No device involved.

#include "Renderer/AbandonedWork.h"

#include <gtest/gtest.h>

#include <memory>

namespace gte {
namespace {

// Owner whose destruction is counted.
struct Counted {
    explicit Counted(int& counter) : destroyed(counter) {}
    ~Counted() { ++destroyed; }
    int& destroyed;
};

std::shared_ptr<void> CountedOwner(int& destroyed)
{
    return std::make_shared<Counted>(destroyed);
}

TEST(AbandonedWorkTest, ParkKeepsTheOwnerAliveAndCounts)
{
    int destroyed = 0;
    AbandonedWork work;
    EXPECT_EQ(work.Count(), 0u);

    work.Park(CountedOwner(destroyed));
    work.Park(CountedOwner(destroyed));
    EXPECT_EQ(work.Count(), 2u);
    EXPECT_EQ(destroyed, 0);

    work.Release(true);
}

TEST(AbandonedWorkTest, ReleaseAfterIdleFreesEverythingOnce)
{
    int destroyed = 0;
    AbandonedWork work;
    work.Park(CountedOwner(destroyed));
    work.Park(CountedOwner(destroyed));
    work.Park(CountedOwner(destroyed));

    work.Release(true);
    EXPECT_EQ(destroyed, 3);
    EXPECT_EQ(work.Count(), 0u);

    work.Release(true);
    EXPECT_EQ(destroyed, 3);
}

TEST(AbandonedWorkTest, ReleaseWithoutIdleFreesNothing)
{
    int destroyed = 0;
    {
        AbandonedWork work;
        work.Park(CountedOwner(destroyed));
        work.Park(CountedOwner(destroyed));
        work.Release(false);
        EXPECT_EQ(work.Count(), 0u);
    }
    EXPECT_EQ(destroyed, 0);
}

TEST(AbandonedWorkTest, ParkingNothingIsIgnored)
{
    AbandonedWork work;
    work.Park(nullptr);
    EXPECT_EQ(work.Count(), 0u);
}

} // namespace
} // namespace gte
