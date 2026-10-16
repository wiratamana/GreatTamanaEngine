// Tier-1 tests for the pure barrier decision logic in RenderGraphBarrierPlanner.h (no live Vulkan device).
// The Regression* tests pin the planner's output to FrameRecorder.cpp's hand-written barriers field for field.

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
    // Matches the hand-written depth attachment barrier in FrameRecorder.cpp.
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
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

// --- RequiredStateFor() - compute and indirect accesses ---

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

// --- RequiredStateFor() - VertexBufferRead ---

TEST(RenderGraphBarrierPlannerTest, RequiredStateForVertexBufferRead)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::VertexBufferRead, false);

    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
}

// --- RequiredStateFor() - VertexShaderStorageRead ---

TEST(RenderGraphBarrierPlannerTest, RequiredStateForVertexShaderStorageRead)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::VertexShaderStorageRead, false);

    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

// Compute read and write differ only in access mask; layout and stage are equal for buffers and images.
TEST(RenderGraphBarrierPlannerTest, ComputeShaderReadAndWriteAreSymmetricApartFromAccessDirection)
{
    const ResourceState readState = RequiredStateFor(ResourceAccess::ComputeShaderRead, false);
    const ResourceState writeState = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    EXPECT_EQ(readState.layout, writeState.layout);
    EXPECT_EQ(readState.stageMask, writeState.stageMask);
    EXPECT_NE(readState.accessMask, writeState.accessMask);
}

// --- TargetsDepthState() - one case per enumerator ---
// Storage-image compute access must route to the color half, never the depth half.

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
    EXPECT_FALSE(TargetsDepthState(ResourceAccess::VertexShaderStorageRead));
}

// --- Texture-side sequence: ComputeShaderWrite -> ShaderRead ---
// Exactly one barrier, GENERAL -> SHADER_READ_ONLY_OPTIMAL, with the matching stage/access masks.
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
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_SHADER_READ_BIT);
}

