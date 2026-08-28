// Unit tests for Phase 5's pure barrier-synthesis decision logic
// (src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h) - entirely Tier-1:
// RequiredStateFor()/RequiresBarrier()/BuildImageMemoryBarrier2()/
// BuildBufferMemoryBarrier2() never touch a live VkDevice/VkCommandBuffer -
// constructing a VkImageMemoryBarrier2/VkBufferMemoryBarrier2 is just
// populating a plain POD struct. EmitImageBarrier()/EmitBufferBarrier() (the
// thin Vulkan-call half - a real vkCmdPipelineBarrier2 call) are
// deliberately NOT exercised here (Tier 2, need a live VkCommandBuffer) -
// see RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md, Step 3.4, for the
// exact coverage list this file follows.
//
// The single most important test in this file is
// RegressionMatchesFrameRecorderPresentPathFieldForField/
// RegressionMatchesFrameRecorderRenderOffscreenPathFieldForField below -
// see that phase's own "Step 5: Their Role" for why: this is the proof that
// migrating FrameRecorder::RecordFrame()'s hand-written barriers onto this
// new, general system (a future phase) cannot silently change rendering
// behavior for the three passes that already work correctly today.
//
// Note on the strategy document's own last 3.4 bullet (preserving
// FrameRecorder::RecordFrame()'s debug-build format-matching ASSERTION,
// not just matching barrier field VALUES): that assertion
// (`target.format == expectedFormat` / `target.depthFormat ==
// expectedDepthFormat`, see FrameRecorder.cpp and AGENTS.md's "Render
// Target Format Matching") lives entirely in Phase 6's future execution
// harness, which is the thing that will actually resolve a pass's
// attachment(s) against a live RenderTarget/Pipeline - this file has no
// such resolution step to assert inside at all (it operates purely on
// already-decided ResourceState values, never a RenderTarget/Pipeline
// format). This is intentionally left as a documented, explicit
// manual-verification note for whoever implements Phase 6 (see this
// engine's AGENTS.md, "Render Target Format Matching", and Phase 5's own
// completion report) rather than a test here that cannot actually exist
// yet.

#include "Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

// --- RequiredStateFor() - one case per ResourceAccess enumerator ----------

TEST(RenderGraphBarrierPlannerTest, RequiredStateForColorAttachmentWrite)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

TEST(RenderGraphBarrierPlannerTest, RequiredStateForDepthStencilAttachmentReadWriteMatchesFrameRecordersOwnDepthBarrier)
{
    // Matches FrameRecorder.cpp's existing `toDepthAttachment` barrier
    // fields exactly - see this phase's own regression-safety requirement.
    const ResourceState state = RequiredStateFor(ResourceAccess::DepthStencilAttachmentReadWrite, true);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(state.stageMask,
        static_cast<VkPipelineStageFlags2>(
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT));
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

TEST(RenderGraphBarrierPlannerTest, RequiredStateForShaderRead)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::ShaderRead, false);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_SHADER_READ_BIT);
}

TEST(RenderGraphBarrierPlannerTest, RequiredStateForTransferSrc)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::TransferSrc, false);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_TRANSFER_READ_BIT);
}

TEST(RenderGraphBarrierPlannerTest, RequiredStateForTransferDst)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::TransferDst, false);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

// --- RequiredStateFor() - Phase 5 of the compute-shader campaign's new -----
// --- enumerators (COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md) -----------

TEST(RenderGraphBarrierPlannerTest, RequiredStateForComputeShaderRead)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::ComputeShaderRead, false);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

TEST(RenderGraphBarrierPlannerTest, RequiredStateForComputeShaderWrite)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    EXPECT_EQ(state.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

TEST(RenderGraphBarrierPlannerTest, RequiredStateForIndirectCommandRead)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::IndirectCommandRead, false);

    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

// --- RequiredStateFor() - GPU Vertex Skinning campaign's own Phase 3 -------
// --- new enumerator (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md) ---

