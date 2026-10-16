#include "FrameRecorder.h"

#include "IndirectDrawTypes.h"

#include <cassert>
#include <cstring>
#include <iterator>

namespace gte {

void FrameRecorder::IssueDrawCommand(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
    VkBuffer vertexBuffer, std::uint32_t vertexCount, VkBuffer indexBuffer, std::uint32_t indexCount,
    const Mat4& model, const Mat4& viewProj, VkDescriptorSet materialDescriptorSet, VkDescriptorSet sceneServicesSet)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Mirrors the `PushConstants { mat4 model; mat4 viewProj; }` block in Triangle.vert: model at offset 0,
    // viewProj at 64, 128 bytes total (see Pipeline.cpp).
    struct PushConstants {
        float model[16];
        float viewProj[16];
    } pushConstants;
    std::memcpy(pushConstants.model, model.Data(), sizeof(pushConstants.model));
    std::memcpy(pushConstants.viewProj, viewProj.Data(), sizeof(pushConstants.viewProj));
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);

    // Set 0: the per-submesh material texture; only PositionNormalUv pipelines declare it, others pass null.
    if (materialDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &materialDescriptorSet, 0, nullptr);
    }

    // Set 1: bound independently of set 0 when a scene-services set was resolved (Renderer::Submit() gates it).
    if (sceneServicesSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, /*firstSet=*/1, 1, &sceneServicesSet, 0, nullptr);
    }

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    if (indexBuffer != VK_NULL_HANDLE) {
        // A real imported mesh (see Mesh's indexed constructor) -
        // vkCmdDrawIndexed() shares its vertices across triangles instead
        // of duplicating them per-triangle.
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);
    }
}

void FrameRecorder::IssueIndirectDrawCommand(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
    VkBuffer vertexBuffer, VkBuffer indexBuffer, VkDescriptorSet instanceBufferDescriptorSet,
    VkDescriptorSet sceneServicesSet, const Mat4& viewProj, VkBuffer indirectBuffer, uint64_t indirectOffset,
    std::uint32_t maxDrawCount, VkBuffer countBuffer, uint64_t countBufferOffset, bool supportsDrawIndirectCount)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Same push-constant shape as every pipeline; the `model` half stays zero (unused by the instanced shader).
    struct PushConstants {
        float model[16];
        float viewProj[16];
    } pushConstants{};
    std::memcpy(pushConstants.viewProj, viewProj.Data(), sizeof(pushConstants.viewProj));
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);

    // Never optional: every indirect draw uses the instanced pipeline, which always declares set 0.
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &instanceBufferDescriptorSet, 0, nullptr);

    // Set 1, bound only when a scene-services set was resolved (SubmitIndirect() gates it).
    if (sceneServicesSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, /*firstSet=*/1, 1, &sceneServicesSet, 0, nullptr);
    }

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    // Indirect draws are always indexed.
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    constexpr std::uint32_t stride = sizeof(IndirectDrawCommand);
    if (countBuffer != VK_NULL_HANDLE) {
        assert(supportsDrawIndirectCount
            && "IssueIndirectDrawCommand: a countBuffer was supplied but this device has no drawIndirectCount; "
               "the caller must decide this once per batch.");
        if (supportsDrawIndirectCount) {
            // Counted path: the surviving instance count comes from `countBuffer`, capped at `maxDrawCount`.
            vkCmdDrawIndexedIndirectCount(cmd, indirectBuffer, static_cast<VkDeviceSize>(indirectOffset), countBuffer,
                static_cast<VkDeviceSize>(countBufferOffset), maxDrawCount, stride);
            return;
        }
        // Release fallback for a caller-side logic error (asserted above): use the fixed-count path below.
    }

    // Fallback: a fixed CPU-known `maxDrawCount`. Culling pads non-surviving instances with degenerate
    // (indexCount == 0) commands, so exactly the survivors still draw.
    vkCmdDrawIndexedIndirect(cmd, indirectBuffer, static_cast<VkDeviceSize>(indirectOffset), maxDrawCount, stride);
}

void FrameRecorder::BeginFrame()
{
    m_drawQueue.clear();
}

void FrameRecorder::Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix,
    const Mat4& viewProjMatrix, VkDescriptorSet materialDescriptorSet, VkDescriptorSet sceneServicesSet)
{
    DrawItem item;
    item.pipeline = pipeline.Native();
    item.layout = pipeline.Layout();
    item.vertexBuffer = mesh.VertexBuffer();
    item.vertexCount = mesh.VertexCount();
    if (mesh.HasIndexBuffer()) {
        item.indexBuffer = mesh.IndexBuffer();
        item.indexCount = mesh.IndexCount();
    }
    item.model = modelMatrix;
    item.viewProj = viewProjMatrix;
    item.materialDescriptorSet = materialDescriptorSet;
    item.sceneServicesSet = sceneServicesSet;
    m_drawQueue.push_back(item);
}

