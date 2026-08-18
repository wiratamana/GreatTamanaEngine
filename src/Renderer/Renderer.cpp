#include "Renderer.h"

#include "../Window/Window.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gte {

namespace {

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

} // namespace

Renderer::Renderer(Window& window)
    : m_instance("GreatTamanaEngine", Window::VulkanInstanceExtensions(), kEnableValidation)
    , m_surface(m_instance.Native(), window)
    , m_device(m_instance.Native(), m_surface.Native())
    // VK_API_VERSION_1_3 matches VulkanInstance::CreateInstance's
    // VkApplicationInfo::apiVersion (see also GetVulkanContextInfo() below).
    , m_allocator(m_instance.Native(), m_device.Physical(), m_device.Native(), VK_API_VERSION_1_3)
    , m_swapchain(m_device.Physical(), m_device.Native(), m_surface.Native(),
                  m_device.GraphicsQueueFamily(), m_device.PresentQueueFamily(),
                  window.Width(), window.Height())
{
    m_pendingWidth = window.Width();
    m_pendingHeight = window.Height();

    CreateCommandObjects();
    CreateSyncObjects();
}

Renderer::~Renderer()
{
    if (m_device.Native() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device.Native());
    }
    DestroySyncObjects();
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device.Native(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    // m_swapchain, m_device, m_surface, m_instance clean themselves up
    // automatically after this destructor body, in reverse declaration order.
}

Renderer::Renderer(Renderer&& other) noexcept
    : m_instance(std::move(other.m_instance))
    , m_surface(std::move(other.m_surface))
    , m_device(std::move(other.m_device))
    , m_allocator(std::move(other.m_allocator))
    , m_swapchain(std::move(other.m_swapchain))
    , m_memoryTracker(std::move(other.m_memoryTracker))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
    , m_commandBuffers(other.m_commandBuffers)
    , m_imageAvailableSemaphores(other.m_imageAvailableSemaphores)
    , m_inFlightFences(other.m_inFlightFences)
    , m_renderFinishedSemaphores(std::move(other.m_renderFinishedSemaphores))
    , m_offscreenCommandBuffer(std::exchange(other.m_offscreenCommandBuffer, VK_NULL_HANDLE))
    , m_offscreenFence(std::exchange(other.m_offscreenFence, VK_NULL_HANDLE))
    , m_currentFrame(other.m_currentFrame)
    , m_resizeRequested(other.m_resizeRequested)
    , m_pendingWidth(other.m_pendingWidth)
    , m_pendingHeight(other.m_pendingHeight)
    , m_drawQueue(std::move(other.m_drawQueue))
{
    other.m_commandBuffers.fill(VK_NULL_HANDLE);
    other.m_imageAvailableSemaphores.fill(VK_NULL_HANDLE);
    other.m_inFlightFences.fill(VK_NULL_HANDLE);
    for (int i = 0; i < 4; ++i) {
        m_clearColor[i] = other.m_clearColor[i];
    }
}

Renderer& Renderer::operator=(Renderer&& other) noexcept
{
    if (this != &other) {
        if (m_device.Native() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device.Native());
        }
        DestroySyncObjects();
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device.Native(), m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }

        m_instance = std::move(other.m_instance);
        m_surface = std::move(other.m_surface);
        m_device = std::move(other.m_device);
        m_allocator = std::move(other.m_allocator);
        m_swapchain = std::move(other.m_swapchain);
        m_memoryTracker = std::move(other.m_memoryTracker);

        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
        m_commandBuffers = other.m_commandBuffers;
        m_imageAvailableSemaphores = other.m_imageAvailableSemaphores;
        m_inFlightFences = other.m_inFlightFences;
        m_renderFinishedSemaphores = std::move(other.m_renderFinishedSemaphores);
        other.m_commandBuffers.fill(VK_NULL_HANDLE);
        other.m_imageAvailableSemaphores.fill(VK_NULL_HANDLE);
        other.m_inFlightFences.fill(VK_NULL_HANDLE);

        m_offscreenCommandBuffer = std::exchange(other.m_offscreenCommandBuffer, VK_NULL_HANDLE);
        m_offscreenFence = std::exchange(other.m_offscreenFence, VK_NULL_HANDLE);

        m_currentFrame = other.m_currentFrame;
        for (int i = 0; i < 4; ++i) {
            m_clearColor[i] = other.m_clearColor[i];
        }
        m_resizeRequested = other.m_resizeRequested;
        m_pendingWidth = other.m_pendingWidth;
        m_pendingHeight = other.m_pendingHeight;
        m_drawQueue = std::move(other.m_drawQueue);
    }
    return *this;
}