TEST(RenderGraphBarrierPlannerTest, RequiredStateForVertexBufferRead)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::VertexBufferRead, false);

    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
}


// ComputeShaderRead/ComputeShaderWrite must produce the exact SAME
// ResourceState value regardless of the resource kind it's applied against
// (a buffer's `layout` field is simply unused - see BuildBufferMemoryBarrier2()
// and RequiredStateFor()'s own comment on this) - mirroring TransferSrc/
// TransferDst's own existing dual applicability.
TEST(RenderGraphBarrierPlannerTest, ComputeShaderReadAndWriteAreSymmetricApartFromAccessDirection)
{
    const ResourceState readState = RequiredStateFor(ResourceAccess::ComputeShaderRead, false);
    const ResourceState writeState = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    EXPECT_EQ(readState.layout, writeState.layout);
    EXPECT_EQ(readState.stageMask, writeState.stageMask);
    EXPECT_NE(readState.accessMask, writeState.accessMask);
}

// --- TargetsDepthState() - one case per enumerator -------------------------
//
// Confirms, directly (not merely by inspection), that a storage-image
// compute access is routed to a texture's COLOR half, never its depth half -
// see COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md's own Step 3
// ("write a targeted unit test proving it rather than trusting it by
// inspection alone").

TEST(RenderGraphBarrierPlannerTest, TargetsDepthStateIsTrueOnlyForDepthStencilAttachmentReadWrite)
{
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::ColorAttachmentWrite));
    EXPECT_TRUE(TargetsDepthState(ResourceAccess::DepthStencilAttachmentReadWrite));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::ShaderRead));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::TransferSrc));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::TransferDst));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::ComputeShaderRead));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::ComputeShaderWrite));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::IndirectCommandRead));
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::VertexBufferRead));
}

// --- IsColorAttachmentWriteAccess() - one case per enumerator --------------
//
// Phase 6 of the compute-shader campaign
// (COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md) - confirms,
// directly, that a pure compute-shader texture write
// (PassBuilder::WriteTexture(handle, ComputeShaderWrite)) is correctly
// EXCLUDED from RenderGraph::Execute()'s hasColorWrite scan, so a
// compute-only pass never gets a vkCmdBeginRendering bracket at all.

TEST(RenderGraphBarrierPlannerTest, IsColorAttachmentWriteAccessIsTrueOnlyForColorAttachmentWrite)
{
    EXPECT_TRUE(IsColorAttachmentWriteAccess(ResourceAccess::ColorAttachmentWrite));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::DepthStencilAttachmentReadWrite));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::ShaderRead));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::TransferSrc));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::TransferDst));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::ComputeShaderRead));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::ComputeShaderWrite));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::IndirectCommandRead));
    EXPECT_FALSE(IsColorAttachmentWriteAccess(ResourceAccess::VertexBufferRead));
}

// --- Texture-side hand-simulated sequence: ComputeShaderWrite (an ---------
// --- RWTexture) -> ShaderRead (the same texture, sampled normally by a ----
// --- later graphics pass) --------------------------------------------------
//
// The texture-side sibling of the companion GPU-driven-rendering document's
// own buffer-side ComputeShaderWrite -> IndirectCommandRead regression test
// (see COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md, Step 3) - together the
// two prove the ResourceAccess extension generalizes correctly across both
// resource kinds. Confirms exactly one barrier is emitted with
// VK_IMAGE_LAYOUT_GENERAL -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL and the
// matching stage/access masks.
TEST(RenderGraphBarrierPlannerTest, ComputeShaderWriteFollowedByShaderReadEmitsExactlyOneCorrectBarrier)
{
    const ResourceState afterComputeWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState afterShaderRead = RequiredStateFor(ResourceAccess::ShaderRead, false);

    ASSERT_TRUE(RequiresBarrier(afterComputeWrite, afterShaderRead));

    const VkImageMemoryBarrier2 barrier =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, afterComputeWrite, afterShaderRead);

    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_SHADER_READ_BIT);
}

