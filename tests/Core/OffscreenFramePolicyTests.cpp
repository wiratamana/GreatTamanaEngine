// Which offscreen path a frame takes and who takes the slot, plus the update-then-build protocol of Core
// played against a fake slot waiter. No device is involved.

#include "Core/OffscreenFramePolicy.h"
#include "Renderer/GpuTiming.h"
#include "Renderer/OffscreenFrameProtocol.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gte {
namespace {

TEST(OffscreenFramePolicyTest, OneFrameInFlightKeepsTheLegacyPath)
{
    EXPECT_EQ(ChooseOffscreenRecordingPath(false, false), OffscreenRecordingPath::Legacy);
    EXPECT_EQ(ChooseOffscreenRecordingPath(false, true), OffscreenRecordingPath::Legacy);
}

TEST(OffscreenFramePolicyTest, OverlappedFramesRecordIntoTheSlotBufferUnlessACaptureIsArmed)
{
    EXPECT_EQ(ChooseOffscreenRecordingPath(true, false), OffscreenRecordingPath::Async);
    EXPECT_EQ(ChooseOffscreenRecordingPath(true, true), OffscreenRecordingPath::Legacy);
}

TEST(OffscreenFramePolicyTest, WithoutOverlapTheCpuWritesTargetTheSynchronousSlot)
{
    const std::optional<OffscreenFrameTicket> noTicket;
    const std::optional<OffscreenFrameTicket> stale = OffscreenFrameTicket{ 1, 7, 6 };
    for (const std::optional<OffscreenFrameTicket>& ticket : { noTicket, stale }) {
        const std::optional<OffscreenFrameTicket> write = WriteTicketFor(false, ticket);
        ASSERT_TRUE(write.has_value());
        EXPECT_FALSE(write->IsAsync());
        EXPECT_EQ(write->slot, 0u);
    }
}

TEST(OffscreenFramePolicyTest, WithOverlapTheCpuWritesTargetTheTicketOrNothing)
{
    const OffscreenFrameTicket ticket{ 1, 7, 6 };
    const std::optional<OffscreenFrameTicket> write = WriteTicketFor(true, ticket);
    ASSERT_TRUE(write.has_value());
    EXPECT_EQ(write->slot, 1u);
    EXPECT_EQ(write->serial, 7u);
    EXPECT_FALSE(WriteTicketFor(true, std::nullopt).has_value());
}

TEST(OffscreenFramePolicyTest, TheBuildTakesTheSlotOnlyWhenTheUpdateNeverTried)
{
    EXPECT_TRUE(BuildMustAcquireSlot(false, false));
    EXPECT_FALSE(BuildMustAcquireSlot(false, true)); // The wait failed: do not wait again this frame.
    EXPECT_FALSE(BuildMustAcquireSlot(true, true));
    EXPECT_FALSE(BuildMustAcquireSlot(true, false));
}

// One loop iteration the way Core runs it: Update() takes the slot, BuildFrame() takes it only as a fallback.
struct CoreFrameSimulator {
    OffscreenFrameProtocol protocol{ kGpuFramesInFlight };
    std::vector<std::uint32_t> waited;
    BlockOutcome outcome = BlockOutcome::Completed;
    std::optional<OffscreenFrameTicket> ticket;
    bool slotWaitAttempted = false;

    std::optional<OffscreenFrameTicket> Acquire()
    {
        ticket = protocol.BeginFrame([this](std::uint32_t slot) {
            waited.push_back(slot);
            return outcome;
        });
        return ticket;
    }

    void Update()
    {
        slotWaitAttempted = true;
        Acquire();
    }

    // Returns true when the frame reached the queue.
    bool Build()
    {
        const bool attempted = std::exchange(slotWaitAttempted, false);
        if (BuildMustAcquireSlot(ticket.has_value(), attempted)) {
            Acquire();
        }
        if (!ticket.has_value()) {
            return false;
        }
        protocol.NoteSubmitted();
        ticket.reset();
        return true;
    }
};

TEST(CoreFrameProtocolTest, UpdateTakesTheSlotAndBuildDoesNotWaitAgain)
{
    CoreFrameSimulator core;
    for (int frame = 0; frame < 6; ++frame) {
        core.Update();
        EXPECT_TRUE(core.Build());
    }
    // The first frames of the ring are free; every later frame waits exactly once.
    EXPECT_EQ(core.waited.size(), 6u - kGpuFramesInFlight);
}

TEST(CoreFrameProtocolTest, AHostThatNeverUpdatesStillWaitsOncePerFrameInBuild)
{
    CoreFrameSimulator core;
    for (int frame = 0; frame < 6; ++frame) {
        EXPECT_TRUE(core.Build());
    }
    EXPECT_EQ(core.waited.size(), 6u - kGpuFramesInFlight);
}

TEST(CoreFrameProtocolTest, AFailedSlotWaitSkipsTheFrameAndNothingMoves)
{
    CoreFrameSimulator core;
    for (std::uint32_t frame = 0; frame < kGpuFramesInFlight; ++frame) {
        core.Update();
        ASSERT_TRUE(core.Build());
    }
    const std::uint64_t submitted = core.protocol.Ring().Submitted();

    core.outcome = BlockOutcome::TimedOut;
    core.Update();
    EXPECT_FALSE(core.Build()); // The build does not repeat the failed wait.
    EXPECT_EQ(core.waited.size(), 1u);
    EXPECT_EQ(core.protocol.Ring().Submitted(), submitted);
    EXPECT_EQ(core.protocol.Ring().CompletedSerial(), 0u);
}

TEST(CoreFrameProtocolTest, ASkippedBuildKeepsTheSlotForTheNextFrame)
{
    CoreFrameSimulator core;
    for (std::uint32_t frame = 0; frame < kGpuFramesInFlight; ++frame) {
        core.Update();
        ASSERT_TRUE(core.Build());
    }
    core.Update(); // Waits for the slot and opens the frame.
    const OffscreenFrameTicket skipped = *core.ticket;
    core.ticket.reset(); // The build bails out before its submit; the frame stays open in the ring.
    core.slotWaitAttempted = false;

    core.Update(); // The next loop iteration repeats the begin without a second wait.
    ASSERT_TRUE(core.ticket.has_value());
    EXPECT_EQ(core.ticket->slot, skipped.slot);
    EXPECT_EQ(core.ticket->serial, skipped.serial);
    EXPECT_EQ(core.waited.size(), 1u);
}

TEST(CoreFrameProtocolTest, EveryRingSizeStaysWithinWhatTheRingCanHold)
{
    static_assert(kGpuFramesInFlight <= OffscreenFrameRing::kMaxSlots);
    EXPECT_LE(kGpuFramesInFlight, OffscreenFrameRing::kMaxSlots);
}

} // namespace
} // namespace gte