DrawStats FrameRecorder::RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
    VkFormat expectedDepthFormat, VkImageLayout finalLayout, const std::array<float, 4>& clearColor,
    const std::function<void(VkCommandBuffer)>& recordExtra)
{
    // Debug-only: the target must match the renderer's default formats every pipeline is built against.
    // A deliberately different format needs its own pipeline variant recorded through recordExtra.
    assert(target.format == expectedFormat &&
        "FrameRecorder::RecordFrame: target format does not match Renderer::ColorFormat() - "
        "any pipeline recorded via recordExtra here must have been built for THIS target's exact format.");
    // A null depthImage means the depth attachment is skipped, so only check the format when one exists.
    assert((target.depthImage == VK_NULL_HANDLE || target.depthFormat == expectedDepthFormat) &&
        "FrameRecorder::RecordFrame: target depth format does not match Renderer::DepthFormat().");

    const bool hasDepth = target.depthImage != VK_NULL_HANDLE;

    // Dynamic rendering (no VkRenderPass/VkFramebuffer) means WE are
    // responsible for the layout transitions a render pass would normally
    // have done implicitly.
    VkImageMemoryBarrier2 toColorAttachment{};
    toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toColorAttachment.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toColorAttachment.srcAccessMask = VK_ACCESS_2_NONE;
    toColorAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColorAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColorAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.image = target.image;
    toColorAttachment.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // The depth aspect and layout follow the real format (depth-only or depth+stencil). The old layout is
    // UNDEFINED because loadOp = CLEAR discards the contents anyway.
    const VkImageAspectFlags depthAspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | (target.depthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    const VkImageLayout depthAttachmentLayout =
        target.depthHasStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkImageMemoryBarrier2 toDepthAttachment{};
    toDepthAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toDepthAttachment.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toDepthAttachment.srcAccessMask = VK_ACCESS_2_NONE;
    toDepthAttachment.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toDepthAttachment.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepthAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDepthAttachment.newLayout = depthAttachmentLayout;
    toDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.image = target.depthImage;
    toDepthAttachment.subresourceRange = { depthAspectMask, 0, 1, 0, 1 };

    // Only include the depth barrier when this target actually has a depth
    // image (hasDepth) - a target with none (see above) skips it entirely,
    // since there's nothing to transition.
    VkImageMemoryBarrier2 toAttachmentBarriers[2];
    toAttachmentBarriers[0] = toColorAttachment;
    std::uint32_t toAttachmentBarrierCount = 1;
    if (hasDepth) {
        toAttachmentBarriers[1] = toDepthAttachment;
        toAttachmentBarrierCount = 2;
    }

    VkDependencyInfo toAttachmentDep{};
    toAttachmentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toAttachmentDep.imageMemoryBarrierCount = toAttachmentBarrierCount;
    toAttachmentDep.pImageMemoryBarriers = toAttachmentBarriers;
    vkCmdPipelineBarrier2(cmd, &toAttachmentDep);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = target.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { clearColor[0], clearColor[1], clearColor[2], clearColor[3] } };

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = target.depthImageView;
    depthAttachment.imageLayout = depthAttachmentLayout;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Nothing ever samples this depth buffer afterwards (see the
    // declaration comment in FrameRecorder.h) - DONT_CARE lets the driver
    // skip writing it back out where the hardware supports that.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 }; // 1.0 == the far plane (see Pipeline's VK_COMPARE_OP_LESS).

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, target.extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    // Null when the target has no depth image; nothing depth-tested is drawn into such a target.
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // Accumulated inline in the loop below that issues each draw, never in a separate pass over m_drawQueue.
    // Stays {0, 0} when the queue is empty.
    DrawStats drawStats;

    // Queued geometry is recorded before any overlay so UI chrome draws on top, then the queue is cleared.
    if (!m_drawQueue.empty()) {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(target.extent.width);
        viewport.height = static_cast<float>(target.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = target.extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        for (const DrawItem& item : m_drawQueue) {
            IssueDrawCommand(cmd, item.pipeline, item.layout, item.vertexBuffer, item.vertexCount, item.indexBuffer,
                item.indexCount, item.model, item.viewProj, item.materialDescriptorSet, item.sceneServicesSet);

            // Counted only after the draw was issued, so it can never overstate what the GPU received.
            AccumulateDrawStats(drawStats, item.indexBuffer != VK_NULL_HANDLE, item.vertexCount, item.indexCount);
        }

        m_drawQueue.clear();
    }

    // Then optionally an overlay (Dear ImGui's
    // ImGui_ImplVulkan_RenderDrawData(), a debug UI, ...) gets recorded
    // here via recordExtra, still between vkCmdBeginRendering/vkCmdEndRendering.
    if (recordExtra) {
        recordExtra(cmd);
    }
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toFinal{};
    toFinal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toFinal.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toFinal.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toFinal.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toFinal.newLayout = finalLayout;
    toFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.image = target.image;
    toFinal.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Off-screen path (RenderOffscreen): the next thing to touch this
        // image is a fragment shader sampling it (e.g. ImGui::Image()).
        toFinal.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toFinal.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    } else {
        // Any other finalLayout (e.g. PRESENT_SRC_KHR): the next consumer is not on our side.
        toFinal.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        toFinal.dstAccessMask = VK_ACCESS_2_NONE;
    }

    VkDependencyInfo toFinalDep{};
    toFinalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toFinalDep.imageMemoryBarrierCount = 1;
    toFinalDep.pImageMemoryBarriers = &toFinal;
    vkCmdPipelineBarrier2(cmd, &toFinalDep);

    // No final depth transition: the next RecordFrame() starts again from UNDEFINED with loadOp = CLEAR.

    return drawStats;
}

} // namespace gte
