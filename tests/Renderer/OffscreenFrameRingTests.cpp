// Slot, serial and completion rules of the offscreen frame ring, and the frames-in-flight option.

#include "Renderer/OffscreenFrameRing.h"
#include "Renderer/OffscreenFramesInFlight.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(OffscreenFrameRingTest, SlotFollowsTheRealSubmitsForAnySlotCount)
{
    for (std::uint32_t slots = 1; slots <= 3; ++slots) {
        OffscreenFrameRing ring(slots);
        for (std::uint64_t submit = 0; submit < 7; ++submit) {
            const OffscreenFrameTicket ticket = ring.Current();
            EXPECT_EQ(ticket.slot, submit % slots);
            EXPECT_EQ(ticket.serial, submit + 1);
            ring.OpenFrame();
            ring.NoteSubmitted();
        }
    }
}

TEST(OffscreenFrameRingTest, ASkippedFrameKeepsItsTicket)
{
    OffscreenFrameRing ring(2);
    ring.OpenFrame();
    const OffscreenFrameTicket first = ring.Current();
    EXPECT_TRUE(ring.FrameOpen());
    EXPECT_EQ(ring.Current().slot, first.slot);
    EXPECT_EQ(ring.Current().serial, first.serial);
    EXPECT_EQ(ring.Submitted(), 0u);
}

TEST(OffscreenFrameRingTest, SubmitClosesTheFrameAndMarksItsSlotPending)
{
    OffscreenFrameRing ring(2);
    ring.OpenFrame();
    ring.NoteSubmitted();
    EXPECT_FALSE(ring.FrameOpen());
    EXPECT_TRUE(ring.SlotPending(0));
    EXPECT_FALSE(ring.SlotPending(1));
    EXPECT_TRUE(ring.AnyPending());
}

TEST(OffscreenFrameRingTest, WaitingOneSlotCompletesItAndEveryEarlierFrame)
{
    OffscreenFrameRing ring(2);
    for (int i = 0; i < 3; ++i) {
        ring.OpenFrame();
        ring.NoteSubmitted();
    }
    // Frames 1 and 3 sit in slot 0, frame 2 in slot 1.
    EXPECT_TRUE(ring.SlotPending(0));
    EXPECT_TRUE(ring.SlotPending(1));
    ring.NoteSlotComplete(1);
    EXPECT_EQ(ring.CompletedSerial(), 2u);
    EXPECT_TRUE(ring.SlotPending(0));
    EXPECT_FALSE(ring.SlotPending(1));
    EXPECT_EQ(ring.Current().completedSerial, 2u);
}

TEST(OffscreenFrameRingTest, NoteAllCompleteClearsEverySlot)
{
    OffscreenFrameRing ring(2);
    for (int i = 0; i < 2; ++i) {
        ring.OpenFrame();
        ring.NoteSubmitted();
    }
    ring.NoteAllComplete();
    EXPECT_FALSE(ring.AnyPending());
    EXPECT_EQ(ring.CompletedSerial(), ring.Submitted());
}

TEST(OffscreenFrameTicketTest, SynchronousTicketIsNotAsync)
{
    EXPECT_FALSE(OffscreenFrameTicket{}.IsAsync());
    OffscreenFrameRing ring(2);
    EXPECT_TRUE(ring.Current().IsAsync());
}

TEST(OffscreenFramesInFlightTest, ParsesOnlyValidCounts)
{
    EXPECT_EQ(ParseOffscreenFramesInFlight("1"), 1u);
    EXPECT_EQ(ParseOffscreenFramesInFlight("2"), 2u);
    EXPECT_FALSE(ParseOffscreenFramesInFlight("0").has_value());
    EXPECT_FALSE(ParseOffscreenFramesInFlight("3").has_value());
    EXPECT_FALSE(ParseOffscreenFramesInFlight("").has_value());
    EXPECT_FALSE(ParseOffscreenFramesInFlight("12").has_value());
    EXPECT_FALSE(ParseOffscreenFramesInFlight("x").has_value());
}

TEST(OffscreenFramesInFlightTest, DefaultKeepsTheSerialPath)
{
    EXPECT_EQ(kDefaultOffscreenFramesInFlight, 1u);
}

} // namespace
} // namespace gte
