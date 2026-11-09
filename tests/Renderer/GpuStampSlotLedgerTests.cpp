// Which frame slots hold GPU stamps that were recorded and not read yet, and where each slot's queries start.

#include "Renderer/GpuStampSlotLedger.h"
#include "Renderer/GpuTiming.h"
#include "Renderer/RenderGraph/RenderGraphTimestampPool.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte {
namespace {

TEST(GpuStampSlotLedgerTest, ASlotStartsEmpty)
{
    BasicGpuStampSlotLedger<2> ledger;
    EXPECT_FALSE(ledger.Pending(0));
    EXPECT_FALSE(ledger.Pending(1));
    EXPECT_FALSE(ledger.TakePending(0));
}

TEST(GpuStampSlotLedgerTest, RecordedStampsCanBeTakenOnce)
{
    BasicGpuStampSlotLedger<2> ledger;
    ledger.MarkPending(1);
    EXPECT_TRUE(ledger.Pending(1));
    EXPECT_TRUE(ledger.TakePending(1));
    EXPECT_FALSE(ledger.Pending(1));
    EXPECT_FALSE(ledger.TakePending(1));
}

TEST(GpuStampSlotLedgerTest, SlotsAreIndependent)
{
    BasicGpuStampSlotLedger<3> ledger;
    ledger.MarkPending(0);
    ledger.MarkPending(2);
    EXPECT_TRUE(ledger.TakePending(0));
    EXPECT_FALSE(ledger.Pending(1));
    EXPECT_TRUE(ledger.Pending(2));
}

TEST(GpuStampSlotLedgerTest, RecordingOverUnreadStampsReportsALostFrameOnce)
{
    BasicGpuStampSlotLedger<2> ledger;
    EXPECT_FALSE(ledger.BeginRecord(0)); // Nothing was waiting.
    ledger.MarkPending(0);
    EXPECT_TRUE(ledger.BeginRecord(0));  // The earlier frame in this slot was never read.
    EXPECT_FALSE(ledger.Pending(0));
    EXPECT_FALSE(ledger.BeginRecord(0));
}

TEST(GpuStampSlotLedgerTest, ReadingTheStampsBeforeTheSlotIsReusedLosesNothing)
{
    BasicGpuStampSlotLedger<2> ledger;
    ledger.MarkPending(1);
    ASSERT_TRUE(ledger.TakePending(1));
    EXPECT_FALSE(ledger.BeginRecord(1));
}

TEST(GpuStampSlotLedgerTest, ADroppedFrameIsTakenLikeARead)
{
    BasicGpuStampSlotLedger<2> ledger;
    ledger.MarkPending(0);
    EXPECT_TRUE(ledger.TakePending(0)); // A frame that failed before its submit gives its stamps up.
    EXPECT_FALSE(ledger.TakePending(0));
}

TEST(GpuStampSlotLedgerTest, AnUnknownSlotIsIgnored)
{
    BasicGpuStampSlotLedger<2> ledger;
    ledger.MarkPending(2);
    EXPECT_FALSE(ledger.Pending(2));
    EXPECT_FALSE(ledger.TakePending(2));
    EXPECT_FALSE(ledger.BeginRecord(7));
    EXPECT_FALSE(ledger.Pending(0));
    EXPECT_FALSE(ledger.Pending(1));
}

TEST(GpuStampSlotLedgerTest, AnySlotCountWorks)
{
    BasicGpuStampSlotLedger<1> one;
    one.MarkPending(0);
    EXPECT_TRUE(one.TakePending(0));

    BasicGpuStampSlotLedger<kGpuFramesInFlight> ring;
    for (std::uint32_t slot = 0; slot < kGpuFramesInFlight; ++slot) {
        ring.MarkPending(slot);
    }
    for (std::uint32_t slot = 0; slot < kGpuFramesInFlight; ++slot) {
        EXPECT_TRUE(ring.TakePending(slot));
    }
}

TEST(TimestampRegionBaseTest, EachSlotStartsAfterTheRegionOfTheSlotBefore)
{
    constexpr std::uint32_t kQueries = 1028;
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(rg::TimestampRegionBase(kQueries, slot), slot * kQueries);
    }
}

TEST(TimestampRegionBaseTest, RegionsOfDifferentSlotsNeverOverlap)
{
    for (const std::uint32_t queries : { 1u, 7u, 1028u }) {
        for (std::uint32_t slots = 1; slots <= 3; ++slots) {
            for (std::uint32_t slot = 1; slot < slots; ++slot) {
                EXPECT_EQ(rg::TimestampRegionBase(queries, slot) - rg::TimestampRegionBase(queries, slot - 1), queries);
            }
        }
    }
}

TEST(TimestampRegionBaseTest, AnUnsupportedPoolHasNoRegions)
{
    EXPECT_EQ(rg::TimestampRegionBase(0, 0), 0u);
    EXPECT_EQ(rg::TimestampRegionBase(0, 1), 0u);
}

} // namespace
} // namespace gte
