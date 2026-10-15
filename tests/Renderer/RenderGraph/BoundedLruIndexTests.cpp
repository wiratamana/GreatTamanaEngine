// Tier-1 tests for BoundedLruIndex: key -> slot with least-recently-used eviction.

#include "Renderer/RenderGraph/BoundedLruIndex.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

using Index = BoundedLruIndex<int, 3>;
using Outcome = Index::Outcome;

TEST(BoundedLruIndexTest, NewKeysFillFreeSlotsInOrder)
{
    Index index;
    EXPECT_EQ(index.Touch(10, 1).slot, 0u);
    EXPECT_EQ(index.Touch(20, 2).slot, 1u);
    const Index::Result third = index.Touch(30, 3);
    EXPECT_EQ(third.slot, 2u);
    EXPECT_EQ(third.outcome, Outcome::Inserted);
}

TEST(BoundedLruIndexTest, KnownKeyIsAHitInItsSlot)
{
    Index index;
    index.Touch(10, 1);
    const Index::Result second = index.Touch(20, 2);
    const Index::Result again = index.Touch(20, 3);
    EXPECT_EQ(again.outcome, Outcome::Hit);
    EXPECT_EQ(again.slot, second.slot);
}

TEST(BoundedLruIndexTest, FullIndexEvictsTheLeastRecentlyUsedSlot)
{
    Index index;
    index.Touch(10, 1);
    index.Touch(20, 2);
    index.Touch(30, 3);
    index.Touch(10, 4); // 20 is now the oldest.

    const Index::Result evicting = index.Touch(40, 5);
    EXPECT_EQ(evicting.outcome, Outcome::Evicted);
    EXPECT_EQ(evicting.slot, 1u);
    EXPECT_EQ(index.Touch(20, 6).outcome, Outcome::Evicted); // 20 is gone.
}

TEST(BoundedLruIndexTest, EvictedKeyNoLongerHits)
{
    Index index;
    index.Touch(10, 1);
    index.Touch(20, 2);
    index.Touch(30, 3);
    index.Touch(40, 4); // Evicts 10.
    EXPECT_NE(index.Touch(10, 5).outcome, Outcome::Hit);
}

TEST(BoundedLruIndexTest, ReleasedSlotIsReusedBeforeAnythingIsEvicted)
{
    Index index;
    index.Touch(10, 1);
    const Index::Result claimed = index.Touch(20, 2);
    index.Touch(30, 3);
    index.Release(claimed.slot);

    const Index::Result next = index.Touch(40, 4);
    EXPECT_EQ(next.outcome, Outcome::Inserted);
    EXPECT_EQ(next.slot, claimed.slot);
    EXPECT_EQ(index.Touch(10, 5).outcome, Outcome::Hit);
}

TEST(BoundedLruIndexTest, EqualTicksEvictTheLowestSlot)
{
    Index index;
    index.Touch(10, 7);
    index.Touch(20, 7);
    index.Touch(30, 7);
    EXPECT_EQ(index.Touch(40, 8).slot, 0u);
}

} // namespace
} // namespace gte::rg
