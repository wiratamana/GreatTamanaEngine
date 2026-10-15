// Tier-1 tests for PersistentSlotTable (pure, no Vulkan).

#include "Renderer/RenderGraph/PersistentSlotTable.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(PersistentSlotTableTest, DefaultRefIsDead)
{
    const PersistentSlotTable table;
    EXPECT_FALSE(table.IsLive(PersistentSlotRef{}));
}

TEST(PersistentSlotTableTest, AcquiredRefIsLiveAndEpochIsNeverZero)
{
    PersistentSlotTable table;
    const PersistentSlotRef ref = table.Acquire();
    EXPECT_NE(ref.epoch, 0u);
    EXPECT_TRUE(table.IsLive(ref));
}

TEST(PersistentSlotTableTest, ReleaseKillsTheRef)
{
    PersistentSlotTable table;
    const PersistentSlotRef ref = table.Acquire();
    table.Release(ref);
    EXPECT_FALSE(table.IsLive(ref));
}

TEST(PersistentSlotTableTest, ReusedIndexGetsANewEpochAndOldRefStaysDead)
{
    PersistentSlotTable table;
    const PersistentSlotRef first = table.Acquire();
    table.Release(first);
    const PersistentSlotRef second = table.Acquire();

    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.epoch, first.epoch);
    EXPECT_FALSE(table.IsLive(first));
    EXPECT_TRUE(table.IsLive(second));
    EXPECT_EQ(table.SlotCount(), 1u);
}

TEST(PersistentSlotTableTest, ReleasingAStaleRefDoesNotFreeTheNewOwnersSlot)
{
    PersistentSlotTable table;
    const PersistentSlotRef first = table.Acquire();
    table.Release(first);
    const PersistentSlotRef second = table.Acquire();

    table.Release(first); // Stale: must be ignored.
    EXPECT_TRUE(table.IsLive(second));

    const PersistentSlotRef third = table.Acquire();
    EXPECT_NE(third.index, second.index); // The slot was not put on the free list twice.
}

TEST(PersistentSlotTableTest, DoubleReleaseDoesNotDuplicateTheFreeSlot)
{
    PersistentSlotTable table;
    const PersistentSlotRef ref = table.Acquire();
    table.Release(ref);
    table.Release(ref);

    const PersistentSlotRef a = table.Acquire();
    const PersistentSlotRef b = table.Acquire();
    EXPECT_NE(a.index, b.index);
}

TEST(PersistentSlotTableTest, RefFromAnotherTableIsDead)
{
    PersistentSlotTable tableA;
    PersistentSlotTable tableB;
    const PersistentSlotRef fromA = tableA.Acquire();
    const PersistentSlotRef fromB = tableB.Acquire();

    ASSERT_EQ(fromA.index, fromB.index);
    EXPECT_FALSE(tableB.IsLive(fromA));
    EXPECT_FALSE(tableA.IsLive(fromB));
}

TEST(PersistentSlotTableTest, OutOfRangeIndexIsDead)
{
    PersistentSlotTable table;
    const PersistentSlotRef ref = table.Acquire();
    EXPECT_FALSE(table.IsLive(PersistentSlotRef{ ref.index + 5, ref.epoch }));
}

TEST(PersistentSlotTableTest, DistinctLiveSlotsGetDistinctIndices)
{
    PersistentSlotTable table;
    const PersistentSlotRef a = table.Acquire();
    const PersistentSlotRef b = table.Acquire();
    EXPECT_NE(a.index, b.index);
    EXPECT_NE(a.epoch, b.epoch);
    EXPECT_TRUE(table.IsLive(a));
    EXPECT_TRUE(table.IsLive(b));
}

} // namespace
} // namespace gte::rg