// --- Buffer-side sequence: ComputeShaderWrite -> IndirectCommandRead ---
// Exactly one barrier, with DRAW_INDIRECT / INDIRECT_COMMAND_READ as destination.
TEST(RenderGraphBarrierPlannerTest, ComputeShaderWriteFollowedByIndirectCommandReadEmitsExactlyOneCorrectBarrier)
{
    const ResourceState afterComputeWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState afterIndirectRead = RequiredStateFor(ResourceAccess::IndirectCommandRead, false);

    ASSERT_TRUE(RequiresBarrier(afterComputeWrite, afterIndirectRead));

    const VkBufferMemoryBarrier2 barrier =
        BuildBufferMemoryBarrier2(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE, afterComputeWrite, afterIndirectRead);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

// Compute write -> vertex-shader storage read (instanced draw reading a culling output buffer).
TEST(RenderGraphBarrierPlannerTest, ComputeShaderWriteFollowedByVertexShaderStorageReadEmitsExactlyOneCorrectBarrier)
{
    const ResourceState afterComputeWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState afterVertexShaderStorageRead = RequiredStateFor(ResourceAccess::VertexShaderStorageRead, false);

    ASSERT_TRUE(RequiresBarrier(afterComputeWrite, afterVertexShaderStorageRead));

    const VkBufferMemoryBarrier2 barrier =
        BuildBufferMemoryBarrier2(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE, afterComputeWrite, afterVertexShaderStorageRead);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

// Two consecutive ComputeShaderWrite usages of one resource need no barrier: the state is identical.
TEST(RenderGraphBarrierPlannerTest, RequiresBarrierIsFalseForTwoConsecutiveComputeShaderWrites)
{
    const ResourceState a = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState b = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    EXPECT_FALSE(RequiresBarrier(a, b));
}

// --- Write-after-write hazard mitigation ---
// A second writer declares a phantom ComputeShaderRead before its write; the differing access masks force barriers.
TEST(RenderGraphBarrierPlannerTest, ReadBeforeWriteMitigationForcesBarriersWhereConsecutiveWritesAloneWouldNotHaveOne)
{
    // Pass A writes the shared output buffer.
    const ResourceState afterPassAWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    // Pass B declares a phantom read before its real write.
    const ResourceState passBPhantomRead = RequiredStateFor(ResourceAccess::ComputeShaderRead, false);
    const ResourceState passBRealWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

    // Write -> write alone needs no barrier; the phantom read in between makes both transitions need one.
    EXPECT_TRUE(RequiresBarrier(afterPassAWrite, passBPhantomRead));
    EXPECT_TRUE(RequiresBarrier(passBPhantomRead, passBRealWrite));
}

// A vertex-buffer read after a compute write always needs a barrier (distinct stage/access masks).
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

// --- Three-pass ping-pong sequence ---
// Color write -> shader read -> color write, starting in the write state: exactly 2 barriers.
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

// --- Regression: field-for-field match against the hand-written barriers in FrameRecorder.cpp ---
// Every field asserted below is copied from FrameRecorder.cpp's toColorAttachment/toDepthAttachment/toFinal.

TEST(RenderGraphBarrierPlannerTest, RegressionMatchesFrameRecordersOwnColorAttachmentEntryBarrier)
{
    // Color attachment transition: TOP_OF_PIPE/NONE -> COLOR_ATTACHMENT_OUTPUT/COLOR_ATTACHMENT_WRITE.
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
    // Depth attachment transition (the planner always uses the combined depth+stencil layout).
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
    // Final PRESENT_SRC transition: no ResourceAccess models presentation, so the state is hand-built.
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
    // Final SHADER_READ_ONLY transition: comes straight from RequiredStateFor(ShaderRead).
    const ResourceState afterColorWrite = RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false);
    const ResourceState shaderReadSource = RequiredStateFor(ResourceAccess::ShaderRead, false);

    const VkImageMemoryBarrier2 barrier =
        BuildImageMemoryBarrier2(VK_NULL_HANDLE, VkImageSubresourceRange{}, afterColorWrite, shaderReadSource);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// --- DecideTextureArrayLayerTransition() - per-layer state tracking ---
// Writes to different layers of one handle do not alias: each layer's state is tracked independently.
TEST(RenderGraphBarrierPlannerTest, DecideTextureArrayLayerTransitionDoesNotAliasAcrossLayers)
{
    // Two untouched layer states.
    ResourceState layer0State{};
    ResourceState layer1State{};

    const ResourceState next = RequiredStateFor(ResourceAccess::DepthStencilAttachmentReadWrite, /*isDepthResource=*/true);

    const TextureArraySubresourceDecision decision0 =
        DecideTextureArrayLayerTransition(layer0State, VK_IMAGE_ASPECT_DEPTH_BIT, /*layerIndex=*/0, next);
    // Mirrors the real per-layer state update, so decision1 sees the post-update layer0State.
    layer0State = next;

    const TextureArraySubresourceDecision decision1 =
        DecideTextureArrayLayerTransition(layer1State, VK_IMAGE_ASPECT_DEPTH_BIT, /*layerIndex=*/1, next);

    // Layer 1 is independent of layer 0 having transitioned: it still needs a barrier.
    EXPECT_TRUE(decision0.requiresBarrier);
    EXPECT_TRUE(decision1.requiresBarrier);

    EXPECT_EQ(decision0.range.baseArrayLayer, 0u);
    EXPECT_EQ(decision1.range.baseArrayLayer, 1u);
    EXPECT_EQ(decision0.range.layerCount, 1u);
    EXPECT_EQ(decision1.range.layerCount, 1u);
    EXPECT_EQ(decision0.range.aspectMask, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
}

// A second call against the already-updated layer state needs no barrier.
TEST(RenderGraphBarrierPlannerTest, DecideTextureArrayLayerTransitionSkipsRedundantBarrierForSameLayer)
{
    ResourceState layerState{};
    const ResourceState next = RequiredStateFor(ResourceAccess::ShaderRead, false);

    const TextureArraySubresourceDecision first =
        DecideTextureArrayLayerTransition(layerState, VK_IMAGE_ASPECT_COLOR_BIT, 0, next);
    layerState = next;
    const TextureArraySubresourceDecision second =
        DecideTextureArrayLayerTransition(layerState, VK_IMAGE_ASPECT_COLOR_BIT, 0, next);

    EXPECT_TRUE(first.requiresBarrier);
    EXPECT_FALSE(second.requiresBarrier);
}

// --- ShortLayoutLabel() / BarrierTransitionLabel() - Editor Inspector display labels ---

TEST(RenderGraphShortLayoutLabelTest, ReturnsDocumentedLabelForEveryNamedLayout)
{
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_UNDEFINED), "Undefined");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), "RT");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL), "Depth Test");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Shader Read");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL), "Transfer Src");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), "Transfer Dst");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_GENERAL), "General (Storage)");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR), "Present");
}

TEST(RenderGraphShortLayoutLabelTest, FallsBackToOtherForAnyUnlistedLayout)
{
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_PREINITIALIZED), "Other");
    EXPECT_STREQ(ShortLayoutLabel(VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL), "Other");
}

TEST(RenderGraphBarrierTransitionLabelTest, ConcatenatesOldAndNewShortLayoutLabelsWithArrow)
{
    const std::string label =
        BarrierTransitionLabel(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(label, "RT -> Shader Read");
}

TEST(RenderGraphBarrierTransitionLabelTest, ConcatenatesUndefinedToColorAttachmentEntryTransition)
{
    const std::string label = BarrierTransitionLabel(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(label, "Undefined -> RT");
}

// --- isDepthResource assertion guard ---
// Debug only: assert() is compiled out under NDEBUG, so the death tests would not die.
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
