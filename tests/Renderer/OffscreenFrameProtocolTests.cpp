// Wait rules of the offscreen frame ring with fake waiters: only a Completed wait may finish a slot or free
// retired objects. No device is involved.

#include "Renderer/OffscreenFrameProtocol.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
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

// Answers every slot wait with `outcome` and remembers which slots were waited on.
struct FakeSlotWaiter {
    BlockOutcome outcome = BlockOutcome::Completed;
    std::vector<std::uint32_t> waited;

    BlockOutcome operator()(std::uint32_t slot)
    {
        waited.push_back(slot);
        return outcome;
    }
};

// Answers a drain with `outcome` and remembers the slots it was asked to wait for.
struct FakeDrainWaiter {
    BlockOutcome outcome = BlockOutcome::Completed;
    std::vector<std::vector<std::uint32_t>> calls;

    BlockOutcome operator()(std::span<const std::uint32_t> slots)
    {
        calls.emplace_back(slots.begin(), slots.end());
        return outcome;
    }
};

// Opens and submits one frame, as a frame that reached the queue does.
OffscreenFrameTicket SubmitOneFrame(OffscreenFrameProtocol& protocol, FakeSlotWaiter& waiter)
{
    const std::optional<OffscreenFrameTicket> ticket = protocol.BeginFrame(waiter);
    EXPECT_TRUE(ticket.has_value());
    protocol.NoteSubmitted();
    return ticket.value_or(OffscreenFrameTicket{});
}

TEST(OffscreenFrameProtocolTest, TheFirstFramesOfARingNeedNoWait)
{
    for (std::uint32_t slots = 1; slots <= 3; ++slots) {
        OffscreenFrameProtocol protocol(slots);
        FakeSlotWaiter waiter;
        for (std::uint32_t frame = 0; frame < slots; ++frame) {
            const OffscreenFrameTicket ticket = SubmitOneFrame(protocol, waiter);
            EXPECT_EQ(ticket.slot, frame);
            EXPECT_EQ(ticket.serial, frame + 1u);
        }
        EXPECT_TRUE(waiter.waited.empty());
    }
}

TEST(OffscreenFrameProtocolTest, ASteadyFrameWaitsOnceOnTheSlotItReuses)
{
    for (std::uint32_t slots = 1; slots <= 3; ++slots) {
        OffscreenFrameProtocol protocol(slots);
        FakeSlotWaiter waiter;
        const std::uint32_t frames = slots * 3;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const OffscreenFrameTicket ticket = SubmitOneFrame(protocol, waiter);
            EXPECT_EQ(ticket.slot, frame % slots);
        }
        ASSERT_EQ(waiter.waited.size(), frames - slots);
        for (std::size_t i = 0; i < waiter.waited.size(); ++i) {
            EXPECT_EQ(waiter.waited[i], (i + slots) % slots);
        }
    }
}

TEST(OffscreenFrameProtocolTest, ARepeatedBeginInTheSameFrameDoesNotWaitAgain)
{
    OffscreenFrameProtocol protocol(2);
    FakeSlotWaiter waiter;
    SubmitOneFrame(protocol, waiter);
    SubmitOneFrame(protocol, waiter);

    const std::optional<OffscreenFrameTicket> first = protocol.BeginFrame(waiter);
    const std::optional<OffscreenFrameTicket> again = protocol.BeginFrame(waiter);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->slot, first->slot);
    EXPECT_EQ(again->serial, first->serial);
    EXPECT_EQ(waiter.waited.size(), 1u);
}

