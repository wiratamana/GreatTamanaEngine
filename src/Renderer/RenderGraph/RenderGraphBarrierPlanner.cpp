#include "RenderGraphBarrierPlanner.h"

#include <cassert>

namespace gte::rg {

ResourceState RequiredStateFor(ResourceAccess access, bool isDepthResource) noexcept
{
    // Deliberately NO `default:` case - see this file's header comment and
    // RenderGraphTypes.cpp's IsWriteAccess()/ToString() for the same rule:
    // a future ResourceAccess enumerator added without updating this
    // switch must fail to compile here, never silently fall through.
    switch (access) {
    case ResourceAccess::ColorAttachmentWrite:
        assert(!isDepthResource &&
            "RequiredStateFor: ColorAttachmentWrite requested against a resource flagged as depth - "
            "these two ResourceAccess/isDepthResource combinations are mutually exclusive by construction.");
        return ResourceState{
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        };
    case ResourceAccess::DepthStencilAttachmentReadWrite:
        assert(isDepthResource &&
            "RequiredStateFor: DepthStencilAttachmentReadWrite requested against a resource NOT flagged as "
            "depth - these two ResourceAccess/isDepthResource combinations are mutually exclusive by "
            "construction.");
        // Matches FrameRecorder.cpp's existing `toDepthAttachment` barrier
        // fields exactly (this phase's own load-bearing regression
        // requirement - see RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md,
        // Step 3.4) - dstAccessMask is WRITE only (not READ | WRITE),
        // matching the old hand-written code precisely, even though a
        // depth test both reads (early fragment test) and writes (late
        // fragment test) the attachment.
        return ResourceState{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };
    case ResourceAccess::ShaderRead:
        return ResourceState{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
        };
    case ResourceAccess::TransferSrc:
        return ResourceState{
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
        };
    case ResourceAccess::TransferDst:
        return ResourceState{
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
        };
    }
    return ResourceState{};
}

bool RequiresBarrier(const ResourceState& previous, const ResourceState& next) noexcept
{
    return !(previous == next);
}

VkImageMemoryBarrier2 BuildImageMemoryBarrier2(
    VkImage image, VkImageSubresourceRange subresourceRange, const ResourceState& previous, const ResourceState& next) noexcept
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = previous.stageMask;
    barrier.srcAccessMask = previous.accessMask;
    barrier.dstStageMask = next.stageMask;
    barrier.dstAccessMask = next.accessMask;
    barrier.oldLayout = previous.layout;
    barrier.newLayout = next.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferMemoryBarrier2(
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const ResourceState& previous, const ResourceState& next) noexcept
{
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = previous.stageMask;
    barrier.srcAccessMask = previous.accessMask;
    barrier.dstStageMask = next.stageMask;
    barrier.dstAccessMask = next.accessMask;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size = size;
    return barrier;
}

void EmitImageBarrier(
    VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange subresourceRange, const ResourceState& previous, const ResourceState& next)
{
    VkImageMemoryBarrier2 barrier = BuildImageMemoryBarrier2(image, subresourceRange, previous, next);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void EmitBufferBarrier(
    VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const ResourceState& previous, const ResourceState& next)
{
    VkBufferMemoryBarrier2 barrier = BuildBufferMemoryBarrier2(buffer, offset, size, previous, next);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = 1;
    dependencyInfo.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

} // namespace gte::rg
