// Tier-1 tests: the CPU-side proof that a host write never lands in a ring copy a frame in flight still reads.

#include "Renderer/OffscreenRingGuard.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace gte {
namespace {

// Ticket of the frame with this serial in a ring of `copies` slots; `completed` frames have finished.
OffscreenFrameTicket TicketFor(std::uint64_t serial, std::uint32_t copies, std::uint64_t completed)
{
    return OffscreenFrameTicket{ static_cast<std::uint32_t>((serial - 1) % copies), serial, completed };
}

TEST(RingCopyWritableTest, ASynchronousTicketAlwaysMayWrite)
{
    EXPECT_TRUE(RingCopyWritable(OffscreenFrameTicket{}, 0));
    EXPECT_TRUE(RingCopyWritable(OffscreenFrameTicket{}, 99));
}

TEST(RingCopyWritableTest, AFrameInFlightBlocksTheWrite)
{
    // Frame 3 wants the copy frame 1 used; frame 1 is not complete yet.
    EXPECT_FALSE(RingCopyWritable(TicketFor(3, 2, 0), 1));
    EXPECT_TRUE(RingCopyWritable(TicketFor(3, 2, 1), 1));
    EXPECT_TRUE(RingCopyWritable(TicketFor(3, 2, 2), 1));
}

TEST(RingCopyWritableTest, TheFrameThatOwnsTheCopyMayWriteAgain)
{
    // A frame writes several times (or retries after a failure) with the same serial.
    EXPECT_TRUE(RingCopyWritable(TicketFor(3, 2, 0), 3));
}

TEST(RingCopyUseTest, ClaimingRecordsTheNewestFrame)
{
    RingCopyUse use;
    EXPECT_EQ(use.LastUsedSerial(), 0u);
    EXPECT_TRUE(use.Claim(TicketFor(1, 2, 0)));
    EXPECT_EQ(use.LastUsedSerial(), 1u);
    EXPECT_TRUE(use.Claim(TicketFor(1, 2, 0)));
    EXPECT_EQ(use.LastUsedSerial(), 1u);
}

TEST(RingCopyUseTest, AClaimWhileAnEarlierFrameRunsIsRefusedAndChangesNothing)
{
    RingCopyUse use;
    ASSERT_TRUE(use.Claim(TicketFor(1, 2, 0)));
    EXPECT_FALSE(use.Writable(TicketFor(3, 2, 0)));
    EXPECT_FALSE(use.Claim(TicketFor(3, 2, 0)));
    EXPECT_EQ(use.LastUsedSerial(), 1u);
    EXPECT_TRUE(use.Claim(TicketFor(3, 2, 1)));
    EXPECT_EQ(use.LastUsedSerial(), 3u);
}

TEST(RingCopyUseTest, ASynchronousClaimLeavesTheRecordAlone)
{
    RingCopyUse use;
    ASSERT_TRUE(use.Claim(TicketFor(2, 2, 0)));
    EXPECT_TRUE(use.Claim(OffscreenFrameTicket{}));
    EXPECT_EQ(use.LastUsedSerial(), 2u);
}

TEST(RingCopyUseTest, ANoteNeverRefusesAndKeepsTheNewestSerial)
{
    RingCopyUse use;
    use.Note(TicketFor(5, 2, 0));
    use.Note(TicketFor(3, 2, 0)); // An older frame never lowers the record.
    EXPECT_EQ(use.LastUsedSerial(), 5u);
    use.Note(OffscreenFrameTicket{});
    EXPECT_EQ(use.LastUsedSerial(), 5u);
    EXPECT_FALSE(use.Writable(TicketFor(7, 2, 0)));
    // After a drain every earlier frame is done.
    use.Reset();
    EXPECT_EQ(use.LastUsedSerial(), 0u);
    EXPECT_TRUE(use.Writable(TicketFor(7, 2, 0)));
}

// A ring of any size keeps one record per copy, so a copy is refused only while its own last user runs.
template <std::uint32_t Copies>
void ExpectEveryCopyIsGuardedOnItsOwn()
{
    std::array<RingCopyUse, Copies> uses{};
    for (std::uint64_t serial = 1; serial <= Copies; ++serial) {
        const OffscreenFrameTicket ticket = TicketFor(serial, Copies, 0);
        ASSERT_EQ(RingCopyOf(ticket, Copies), serial - 1);
        EXPECT_TRUE(uses[RingCopyOf(ticket, Copies)].Claim(ticket));
    }
    for (std::uint64_t serial = Copies + 1; serial <= 2 * Copies; ++serial) {
        // The copy of frame `serial - Copies` is busy until that frame completes.
        const OffscreenFrameTicket busy = TicketFor(serial, Copies, serial - Copies - 1);
        const OffscreenFrameTicket done = TicketFor(serial, Copies, serial - Copies);
        EXPECT_FALSE(uses[RingCopyOf(busy, Copies)].Writable(busy));
        EXPECT_TRUE(uses[RingCopyOf(done, Copies)].Claim(done));
    }
}

TEST(RingCopyUseTest, OneTwoAndThreeCopyRingsGuardEachCopyOnItsOwn)
{
    ExpectEveryCopyIsGuardedOnItsOwn<1>();
    ExpectEveryCopyIsGuardedOnItsOwn<2>();
    ExpectEveryCopyIsGuardedOnItsOwn<3>();
}

TEST(RingCopyOfTest, FoldsTheSlotIntoTheRing)
{
    EXPECT_EQ(RingCopyOf(OffscreenFrameTicket{ 1, 2, 0 }, 2), 1u);
    EXPECT_EQ(RingCopyOf(OffscreenFrameTicket{ 1, 2, 0 }, 1), 0u);
    EXPECT_EQ(RingCopyOf(OffscreenFrameTicket{ 5, 6, 0 }, 3), 2u);
    EXPECT_EQ(RingCopyOf(OffscreenFrameTicket{ 1, 2, 0 }, 0), 0u);
}

} // namespace
} // namespace gte
