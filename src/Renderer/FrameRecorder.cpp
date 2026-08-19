#include "FrameRecorder.h"

#include <cassert>

namespace gte {

void FrameRecorder::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    m_clearColor[0] = static_cast<float>(r) / 255.0f;
    m_clearColor[1] = static_cast<float>(g) / 255.0f;
    m_clearColor[2] = static_cast<float>(b) / 255.0f;
    m_clearColor[3] = static_cast<float>(a) / 255.0f;
}

void FrameRecorder::BeginFrame()
{
    m_drawQueue.clear();
}

void FrameRecorder::Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix)
{
    DrawItem item;
    item.pipeline = pipeline.Native();
    item.layout = pipeline.Layout();
    item.vertexBuffer = mesh.VertexBuffer();
    item.vertexCount = mesh.VertexCount();
    item.model = modelMatrix;
    m_drawQueue.push_back(item);
}

void FrameRecorder::RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
    VkImageLayout finalLayout, const std::function<void(VkCommandBuffer)>& recordExtra)
{
    // Fail fast (debug builds only) if this target's format doesn't match
    // Renderer::ColorFormat() - the shared default every pipeline is
    // expected to be built against (see AGENTS.md, "Render Target Format
    // Matching"). A target that's deliberately a different format needs its
    // own dedicated pipeline variant recorded through recordExtra; this
    // catches a mismatch right here, at the one recording path shared by
    // Present()/RenderOffscreen(), instead of a confusing validation-layer
    // warning (or silent misrendering on a driver that happens to tolerate
    // it) once real pipelines exist. Compiled out entirely in release
    // (NDEBUG) - zero cost.
    assert(target.format == expectedFormat &&
        "FrameRecorder::RecordFrame: target format does not match Renderer::ColorFormat() - "
        "any pipeline recorded via recordExtra here must have been built for THIS target's exact format.");

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

    VkDependencyInfo toColorAttachmentDep{};
    toColorAttachmentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toColorAttachmentDep.imageMemoryBarrierCount = 1;
    toColorAttachmentDep.pImageMemoryBarriers = &toColorAttachment;
    vkCmdPipelineBarrier2(cmd, &toColorAttachmentDep);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = target.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {
        { m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }
    };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, target.extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // Engine (Game) geometry queued via Submit() this frame - recorded
    // first, before any overlay, so an Editor's ImGui chrome always draws
    // on top of it. Cleared right after being recorded - see this
    // function's declaration comment in FrameRecorder.h.
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
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline);
            vkCmdPushConstants(cmd, item.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, item.model.Data());
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &item.vertexBuffer, &offset);
            vkCmdDraw(cmd, item.vertexCount, 1, 0, 0);
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
        // Swapchain path (Present): nothing further touches the image on
        // our side before the presentation engine takes it.
        toFinal.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        toFinal.dstAccessMask = VK_ACCESS_2_NONE;
    }

    VkDependencyInfo toFinalDep{};
    toFinalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toFinalDep.imageMemoryBarrierCount = 1;
    toFinalDep.pImageMemoryBarriers = &toFinal;
    vkCmdPipelineBarrier2(cmd, &toFinalDep);
}

} // namespace gte