TEST(OffscreenFrameProtocolTest, ASkippedFrameKeepsItsSlotAndTheNextBeginStillWaitsOnTheFrameBefore)
{
    OffscreenFrameProtocol protocol(2);
    FakeSlotWaiter waiter;
    SubmitOneFrame(protocol, waiter); // Serial 1, slot 0.
    SubmitOneFrame(protocol, waiter); // Serial 2, slot 1.

    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value()); // Serial 3 waits for slot 0 and is then skipped.
    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value()); // The retry keeps the same frame.
    EXPECT_EQ(waiter.waited, std::vector<std::uint32_t>({ 0 }));

    protocol.NoteSubmitted();
    EXPECT_EQ(protocol.Current().serial, 4u);
    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value());
    EXPECT_EQ(waiter.waited, std::vector<std::uint32_t>({ 0, 1 }));
}

TEST(OffscreenFrameProtocolTest, ATimedOutSlotWaitOpensNothingAndFreesNothing)
{
    OffscreenFrameProtocol protocol(2);
    FakeSlotWaiter waiter;
    int alive = 0;
    SubmitOneFrame(protocol, waiter);
    protocol.SetOverlapped(true);
    protocol.Retire(Tracked(&alive)); // Tagged with the frame about to be built (serial 2).
    SubmitOneFrame(protocol, waiter);

    waiter.outcome = BlockOutcome::TimedOut;
    EXPECT_FALSE(protocol.BeginFrame(waiter).has_value());
    EXPECT_FALSE(protocol.Ring().FrameOpen());
    EXPECT_EQ(protocol.Ring().CompletedSerial(), 0u);
    EXPECT_EQ(protocol.RetiredPending(), 1u);
    EXPECT_EQ(alive, 1);
}

TEST(OffscreenFrameProtocolTest, ALostDeviceDuringTheSlotWaitIsNotCompletionEither)
{
    for (const BlockOutcome outcome : { BlockOutcome::DeviceLost, BlockOutcome::Failed }) {
        OffscreenFrameProtocol protocol(1);
        FakeSlotWaiter waiter;
        int alive = 0;
        SubmitOneFrame(protocol, waiter);
        protocol.SetOverlapped(true);
        protocol.Retire(Tracked(&alive));

        waiter.outcome = outcome;
        EXPECT_FALSE(protocol.BeginFrame(waiter).has_value());
        EXPECT_EQ(protocol.Ring().CompletedSerial(), 0u);
        EXPECT_TRUE(protocol.Ring().SlotPending(0));
        EXPECT_EQ(alive, 1);
    }
}

TEST(OffscreenFrameProtocolTest, AFailedSlotWaitIsRetriedByTheNextBegin)
{
    OffscreenFrameProtocol protocol(1);
    FakeSlotWaiter waiter;
    SubmitOneFrame(protocol, waiter);

    waiter.outcome = BlockOutcome::TimedOut;
    EXPECT_FALSE(protocol.BeginFrame(waiter).has_value());
    waiter.outcome = BlockOutcome::Completed;
    EXPECT_TRUE(protocol.BeginFrame(waiter).has_value());
    EXPECT_EQ(waiter.waited.size(), 2u);
    EXPECT_EQ(protocol.Ring().CompletedSerial(), 1u);
}

TEST(OffscreenFrameProtocolTest, ACompletedSlotWaitFreesOnlyWhatFinishedFramesTagged)
{
    OffscreenFrameProtocol protocol(2);
    FakeSlotWaiter waiter;
    protocol.SetOverlapped(true);
    int alive = 0;

    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value());
    protocol.Retire(Tracked(&alive)); // Tag 1: frame 1 may still use it.
    protocol.NoteSubmitted();
    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value());
    protocol.Retire(Tracked(&alive)); // Tag 2.
    protocol.NoteSubmitted();
    EXPECT_EQ(alive, 2);

    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value()); // Waits for frame 1.
    EXPECT_EQ(alive, 1);
    EXPECT_EQ(protocol.RetiredPending(), 1u);

    protocol.NoteSubmitted();
    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value()); // Waits for frame 2.
    EXPECT_EQ(alive, 0);
}

