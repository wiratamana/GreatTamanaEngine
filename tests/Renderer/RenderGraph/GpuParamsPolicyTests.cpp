// Tier-1 tests: the pure upload policy behind GpuParams.

#include "Renderer/RenderGraph/GpuParams.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(GpuParamsPolicyTest, FirstUploadCreatesTheBuffer)
{
    EXPECT_EQ(DecideGpuParamsUpload(0, 144, false), GpuParamsUploadDecision::Create);
}

TEST(GpuParamsPolicyTest, SameSizeOverwrites)
{
    EXPECT_EQ(DecideGpuParamsUpload(144, 144, false), GpuParamsUploadDecision::Overwrite);
}

TEST(GpuParamsPolicyTest, DifferentSizeIsRefused)
{
    EXPECT_EQ(DecideGpuParamsUpload(144, 160, false), GpuParamsUploadDecision::RefuseSizeMismatch);
    EXPECT_EQ(DecideGpuParamsUpload(144, 16, false), GpuParamsUploadDecision::RefuseSizeMismatch);
}

TEST(GpuParamsPolicyTest, PipelinedRegimeIsRefused)
{
    EXPECT_EQ(DecideGpuParamsUpload(0, 144, true), GpuParamsUploadDecision::RefusePipelined);
    EXPECT_EQ(DecideGpuParamsUpload(144, 144, true), GpuParamsUploadDecision::RefusePipelined);
}

TEST(GpuParamsPolicyTest, EmptyUploadIsRefused)
{
    EXPECT_EQ(DecideGpuParamsUpload(0, 0, false), GpuParamsUploadDecision::RefuseEmpty);
    EXPECT_EQ(DecideGpuParamsUpload(144, 0, true), GpuParamsUploadDecision::RefuseEmpty);
}

TEST(GpuParamsPolicyTest, HandleIsInvalidBeforeTheFirstUpload)
{
    const RenderGraphBuilder builder{};
    const GpuParams<float> params;
    EXPECT_EQ(params.Handle(builder), BufferObjectHandle::Invalid);
}

// A ticket for the frame with this serial in a ring of `copies` slots; `completed` frames have finished.
OffscreenFrameTicket TicketFor(std::uint64_t serial, std::uint32_t copies, std::uint64_t completed)
{
    return OffscreenFrameTicket{ static_cast<std::uint32_t>((serial - 1) % copies), serial, completed };
}

TEST(GpuParamsSlotLedgerTest, AnUploadIsVisibleOnlyInItsOwnFrameAndSlot)
{
    GpuParamsSlotLedger ledger;
    const OffscreenFrameTicket frame = TicketFor(1, kGpuFramesInFlight, 0);
    EXPECT_FALSE(ledger.UploadedThisFrame(frame, 7));
    ASSERT_TRUE(ledger.NoteUpload(frame, 7));
    EXPECT_TRUE(ledger.UploadedThisFrame(frame, 7));
    EXPECT_FALSE(ledger.UploadedThisFrame(frame, 8));
    EXPECT_FALSE(ledger.UploadedThisFrame(TicketFor(2, kGpuFramesInFlight, 0), 7));
}

TEST(GpuParamsSlotLedgerTest, SeveralUploadsOfOneFrameHitTheSameCopy)
{
    GpuParamsSlotLedger ledger;
    const OffscreenFrameTicket frame = TicketFor(3, kGpuFramesInFlight, 1);
    EXPECT_TRUE(ledger.NoteUpload(frame, 9));
    EXPECT_TRUE(ledger.NoteUpload(frame, 9));
    EXPECT_TRUE(ledger.NoteUpload(frame, 9));
}

TEST(GpuParamsSlotLedgerTest, ACopyStillReadByAFrameInFlightIsNeverWritten)
{
    BasicGpuParamsSlotLedger<2> ledger;
    ASSERT_TRUE(ledger.NoteUpload(TicketFor(1, 2, 0), 1)); // Frame 1 uses copy 0.
    ASSERT_TRUE(ledger.NoteUpload(TicketFor(2, 2, 0), 2)); // Frame 2 uses copy 1.
    // Frame 3 maps to copy 0 again but frame 1 has not finished.
    const OffscreenFrameTicket early = TicketFor(3, 2, 0);
    EXPECT_FALSE(ledger.Writable(early));
    EXPECT_FALSE(ledger.NoteUpload(early, 3));
    EXPECT_FALSE(ledger.UploadedThisFrame(early, 3));
    // Once frame 1 is complete the same write is allowed.
    const OffscreenFrameTicket late = TicketFor(3, 2, 1);
    EXPECT_TRUE(ledger.Writable(late));
    EXPECT_TRUE(ledger.NoteUpload(late, 3));
}

TEST(GpuParamsSlotLedgerTest, ASynchronousTicketAlwaysWrites)
{
    BasicGpuParamsSlotLedger<2> ledger;
    ASSERT_TRUE(ledger.NoteUpload(TicketFor(1, 2, 0), 1));
    EXPECT_TRUE(ledger.Writable(OffscreenFrameTicket{}));
    EXPECT_TRUE(ledger.NoteUpload(OffscreenFrameTicket{}, 2));
}

TEST(GpuParamsSlotLedgerTest, EveryRingSizeKeepsItsCopiesApart)
{
    BasicGpuParamsSlotLedger<3> ledger;
    for (std::uint64_t serial = 1; serial <= 3; ++serial) {
        EXPECT_TRUE(ledger.NoteUpload(TicketFor(serial, 3, 0), serial));
    }
    // Frame 4 reuses frame 1's copy: refused until frame 1 completes, while copies 1 and 2 stay untouched.
    EXPECT_FALSE(ledger.Writable(TicketFor(4, 3, 0)));
    EXPECT_TRUE(ledger.Writable(TicketFor(4, 3, 1)));
    EXPECT_TRUE(ledger.UploadedThisFrame(TicketFor(2, 3, 0), 2));
    EXPECT_TRUE(ledger.UploadedThisFrame(TicketFor(3, 3, 0), 3));
}

} // namespace
} // namespace gte::rg