void Renderer::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    m_clearColor[0] = static_cast<float>(r) / 255.0f;
    m_clearColor[1] = static_cast<float>(g) / 255.0f;
    m_clearColor[2] = static_cast<float>(b) / 255.0f;
    m_clearColor[3] = static_cast<float>(a) / 255.0f;
}

void Renderer::OnResize(int width, int height)
{
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_resizeRequested = true;
}

void Renderer::CreateCommandObjects()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_device.GraphicsQueueFamily();

    if (vkCreateCommandPool(m_device.Native(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kMaxFramesInFlight;

    if (vkAllocateCommandBuffers(m_device.Native(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    // Separate command buffer for RenderOffscreen() - see the member
    // comment in Renderer.h for why this doesn't share the per-frame ones
    // above.
    VkCommandBufferAllocateInfo offscreenAllocInfo{};
    offscreenAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    offscreenAllocInfo.commandPool = m_commandPool;
    offscreenAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    offscreenAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device.Native(), &offscreenAllocInfo, &m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateCommandBuffers failed (offscreen)");
    }
}

void Renderer::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(m_device.Native(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device.Native(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create per-frame semaphore/fence");
        }
    }

    m_renderFinishedSemaphores.resize(m_swapchain.ImageCount(), VK_NULL_HANDLE);
    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (vkCreateSemaphore(m_device.Native(), &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create per-image render-finished semaphore");
        }
    }

    // Starts signaled, same reasoning as m_inFlightFences: the first
    // RenderOffscreen() call must not block waiting on a fence that was
    // never submitted.
    if (vkCreateFence(m_device.Native(), &fenceInfo, nullptr, &m_offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen fence");
    }
}

void Renderer::DestroySyncObjects() noexcept
{
    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device.Native(), semaphore, nullptr);
        }
    }
    m_renderFinishedSemaphores.clear();

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device.Native(), m_imageAvailableSemaphores[i], nullptr);
            m_imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
        if (m_inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(m_device.Native(), m_inFlightFences[i], nullptr);
            m_inFlightFences[i] = VK_NULL_HANDLE;
        }
    }

    if (m_offscreenFence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device.Native(), m_offscreenFence, nullptr);
        m_offscreenFence = VK_NULL_HANDLE;
    }
}

void Renderer::RecreateSwapchain()
{
    if (m_pendingWidth <= 0 || m_pendingHeight <= 0) {
        // Minimized (or otherwise zero-sized) - nothing sensible to build
        // yet. Leave m_resizeRequested set so we keep retrying each frame
        // until the window has a real size again.
        return;
    }

    vkDeviceWaitIdle(m_device.Native());

    // Per-swapchain-image semaphores must be resized alongside the
    // swapchain, since the image count can change (or just to be safe).
    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device.Native(), semaphore, nullptr);
        }
    }
    m_renderFinishedSemaphores.clear();

    m_swapchain.Recreate(m_pendingWidth, m_pendingHeight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    m_renderFinishedSemaphores.resize(m_swapchain.ImageCount(), VK_NULL_HANDLE);
    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (vkCreateSemaphore(m_device.Native(), &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to recreate per-image render-finished semaphore");
        }
    }

    m_resizeRequested = false;
}