TEST(OffscreenFrameProtocolTest, WithoutOverlapAnObjectIsFreedByTheFirstCompletedWait)
{
    OffscreenFrameProtocol protocol(2);
    int alive = 0;
    protocol.Retire(Tracked(&alive));
    EXPECT_EQ(alive, 1);
    protocol.CollectAfterCompletedWait();
    EXPECT_EQ(alive, 0);
}

TEST(OffscreenFrameProtocolTest, DrainAsksForEveryPendingSlotAndCompletesThemAll)
{
    for (std::uint32_t slots = 1; slots <= 3; ++slots) {
        OffscreenFrameProtocol protocol(slots);
        FakeSlotWaiter slotWaiter;
        for (std::uint32_t frame = 0; frame < slots; ++frame) {
            SubmitOneFrame(protocol, slotWaiter);
        }

        FakeDrainWaiter drain;
        EXPECT_EQ(protocol.DrainAll(drain), BlockOutcome::Completed);
        ASSERT_EQ(drain.calls.size(), 1u);
        EXPECT_EQ(drain.calls[0].size(), slots);
        EXPECT_FALSE(protocol.Ring().AnyPending());
        EXPECT_EQ(protocol.Ring().CompletedSerial(), protocol.Ring().Submitted());
    }
}

TEST(OffscreenFrameProtocolTest, ADrainThatDoesNotCompleteNotesNothingAndFreesNothing)
{
    for (const BlockOutcome outcome : { BlockOutcome::TimedOut, BlockOutcome::DeviceLost, BlockOutcome::Failed }) {
        OffscreenFrameProtocol protocol(2);
        FakeSlotWaiter slotWaiter;
        int alive = 0;
        protocol.SetOverlapped(true);
        ASSERT_TRUE(protocol.BeginFrame(slotWaiter).has_value());
        protocol.Retire(Tracked(&alive));
        protocol.NoteSubmitted();

        FakeDrainWaiter drain;
        drain.outcome = outcome;
        EXPECT_EQ(protocol.DrainAll(drain), outcome);
        EXPECT_TRUE(protocol.Ring().AnyPending());
        EXPECT_EQ(protocol.Ring().CompletedSerial(), 0u);
        EXPECT_EQ(protocol.RetiredPending(), 1u);
        EXPECT_EQ(alive, 1);
    }
}

TEST(OffscreenFrameProtocolTest, ACompletedDrainFreesTheRetiredObjectsOfEveryFrame)
{
    OffscreenFrameProtocol protocol(2);
    FakeSlotWaiter slotWaiter;
    int alive = 0;
    protocol.SetOverlapped(true);
    for (int frame = 0; frame < 2; ++frame) {
        ASSERT_TRUE(protocol.BeginFrame(slotWaiter).has_value());
        protocol.Retire(Tracked(&alive));
        protocol.NoteSubmitted();
    }

    FakeDrainWaiter drain;
    EXPECT_EQ(protocol.DrainAll(drain), BlockOutcome::Completed);
    EXPECT_EQ(alive, 0);
    EXPECT_EQ(protocol.RetiredPending(), 0u);
}

TEST(OffscreenFrameProtocolTest, DrainingAnIdleRingAsksForNoSlots)
{
    OffscreenFrameProtocol protocol(2);
    FakeDrainWaiter drain;
    EXPECT_EQ(protocol.DrainAll(drain), BlockOutcome::Completed);
    ASSERT_EQ(drain.calls.size(), 1u);
    EXPECT_TRUE(drain.calls[0].empty());
}

TEST(OffscreenFrameProtocolTest, ALostFrameKeepsItsSerialUntilARealSubmit)
{
    OffscreenFrameProtocol protocol(2);
    FakeSlotWaiter waiter;
    const std::uint64_t before = protocol.Current().serial;
    ASSERT_TRUE(protocol.BeginFrame(waiter).has_value()); // Opened, then dropped without a submit.
    EXPECT_EQ(protocol.Current().serial, before);
    EXPECT_EQ(protocol.Ring().Submitted(), 0u);
}

} // namespace
} // namespace gte
