#include "Renderer/RenderGraph/RenderGraphNameSlotTable.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(RenderGraphNameSlotTableTests, FirstCallToANewNameAssignsSlotZero)
{
    RenderGraphNameSlotTable table(4);
    EXPECT_EQ(table.AssignOrGetSlot("GameView"), 0);
    EXPECT_EQ(table.AssignedCount(), 1u);
}

TEST(RenderGraphNameSlotTableTests, DistinctNamesGetIncreasingSlots)
{
    RenderGraphNameSlotTable table(4);
    EXPECT_EQ(table.AssignOrGetSlot("GameView"), 0);
    EXPECT_EQ(table.AssignOrGetSlot("SceneView"), 1);
    EXPECT_EQ(table.AssignOrGetSlot("Present"), 2);
    EXPECT_EQ(table.AssignedCount(), 3u);
}

TEST(RenderGraphNameSlotTableTests, ReusingTheExactSamePointerReturnsTheSameSlot)
{
    RenderGraphNameSlotTable table(4);
    static const char* kName = "GameView";
    const std::int32_t first = table.AssignOrGetSlot(kName);
    const std::int32_t second = table.AssignOrGetSlot(kName);
    EXPECT_EQ(first, second);
    EXPECT_EQ(table.AssignedCount(), 1u);
}

TEST(RenderGraphNameSlotTableTests, ReusingEqualContentFromADifferentPointerReturnsTheSameSlot)
{
    RenderGraphNameSlotTable table(4);
    // Two separate string literals with identical content are NOT
    // guaranteed to share a pointer (string-literal pooling is a compiler
    // optimization, not a language guarantee) - build one from a local
    // array so this is never accidentally coalesced by the compiler.
    const char localCopy[] = { 'G', 'a', 'm', 'e', 'V', 'i', 'e', 'w', '\0' };
    const std::int32_t first = table.AssignOrGetSlot("GameView");
    const std::int32_t second = table.AssignOrGetSlot(localCopy);
    EXPECT_EQ(first, second);
    EXPECT_EQ(table.AssignedCount(), 1u);
}

TEST(RenderGraphNameSlotTableTests, NullNameReturnsSentinelAndAssignsNothing)
{
    RenderGraphNameSlotTable table(4);
    EXPECT_EQ(table.AssignOrGetSlot(nullptr), kNoNameSlot);
    EXPECT_EQ(table.AssignedCount(), 0u);
}

TEST(RenderGraphNameSlotTableTests, OverflowReturnsSentinelForABrandNewNameOnceBudgetIsExhausted)
{
    RenderGraphNameSlotTable table(2);
    EXPECT_EQ(table.AssignOrGetSlot("A"), 0);
    EXPECT_EQ(table.AssignOrGetSlot("B"), 1);
    // Budget of 2 is now fully spent - a brand-new third name must degrade
    // to the sentinel, never crash and never silently alias A's or B's slot.
    EXPECT_EQ(table.AssignOrGetSlot("C"), kNoNameSlot);
    EXPECT_EQ(table.AssignedCount(), 2u);
}

TEST(RenderGraphNameSlotTableTests, AlreadyAssignedNamesStillResolveCorrectlyAfterOverflow)
{
    RenderGraphNameSlotTable table(2);
    EXPECT_EQ(table.AssignOrGetSlot("A"), 0);
    EXPECT_EQ(table.AssignOrGetSlot("B"), 1);
    EXPECT_EQ(table.AssignOrGetSlot("C"), kNoNameSlot);
    // A previously-assigned name must keep resolving to its own slot even
    // after the table has started rejecting brand-new names.
    EXPECT_EQ(table.AssignOrGetSlot("A"), 0);
    EXPECT_EQ(table.AssignOrGetSlot("B"), 1);
}

TEST(RenderGraphNameSlotTableTests, ZeroBudgetTableRejectsEveryNameImmediately)
{
    RenderGraphNameSlotTable table(0);
    EXPECT_EQ(table.AssignOrGetSlot("Anything"), kNoNameSlot);
    EXPECT_EQ(table.AssignedCount(), 0u);
    EXPECT_EQ(table.SlotBudget(), 0u);
}

TEST(RenderGraphNameSlotTableTests, TwoIndependentTablesNeverCollideOnTheSameName)
{
    // Simulates RenderGraph's own two independent per-regime tables
    // (SynchronousImmediateReadback vs. PipelinedDeferredReadback) - the
    // same pass name assigned in each must resolve completely
    // independently, proving the two regimes' slot ranges are genuinely
    // separate, not merely non-overlapping by accident.
    RenderGraphNameSlotTable synchronousRegime(4);
    RenderGraphNameSlotTable pipelinedRegime(4);

    EXPECT_EQ(synchronousRegime.AssignOrGetSlot("GameView"), 0);
    EXPECT_EQ(synchronousRegime.AssignOrGetSlot("SceneView"), 1);

    EXPECT_EQ(pipelinedRegime.AssignOrGetSlot("Present"), 0);

    // Re-querying "GameView" against the pipelined table's own independent
    // range must NOT return the synchronous table's slot 0 for it - it must
    // be treated as a brand-new name in this table.
    EXPECT_EQ(pipelinedRegime.AssignOrGetSlot("GameView"), 1);

    // Neither table's assignment count/budget is affected by the other.
    EXPECT_EQ(synchronousRegime.AssignedCount(), 2u);
    EXPECT_EQ(pipelinedRegime.AssignedCount(), 2u);
}

} // namespace
} // namespace gte::rg
