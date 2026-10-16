#include "RenderGraphBarrierPlanner.h"

#include <cassert>

namespace gte::rg {

ResourceState RequiredStateFor(ResourceAccess access, bool isDepthResource) noexcept
{
    // No `default:` on purpose: a new ResourceAccess must fail to compile here.
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
        // Access mask is WRITE only (not READ | WRITE), matching the established depth-attachment barrier.
        return ResourceState{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };
    case ResourceAccess::ShaderRead:
        return ResourceState{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
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
    case ResourceAccess::ComputeShaderRead:
        // Textures: GENERAL is the only layout a storage image can be read/written in. Buffers ignore layout.
        return ResourceState{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        };
    case ResourceAccess::ComputeShaderWrite:
        // Same layout/stage as ComputeShaderRead, write access.
        return ResourceState{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        };
    case ResourceAccess::IndirectCommandRead:
        // Buffer-only; layout is unused.
        return ResourceState{
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
        };
    case ResourceAccess::VertexBufferRead:
        // Buffer-only; layout is unused. Sync against a compute pass that wrote the same vertex buffer
        // (e.g. GPU skinning) via the vertex-input stage.
        return ResourceState{
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        };
    case ResourceAccess::VertexShaderStorageRead:
        // Buffer-only; layout is unused. Distinct from ShaderRead: a StructuredBuffer read by the vertex stage.
        return ResourceState{
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        };
    }
    return ResourceState{};
}

bool TargetsDepthState(ResourceAccess access) noexcept
{
    // The only access that targets a texture's depth half; everything else targets color.
    return access == ResourceAccess::DepthStencilAttachmentReadWrite;
}

bool RequiresBarrier(const ResourceState& previous, const ResourceState& next) noexcept
{
    return !(previous == next);
}

TextureArraySubresourceDecision DecideTextureArrayLayerTransition(
    const ResourceState& layerState, VkImageAspectFlags aspect, std::uint32_t layerIndex,
    const ResourceState& next) noexcept
{
    TextureArraySubresourceDecision decision;
    decision.requiresBarrier = RequiresBarrier(layerState, next);
    decision.range = VkImageSubresourceRange{ aspect, 0, 1, layerIndex, 1 };
    return decision;
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
    VkBuffer buffer, uint64_t offset, uint64_t size, const ResourceState& previous, const ResourceState& next) noexcept
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
    barrier.offset = static_cast<VkDeviceSize>(offset);
    barrier.size = static_cast<VkDeviceSize>(size);
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
    VkCommandBuffer cmd, VkBuffer buffer, uint64_t offset, uint64_t size, const ResourceState& previous, const ResourceState& next)
{
    VkBufferMemoryBarrier2 barrier = BuildBufferMemoryBarrier2(buffer, offset, size, previous, next);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = 1;
    dependencyInfo.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

const char* ShortLayoutLabel(VkImageLayout layout) noexcept
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return "Undefined";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return "RT";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return "Depth Test";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return "Shader Read";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return "Transfer Src";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return "Transfer Dst";
    case VK_IMAGE_LAYOUT_GENERAL:
        return "General (Storage)";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return "Present";
    default:
        return "Other";
    }
}

} // namespace gte::rg