// Two consecutive ComputeShaderWrite usages against the SAME resource (e.g.
// two dispatches within the same declared pass, or two passes that both
// write the same RWTexture back-to-back with no intervening read) need NO
// barrier at all between them - identical layout/stage/access, same
// "already-known-safe redundant transition is skipped" optimization
// RequiresBarrier() already provides for ShaderRead/ShaderRead above.
TEST(RenderGraphBarrierPlannerTest, RequiresBarrierIsFalseForTwoConsecutiveComputeShaderWrites)
{
    const ResourceState a = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState b = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    EXPECT_FALSE(RequiresBarrier(a, b));
}

// --- GPU Vertex Skinning campaign, Phase 3's own WAW-hazard mitigation -----
// (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md, Step 3.6) -
//
// The test immediately above is the CONFIRMED hazard that Step 3.6's own
// "Step 1" verification asked for: two passes writing the SAME buffer with
// the SAME ResourceAccess (e.g. two SkeletalAnimators sharing one GPU
// skinning output buffer, both dispatching ComputeShaderWrite) get NO
// barrier between them at all, since RequiresBarrier() is a pure state-diff
// with no notion of "a different pass already wrote this". This is a real,
// unsynchronized GPU write-after-write hazard - strictly worse than the CPU
// path's own well-defined "last write wins" behavior for the same shared-
// Mesh scenario.
//
// The mitigation (Step 3.6's "Step 2"): the SECOND (and any subsequent)
// writer declares a "phantom" ComputeShaderRead of its own output buffer
// immediately BEFORE its real ComputeShaderWrite - even though its own
// compute shader body never actually reads the buffer's prior contents.
// Since ComputeShaderRead and ComputeShaderWrite are DIFFERENT
// ResourceAccess values (different access masks), RequiredStateFor()
// necessarily produces two DIFFERENT ResourceState values for them - which
// means RequiresBarrier() is GUARANTEED to return true for the transition
// INTO the phantom read (from whatever the previous writer left behind),
// and AGAIN for the transition from that phantom read to the real write -
// closing the hazard unconditionally, regardless of how RequiresBarrier()
// itself happens to be implemented. This is proven directly below, not
// merely asserted by inspection.
TEST(RenderGraphBarrierPlannerTest, ReadBeforeWriteMitigationForcesBarriersWhereConsecutiveWritesAloneWouldNotHaveOne)
{
    // Pass A (the first SkeletalAnimator) writes the shared output buffer.
    const ResourceState afterPassAWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    // Pass B (the second SkeletalAnimator sharing the same Mesh) declares
    // its own mitigation: a phantom read BEFORE its real write.
    const ResourceState passBPhantomRead = RequiredStateFor(ResourceAccess::ComputeShaderRead, false);
    const ResourceState passBRealWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    // Without the mitigation, transitioning straight from Pass A's write to
    // Pass B's write would need NO barrier at all (see the test immediately
    // above) - the mitigation's whole point is that inserting the phantom
    // read in between makes BOTH of these transitions require one.
    EXPECT_TRUE(RequiresBarrier(afterPassAWrite, passBPhantomRead));
    EXPECT_TRUE(RequiresBarrier(passBPhantomRead, passBRealWrite));
}

// The graphics pass that then DRAWS from the GPU-skinned output buffer
// declares ResourceAccess::VertexBufferRead against the very same handle -
// this must also unconditionally require a barrier against whichever
// compute pass most recently wrote it (ComputeShaderWrite), since the two
// access kinds have distinct stage/access masks (VERTEX_INPUT/
// VERTEX_ATTRIBUTE_READ vs. COMPUTE_SHADER/SHADER_STORAGE_WRITE).
TEST(RenderGraphBarrierPlannerTest, ComputeShaderWriteFollowedByVertexBufferReadRequiresABarrier)
{
    const ResourceState afterComputeWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState vertexBufferRead = RequiredStateFor(ResourceAccess::VertexBufferRead, false);

    EXPECT_TRUE(RequiresBarrier(afterComputeWrite, vertexBufferRead));

    const VkBufferMemoryBarrier2 barrier =
        BuildBufferMemoryBarrier2(VK_NULL_HANDLE, 0, 1024, afterComputeWrite, vertexBufferRead);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
}

