#include "FrameRecorder.h"

#include <cassert>
#include <cstring>
#include <iterator>

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

void FrameRecorder::Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix, const Mat4& viewProjMatrix)
{
    DrawItem item;
    item.pipeline = pipeline.Native();
    item.layout = pipeline.Layout();
    item.vertexBuffer = mesh.VertexBuffer();
    item.vertexCount = mesh.VertexCount();
    item.model = modelMatrix;
    item.viewProj = viewProjMatrix;
    m_drawQueue.push_back(item);
}

void FrameRecorder::RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
    VkFormat expectedDepthFormat, VkImageLayout finalLayout, const std::function<void(VkCommandBuffer)>& recordExtra)
{
    // Fail fast (debug builds only) if this target's format doesn't match
    // Renderer::ColorFormat()/DepthFormat() - the shared defaults every
    // pipeline is expected to be built against (see AGENTS.md, "Render
    // Target Format Matching"). A target that's deliberately a different
    // format needs its own dedicated pipeline variant recorded through
    // recordExtra; this catches a mismatch right here, at the one recording
    // path shared by Present()/RenderOffscreen(), instead of a confusing
    // validation-layer warning (or silent misrendering on a driver that
    // happens to tolerate it). Compiled out entirely in release (NDEBUG) -
    // zero cost.
    assert(target.format == expectedFormat &&
        "FrameRecorder::RecordFrame: target format does not match Renderer::ColorFormat() - "
        "any pipeline recorded via recordExtra here must have been built for THIS target's exact format.");
    // Only enforced when this target actually carries a depth image -
    // target.depthImage == VK_NULL_HANDLE deliberately means "skip the depth
    // attachment for this pass entirely" (see below) - e.g.
    // FramePresenter::Present() lazily skips allocating/binding the
    // swapchain's own depth buffers on a frame where nothing but Dear
    // ImGui's own (never depth-tested) chrome is being drawn into it.
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

    // Depth image aspect mask/layout must match whatever it actually is
    // (depth-only vs. combined depth+stencil - see DepthBuffer::
    // HasStencilComponent()). Old layout is UNDEFINED here too - always
    // valid when the attachment's own loadOp is CLEAR (see below), which
    // discards whatever was in it beforehand anyway. Only built/used when
    // hasDepth is true - see below.
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
    colorAttachment.clearValue.color = {
        { m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }
    };

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
    // NULL when this target has no depth image (see hasDepth above) - a
    // pipeline that itself requires a real depth attachment (e.g. this
    // engine's own Pipeline, built with depthAttachmentFormat =
    // Renderer::DepthFormat()) is never actually bound/drawn with in that
    // case anyway, since m_drawQueue is guaranteed empty whenever the caller
    // chose not to supply a depth image - see
    // FrameRecorder::HasQueuedDraws()'s own comment.
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

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

            // Matches Shaders/Triangle.vert's `layout(push_constant) uniform
            // PushConstants { mat4 model; mat4 viewProj; } pc;` exactly -
            // model first (offset 0, 64 bytes), viewProj right after
            // (offset 64, 64 bytes), 128 bytes total - the guaranteed
            // minimum maxPushConstantsSize on every conformant Vulkan
            // implementation (see Pipeline.cpp's VkPushConstantRange), so
            // this never needs a per-GPU size check.
            struct PushConstants {
                float model[16];
                float viewProj[16];
            } pushConstants;
            std::memcpy(pushConstants.model, item.model.Data(), sizeof(pushConstants.model));
            std::memcpy(pushConstants.viewProj, item.viewProj.Data(), sizeof(pushConstants.viewProj));
            vkCmdPushConstants(
                cmd, item.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);

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

    // The depth image is never touched again after this pass (see the
    // declaration comment in FrameRecorder.h) - no final transition needed;
    // the next RecordFrame() call against it starts over from
    // VK_IMAGE_LAYOUT_UNDEFINED via loadOp = CLEAR again either way.
}

} // namespace gte