void Renderer::RecordClearAndTransition(VkCommandBuffer cmd, const RenderTarget& target, VkImageLayout finalLayout,
    const std::function<void(VkCommandBuffer)>& recordExtra)
{
    // Fail fast (debug builds only) if this target's format doesn't match
    // ColorFormat() - the shared default every pipeline is expected to be
    // built against (see AGENTS.md, "Render Target Format Matching"). A
    // target that's deliberately a different format needs its own dedicated
    // pipeline variant recorded through recordExtra; this catches a mismatch
    // right here, at the one recording path shared by Present()/
    // RenderOffscreen(), instead of a confusing validation-layer warning (or
    // silent misrendering on a driver that happens to tolerate it) once real
    // pipelines exist. Compiled out entirely in release (NDEBUG) - zero cost.
    assert(target.format == ColorFormat() &&
        "Renderer::RecordClearAndTransition: target format does not match Renderer::ColorFormat() - "
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
    // on top of it. Cleared right after being recorded so a second
    // RecordClearAndTransition() call later in the SAME frame (e.g.
    // Present()'s editor-chrome-only pass, after RenderOffscreen() already
    // drew the queue into the Game view texture) never redraws it - see
    // the m_drawQueue member comment in Renderer.h.
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

void Renderer::Present(const std::function<void(VkCommandBuffer)>& recordExtra)
{
    if (m_pendingWidth <= 0 || m_pendingHeight <= 0) {
        return; // Minimized - nothing to draw this frame.
    }

    if (m_resizeRequested) {
        RecreateSwapchain();
        if (m_resizeRequested) {
            return; // Still pending (stayed minimized) - try again next frame.
        }
    }

    const VkFence fence = m_inFlightFences[m_currentFrame];
    vkWaitForFences(m_device.Native(), 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_device.Native(), m_swapchain.Native(), std::numeric_limits<std::uint64_t>::max(),
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed (VkResult=" + std::to_string(acquireResult) + ")");
    }

    vkResetFences(m_device.Native(), 1, &fence);

    const VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed");
    }

    RenderTarget target;
    target.image = m_swapchain.Image(imageIndex);
    target.imageView = m_swapchain.ImageView(imageIndex);
    target.extent = m_swapchain.Extent();
    target.format = m_swapchain.ImageFormat();
    RecordClearAndTransition(cmd, target, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, recordExtra);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed");
    }

    const VkSemaphore waitSemaphore = m_imageAvailableSemaphores[m_currentFrame];
    const VkSemaphore signalSemaphore = m_renderFinishedSemaphores[imageIndex];
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    if (vkQueueSubmit(m_device.GraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    const VkSwapchainKHR swapchain = m_swapchain.Native();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_device.PresentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed (VkResult=" + std::to_string(presentResult) + ")");
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

void Renderer::RenderOffscreen(RenderTexture& target, const std::function<void(VkCommandBuffer)>& recordExtra)
{
    vkWaitForFences(m_device.Native(), 1, &m_offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkResetFences(m_device.Native(), 1, &m_offscreenFence);

    vkResetCommandBuffer(m_offscreenCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(m_offscreenCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed (offscreen)");
    }

    RecordClearAndTransition(m_offscreenCommandBuffer, target.Target(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, recordExtra);

    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen)");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_offscreenCommandBuffer;

    if (vkQueueSubmit(m_device.GraphicsQueue(), 1, &submitInfo, m_offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed (offscreen)");
    }

    // Synchronous for now - see the declaration comment in Renderer.h.
    vkWaitForFences(m_device.Native(), 1, &m_offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
}

VkFormat Renderer::ColorFormat() const noexcept
{
    return m_swapchain.ImageFormat();
}

RenderTexture Renderer::CreateRenderTexture(int width, int height, VkFormat format, const char* debugName) const
{
    // VK_FORMAT_UNDEFINED (the default - see Renderer.h) means "match
    // ColorFormat() exactly", not "let Vulkan pick" - resolved here rather
    // than baking a hardcoded literal into the default argument, so this
    // always tracks whatever the swapchain actually negotiated at runtime
    // (see VulkanSwapchain.cpp's ChooseSurfaceFormat), even if that differs
    // across GPUs/drivers. See AGENTS.md ("Render Target Format Matching").
    const VkFormat resolvedFormat = (format == VK_FORMAT_UNDEFINED) ? ColorFormat() : format;
    return RenderTexture(
        m_allocator.Native(), m_memoryTracker, m_device.Native(), width, height, resolvedFormat, debugName);
}

Buffer Renderer::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return Buffer(m_allocator.Native(), m_memoryTracker, size, usage, memoryUsage, debugName);
}

Buffer Renderer::CreateDeviceLocalBuffer(
    const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName) const
{
    // The staging buffer is intentionally unnamed (nullptr) - it's a
    // throwaway that never outlives this function, so it's never worth
    // showing up as a named entry in a future Memory panel.
    Buffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemoryUsage::CpuToGpu);
    staging.Upload(data, static_cast<std::size_t>(size));

    Buffer deviceLocal =
        CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuOnly, debugName);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, staging.Native(), deviceLocal.Native(), 1, &copyRegion);
    });
    // staging goes out of scope here and is destroyed - the copy above has
    // already completed (ImmediateSubmit blocks until the GPU is done).

    return deviceLocal;
}

void Renderer::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& recordFn) const
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device.Native(), &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("Renderer::ImmediateSubmit: vkAllocateCommandBuffers failed.");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device.Native(), m_commandPool, 1, &cmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkBeginCommandBuffer failed.");
    }

    recordFn(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device.Native(), m_commandPool, 1, &cmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkEndCommandBuffer failed.");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device.Native(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device.Native(), m_commandPool, 1, &cmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkCreateFence failed.");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    if (vkQueueSubmit(m_device.GraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device.Native(), fence, nullptr);
        vkFreeCommandBuffers(m_device.Native(), m_commandPool, 1, &cmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkQueueSubmit failed.");
    }

    vkWaitForFences(m_device.Native(), 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    vkDestroyFence(m_device.Native(), fence, nullptr);
    vkFreeCommandBuffers(m_device.Native(), m_commandPool, 1, &cmd);
}

void Renderer::BeginFrame()
{
    m_drawQueue.clear();
}

void Renderer::Submit(const Pipeline& pipeline, const Mesh& mesh)
{
    DrawItem item;
    item.pipeline = pipeline.Native();
    item.vertexBuffer = mesh.VertexBuffer();
    item.vertexCount = mesh.VertexCount();
    m_drawQueue.push_back(item);
}

Pipeline Renderer::CreatePipeline(
    const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath) const
{
    return Pipeline(m_device.Native(), ColorFormat(), vertexShaderSpirvPath, fragmentShaderSpirvPath);
}

Mesh Renderer::CreateMesh(
    const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount, const char* debugName) const
{
    Buffer vertexBuffer =
        CreateDeviceLocalBuffer(vertexData, vertexDataSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, debugName);
    return Mesh(std::move(vertexBuffer), vertexCount);
}

Renderer::VulkanContextInfo Renderer::GetVulkanContextInfo() const
{
    VulkanContextInfo info;
    info.apiVersion = VK_API_VERSION_1_3; // matches VulkanInstance::CreateInstance's VkApplicationInfo::apiVersion
    info.instance = m_instance.Native();
    info.physicalDevice = m_device.Physical();
    info.device = m_device.Native();
    info.graphicsQueueFamily = m_device.GraphicsQueueFamily();
    info.graphicsQueue = m_device.GraphicsQueue();
    info.colorFormat = ColorFormat(); // single source of truth - see ColorFormat()'s comment in Renderer.h
    info.imageCount = m_swapchain.ImageCount();
    info.minImageCount = kMaxFramesInFlight; // matches what this Renderer actually keeps in flight
    return info;
}

GpuMemoryTracker::Totals Renderer::GetMemoryTotals() const
{
    return m_memoryTracker->GetTotals();
}

std::vector<GpuMemoryTracker::Entry> Renderer::GetMemoryResources() const
{
    return m_memoryTracker->GetAllResources();
}

} // namespace gte