// --- RequiresBarrier() -----------------------------------------------------

TEST(RenderGraphBarrierPlannerTest, RequiresBarrierIsFalseForTwoIdenticalStates)
{
    const ResourceState a = RequiredStateFor(ResourceAccess::ShaderRead, false);
    const ResourceState b = RequiredStateFor(ResourceAccess::ShaderRead, false);

    EXPECT_FALSE(RequiresBarrier(a, b));
}

TEST(RenderGraphBarrierPlannerTest, RequiresBarrierIsTrueWhenLayoutDiffers)
{
    ResourceState a{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT };
    ResourceState b = a;
    b.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    EXPECT_TRUE(RequiresBarrier(a, b));
}

TEST(RenderGraphBarrierPlannerTest, RequiresBarrierIsTrueWhenStageMaskDiffers)
{
    ResourceState a{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT };
    ResourceState b = a;
    b.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    EXPECT_TRUE(RequiresBarrier(a, b));
}

TEST(RenderGraphBarrierPlannerTest, RequiresBarrierIsTrueWhenAccessMaskDiffersAloneEverythingElseEqual)
{
    ResourceState a{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT };
    ResourceState b = a;
    b.accessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

    EXPECT_TRUE(RequiresBarrier(a, b));
}

// --- Three-pass hand-simulated ping-pong sequence --------------------------
//
// A writes color -> B reads as shader-read -> C writes color again. Starts
// from a resource ALREADY in the ColorAttachmentWrite state (e.g. left over
// from a previous use, the same way an imported RenderTexture is seeded
// per Phase 2's ImportTexture()) - so entering pass A needs no barrier at
// all, and exactly 2 barriers are needed overall (one after A, one after
// B) - not 3 (one per pass boundary) and not 1.
TEST(RenderGraphBarrierPlannerTest, ThreePassPingPongSequenceEmitsExactlyTwoBarriers)
{
    ResourceState current = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);
    int barrierCount = 0;

    // Pass A writes color - already in the exact right state.
    const ResourceState afterA = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);
    if (RequiresBarrier(current, afterA)) {
        ++barrierCount;
    }
    current = afterA;

    // Pass B reads as shader-read - a real transition, needs a barrier.
    const ResourceState afterB = RequiredStateFor(ResourceAccess::ShaderRead, false);
    if (RequiresBarrier(current, afterB)) {
        ++barrierCount;
    }
    const ResourceState stateEnteringB = current; // Captured for the field check below.
    current = afterB;

    // Pass C writes color again - another real transition, needs a barrier.
    const ResourceState afterC = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);
    if (RequiresBarrier(current, afterC)) {
        ++barrierCount;
    }
    const ResourceState stateEnteringC = current; // Captured for the field check below.
    current = afterC;

    EXPECT_EQ(barrierCount, 2);

    const VkImageMemoryBarrier2 barrierAfterA =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, stateEnteringB, afterB);
    EXPECT_EQ(barrierAfterA.srcAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(barrierAfterA.dstAccessMask, VK_ACCESS_2_SHADER_READ_BIT);

    const VkImageMemoryBarrier2 barrierAfterB =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, stateEnteringC, afterC);
    EXPECT_EQ(barrierAfterB.srcAccessMask, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(barrierAfterB.dstAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

// --- BuildImageMemoryBarrier2()/BuildBufferMemoryBarrier2() - plain field population ---

TEST(RenderGraphBarrierPlannerTest, BuildImageMemoryBarrier2PopulatesEveryFieldFromTheGivenStatesAndImage)
{
    const VkImage fakeImage = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0xABCD));
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    const ResourceState previous{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
    const ResourceState next = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);

    const VkImageMemoryBarrier2 barrier = BuildImageMemoryBarrier2(fakeImage, range, previous, next);

    EXPECT_EQ(barrier.sType, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    EXPECT_EQ(barrier.image, fakeImage);
    EXPECT_EQ(barrier.srcStageMask, previous.stageMask);
    EXPECT_EQ(barrier.srcAccessMask, previous.accessMask);
    EXPECT_EQ(barrier.dstStageMask, next.stageMask);
    EXPECT_EQ(barrier.dstAccessMask, next.accessMask);
    EXPECT_EQ(barrier.oldLayout, previous.layout);
    EXPECT_EQ(barrier.newLayout, next.layout);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, static_cast<std::uint32_t>(VK_QUEUE_FAMILY_IGNORED));
    EXPECT_EQ(barrier.dstQueueFamilyIndex, static_cast<std::uint32_t>(VK_QUEUE_FAMILY_IGNORED));
    EXPECT_EQ(barrier.subresourceRange.aspectMask, range.aspectMask);
}

