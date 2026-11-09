// Tier-1 tests for the instance buffer ring of the cluster draw state: which buffers are ringed, the per-copy
// content memo, the ledger and the refund on release. No device involved.

#include "Core/FeatureKit/ClusterDrawState.h"
#include "Core/FeatureKit/FrameContext.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace gte::fx {
namespace {

constexpr rg::ViewScope kGame = rg::ViewScope::Primary;
constexpr std::uint32_t kCopies = ClusterDrawState::InstanceRingCopies();

static_assert(kCopies == kGpuFramesInFlight, "the instance ring has one copy per frame in flight");

Registry& FakeScene()
{
    static Registry registry;
    return registry;
}

// A builder-only frame whose offscreen ticket picks one ring copy.
struct RingFrame {
    explicit RingFrame(std::uint64_t frameIndex = 0, std::uint32_t slot = 0, std::uint64_t serial = 0)
    {
        builder.SetOffscreenFrameTicket(OffscreenFrameTicket{ slot, serial, 0 });
        context.builder = &builder;
        context.scope = kGame;
        view.scope = kGame;
        frame = std::make_unique<Frame>(view, FakeScene(), FrameSerial(frameIndex, kGame), &context);
    }

    rg::RenderGraphBuilder builder;
    detail::FrameContext context;
    View view;
    std::unique_ptr<Frame> frame;
};

ClusterDrawState::InstanceContent ContentOf(std::uint64_t epoch, std::uint64_t generation)
{
    ClusterDrawState::InstanceContent content;
    content.group.epoch = epoch;
    content.group.instanceCount = 10;
    content.generation = generation;
    return content;
}

TEST(ClusterInstanceRingTest, OnlyTheHostWrittenInstanceBufferIsRinged)
{
    ClusterDrawState state("TestRingBuffers", { 2, 2 });
    ASSERT_TRUE(state.Valid());
    for (std::uint32_t slot = 0; slot < 2; ++slot) {
        EXPECT_EQ(state.Instances(slot)->Copies(), kCopies);
        EXPECT_EQ(state.History(slot)->Copies(), 1u);
        for (std::uint32_t lane = 0; lane < 2; ++lane) {
            EXPECT_EQ(state.Visible(lane, slot)->Copies(), 1u);
            EXPECT_EQ(state.Commands(lane, slot)->Copies(), 1u);
            EXPECT_EQ(state.Count(lane, slot)->Copies(), 1u);
        }
    }
}

TEST(PersistentBufferRingTest, OnlyAHostWriteBufferGetsCopies)
{
    constexpr BufferUse kHost = BufferUse::Storage | BufferUse::HostWrite;
    EXPECT_EQ(PersistentBuffer::RingCopiesFor(kHost, 2), 2u);
    EXPECT_EQ(PersistentBuffer::RingCopiesFor(kHost, 1), 1u);
    EXPECT_EQ(PersistentBuffer::RingCopiesFor(kHost, 0), 1u);
    EXPECT_EQ(PersistentBuffer::RingCopiesFor(kHost, 100), kGpuFramesInFlight);
    EXPECT_EQ(PersistentBuffer::RingCopiesFor(BufferUse::Storage, 2), 1u);
    EXPECT_EQ(PersistentBuffer::RingCopiesFor(BufferUse::Storage | BufferUse::Indirect, 2), 1u);

    const PersistentBuffer ringed("Test.Ringed", kHost, 2);
    const PersistentBuffer single("Test.Single", BufferUse::Storage, 2);
    EXPECT_EQ(ringed.Copies(), 2u);
    EXPECT_EQ(single.Copies(), 1u);
}

TEST(ClusterInstanceRingTest, TheContentMemoIsKeptPerRingCopy)
{
    ClusterDrawState state("TestRingMemo", { 1, 1 });
    const ClusterDrawState::InstanceContent content = ContentOf(5, 1);

    RingFrame slot0(0, 0, 1);
    RingFrame slot1(1, 1, 2);
    ASSERT_NE(kCopies, 1u);

    state.NoteInstanceContent(*slot0.frame, 0, content);
    EXPECT_TRUE(state.InstanceContentIs(*slot0.frame, 0, content));
    // The other copy was never written: trusting the first copy's memo would draw stale records.
    EXPECT_FALSE(state.InstanceContentIs(*slot1.frame, 0, content));

    state.NoteInstanceContent(*slot1.frame, 0, content);
    EXPECT_TRUE(state.InstanceContentIs(*slot1.frame, 0, content));
    EXPECT_TRUE(state.InstanceContentIs(*slot0.frame, 0, content));

    // A scene change seen by slot 1 invalidates its copy only.
    const ClusterDrawState::InstanceContent changed = ContentOf(6, 1);
    state.NoteInstanceContent(*slot1.frame, 0, changed);
    EXPECT_FALSE(state.InstanceContentIs(*slot1.frame, 0, content));
    EXPECT_TRUE(state.InstanceContentIs(*slot1.frame, 0, changed));
    EXPECT_TRUE(state.InstanceContentIs(*slot0.frame, 0, content));
}

TEST(ClusterInstanceRingTest, AFailedUploadClearsTheMemoOfThatCopyOnly)
{
    ClusterDrawState state("TestRingMemoFail", { 1, 1 });
    const ClusterDrawState::InstanceContent content = ContentOf(5, 1);
    RingFrame slot0(0, 0, 1);
    RingFrame slot1(1, 1, 2);
    state.NoteInstanceContent(*slot0.frame, 0, content);
    state.NoteInstanceContent(*slot1.frame, 0, content);

    state.NoteInstanceContent(*slot1.frame, 0, ClusterDrawState::InstanceContent{});
    EXPECT_FALSE(state.InstanceContentIs(*slot1.frame, 0, content));
    EXPECT_TRUE(state.InstanceContentIs(*slot0.frame, 0, content));
}

TEST(ClusterInstanceRingTest, AGenerationChangeOfOneCopyForcesThatCopyToUpload)
{
    ClusterDrawState state("TestRingMemoGeneration", { 1, 1 });
    RingFrame slot0(0, 0, 1);
    state.NoteInstanceContent(*slot0.frame, 0, ContentOf(5, 1));
    EXPECT_FALSE(state.InstanceContentIs(*slot0.frame, 0, ContentOf(5, 2)));
}

TEST(ClusterInstanceRingTest, TheMemoNeverMatchesTheNonSharedCandidateList)
{
    ClusterDrawState state("TestRingMemoEpoch", { 1, 1 });
    RingFrame slot0(0, 0, 1);
    state.NoteInstanceContent(*slot0.frame, 0, ContentOf(0, 1));
    EXPECT_FALSE(state.InstanceContentIs(*slot0.frame, 0, ContentOf(0, 1)));
}

TEST(ClusterInstanceRingTest, AFrameWithoutAnEngineContextUsesCopyZero)
{
    ClusterDrawState state("TestRingNoContext", { 1, 1 });
    View view;
    view.scope = kGame;
    const Frame bare(view, FakeScene(), FrameSerial(0, kGame));
    EXPECT_FALSE(bare.OffscreenTicket().IsAsync());
    EXPECT_EQ(bare.OffscreenTicket().slot, 0u);
    state.NoteInstanceContent(bare, 0, ContentOf(5, 1));
    EXPECT_TRUE(state.InstanceContentIs(bare, 0, ContentOf(5, 1)));
}

TEST(ClusterInstanceRingTest, TheLedgerChargesEveryInstanceBufferCopy)
{
    constexpr std::uint64_t kInstances = 50000;
    constexpr std::uint64_t kWork = 1100000;
    ClusterDrawState state("TestRingLedger", { 1, 1 });
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 0, kInstances, kWork, false), ClusterReserveResult::Ok);
    const std::uint64_t instances = ClusterBufferCapacity(kInstances * sizeof(ClusterInstanceGpu));
    EXPECT_EQ(state.HeldBytes(kGame), ClusterChainBytes(kInstances, kWork, false) + (kCopies - 1) * instances);
}

TEST(ClusterInstanceRingTest, ReleasingAnIdleScopeRefundsEveryCopy)
{
    ClusterDrawState state("TestRingRefund", { 1, 1 });
    const std::uint64_t before = ProcessClusterLedger().Total();
    ASSERT_EQ(state.TryReserveChainBytes(kGame, 0, 0, 50000, 1100000, false), ClusterReserveResult::Ok);
    const std::uint64_t held = state.HeldBytes(kGame);
    ASSERT_GT(held, 0u);
    EXPECT_EQ(ProcessClusterLedger().Total() - before, held);

    RingFrame first(0);
    state.NoteFrame(*first.frame, true);
    RingFrame later(kClusterIdleReleaseFrames + 1);
    // No buffer exists on this device-less run, so the release has nothing to wait for.
    ASSERT_TRUE(state.ReleaseIdleScope(*later.frame));
    EXPECT_EQ(state.HeldBytes(kGame), 0u);
    EXPECT_EQ(ProcessClusterLedger().Total(), before);
}

} // namespace
} // namespace gte::fx