TEST(RenderGraphBarrierPlannerTest, BuildBufferMemoryBarrier2PopulatesEveryFieldFromTheGivenStatesAndBuffer)
{
    const VkBuffer fakeBuffer = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(0x1234));
    const ResourceState previous{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
    const ResourceState next = RequiredStateFor(ResourceAccess::TransferDst, false);

    const VkBufferMemoryBarrier2 barrier = BuildBufferMemoryBarrier2(fakeBuffer, 16, 256, previous, next);

    EXPECT_EQ(barrier.sType, VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    EXPECT_EQ(barrier.buffer, fakeBuffer);
    EXPECT_EQ(barrier.offset, 16u);
    EXPECT_EQ(barrier.size, 256u);
    EXPECT_EQ(barrier.srcStageMask, previous.stageMask);
    EXPECT_EQ(barrier.srcAccessMask, previous.accessMask);
    EXPECT_EQ(barrier.dstStageMask, next.stageMask);
    EXPECT_EQ(barrier.dstAccessMask, next.accessMask);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, static_cast<std::uint32_t>(VK_QUEUE_FAMILY_IGNORED));
    EXPECT_EQ(barrier.dstQueueFamilyIndex, static_cast<std::uint32_t>(VK_QUEUE_FAMILY_IGNORED));
}

// --- Regression: field-for-field match against FrameRecorder.cpp's own ----
// --- existing, hand-written barriers ---------------------------------------
//
// This is the single most important test in this whole phase - see this
// file's own header comment. Every field asserted below is transcribed
// directly from src/Renderer/FrameRecorder.cpp's own
// toColorAttachment/toDepthAttachment/toFinal VkImageMemoryBarrier2 structs.

TEST(RenderGraphBarrierPlannerTest, RegressionMatchesFrameRecordersOwnColorAttachmentEntryBarrier)
{
    // FrameRecorder.cpp's `toColorAttachment`: srcStageMask = TOP_OF_PIPE,
    // srcAccessMask = NONE, dstStageMask = COLOR_ATTACHMENT_OUTPUT,
    // dstAccessMask = COLOR_ATTACHMENT_WRITE, oldLayout = UNDEFINED,
    // newLayout = COLOR_ATTACHMENT_OPTIMAL.
    const ResourceState previous{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
    const ResourceState next = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);

    const VkImageMemoryBarrier2 barrier =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, previous, next);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_NONE);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST(RenderGraphBarrierPlannerTest, RegressionMatchesFrameRecordersOwnDepthAttachmentEntryBarrier)
{
    // FrameRecorder.cpp's `toDepthAttachment`: srcStageMask = TOP_OF_PIPE,
    // srcAccessMask = NONE, dstStageMask = EARLY_FRAGMENT_TESTS |
    // LATE_FRAGMENT_TESTS, dstAccessMask = DEPTH_STENCIL_ATTACHMENT_WRITE,
    // oldLayout = UNDEFINED, newLayout = DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    // (this planner's MVP always uses the combined depth+stencil layout -
    // see RequiredStateFor()'s own comment).
    const ResourceState previous{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
    const ResourceState next = RequiredStateFor(ResourceAccess::DepthStencilAttachmentReadWrite, true);

    const VkImageMemoryBarrier2 barrier =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, previous, next);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_NONE);
    EXPECT_EQ(barrier.dstStageMask,
        static_cast<VkPipelineStageFlags2>(
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT));
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

TEST(RenderGraphBarrierPlannerTest, RegressionMatchesFrameRecordersPresentPathFieldForField)
{
    // FrameRecorder.cpp's `toFinal` when finalLayout ==
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: srcStageMask =
    // COLOR_ATTACHMENT_OUTPUT, srcAccessMask = COLOR_ATTACHMENT_WRITE,
    // dstStageMask = BOTTOM_OF_PIPE, dstAccessMask = NONE, oldLayout =
    // COLOR_ATTACHMENT_OPTIMAL, newLayout = PRESENT_SRC_KHR. There is no
    // ResourceAccess enumerator for "about to be presented" (presenting is
    // not something any pass "reads"/"writes" through the graph's own
    // vocabulary - see RenderGraphTypes.h) - the fixed present-source state
    // is therefore hand-built here exactly as Phase 6's future execution
    // harness will need to for an imported SWAPCHAIN resource specifically,
    // not derived from RequiredStateFor().
    const ResourceState afterColorWrite = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);
    const ResourceState presentSource{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_2_NONE };

    const VkImageMemoryBarrier2 barrier =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, afterColorWrite, presentSource);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_NONE);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

TEST(RenderGraphBarrierPlannerTest, RegressionMatchesFrameRecordersRenderOffscreenPathFieldForField)
{
    // FrameRecorder.cpp's `toFinal` when finalLayout ==
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: srcStageMask =
    // COLOR_ATTACHMENT_OUTPUT, srcAccessMask = COLOR_ATTACHMENT_WRITE,
    // dstStageMask = FRAGMENT_SHADER, dstAccessMask = SHADER_READ,
    // oldLayout = COLOR_ATTACHMENT_OPTIMAL, newLayout =
    // SHADER_READ_ONLY_OPTIMAL - this one DOES come straight from
    // RequiredStateFor(ShaderRead, ...), since sampling a previous pass's
    // output is exactly what ResourceAccess::ShaderRead already models.
    const ResourceState afterColorWrite = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);
    const ResourceState shaderReadSource = RequiredStateFor(ResourceAccess::ShaderRead, false);

    const VkImageMemoryBarrier2 barrier =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, afterColorWrite, shaderReadSource);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// --- isDepthResource assertion guard (debug builds only) -------------------
//
// Guarded by NDEBUG since a release build compiles assert() down to a
// no-op entirely - these death tests would otherwise not actually die. See
// RenderGraphBuilderTests.cpp's own identical guard for this codebase's
// established pattern.
#ifndef NDEBUG

TEST(RenderGraphBarrierPlannerDeathTest, ColorAttachmentWriteRejectsIsDepthResourceTrue)
{
    EXPECT_DEATH({ RequiredStateFor(ResourceAccess::ColorAttachmentWrite, true); }, "");
}

TEST(RenderGraphBarrierPlannerDeathTest, DepthStencilAttachmentReadWriteRejectsIsDepthResourceFalse)
{
    EXPECT_DEATH({ RequiredStateFor(ResourceAccess::DepthStencilAttachmentReadWrite, false); }, "");
}

#endif // !NDEBUG

} // namespace
} // namespace gte::rg
