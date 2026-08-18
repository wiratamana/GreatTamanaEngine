#include "FramePresenter.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

FramePresenter::FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
    std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue graphicsQueue,
    VkQueue presentQueue, int width, int height)
    : m_device(device)
    , m_graphicsQueue(graphicsQueue)
    , m_presentQueue(presentQueue)
    , m_graphicsQueueFamily(graphicsQueueFamily)
    , m_swapchain(physicalDevice, device, surface, graphicsQueueFamily, presentQueueFamily, width, height)
    , m_frameSync(device, kFramesInFlight, m_swapchain.ImageCount())
{
    m_pendingWidth = width;
    m_pendingHeight = height;

    CreateCommandObjects();
}

FramePresenter::~FramePresenter()
{
    Destroy();
}

FramePresenter::FramePresenter(FramePresenter&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_graphicsQueue(std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE))
    , m_presentQueue(std::exchange(other.m_presentQueue, VK_NULL_HANDLE))
    , m_graphicsQueueFamily(other.m_graphicsQueueFamily)
    , m_swapchain(std::move(other.m_swapchain))
    , m_frameSync(std::move(other.m_frameSync))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
    , m_commandBuffers(other.m_commandBuffers)
    , m_offscreenCommandBuffer(std::exchange(other.m_offscreenCommandBuffer, VK_NULL_HANDLE))
    , m_currentFrame(other.m_currentFrame)
    , m_resizeRequested(other.m_resizeRequested)
    , m_pendingWidth(other.m_pendingWidth)
    , m_pendingHeight(other.m_pendingHeight)
{
    other.m_commandBuffers.fill(VK_NULL_HANDLE);
}

FramePresenter& FramePresenter::operator=(FramePresenter&& other) noexcept
{
    if (this != &other) {
        Destroy();

        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_graphicsQueue = std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE);
        m_presentQueue = std::exchange(other.m_presentQueue, VK_NULL_HANDLE);
        m_graphicsQueueFamily = other.m_graphicsQueueFamily;
        m_swapchain = std::move(other.m_swapchain);
        m_frameSync = std::move(other.m_frameSync);
        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
        m_commandBuffers = other.m_commandBuffers;
        other.m_commandBuffers.fill(VK_NULL_HANDLE);
        m_offscreenCommandBuffer = std::exchange(other.m_offscreenCommandBuffer, VK_NULL_HANDLE);
        m_currentFrame = other.m_currentFrame;
        m_resizeRequested = other.m_resizeRequested;
        m_pendingWidth = other.m_pendingWidth;
        m_pendingHeight = other.m_pendingHeight;
    }
    return *this;
}

void FramePresenter::Destroy() noexcept
{
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    // m_frameSync, m_swapchain clean themselves up automatically right
    // after this, in reverse declaration order.
}

void FramePresenter::CreateCommandObjects()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("FramePresenter: vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("FramePresenter: vkAllocateCommandBuffers failed");
    }

    // Separate command buffer for RenderOffscreen() - see the member
    // comment in FramePresenter.h for why this doesn't share the per-frame
    // ones above.
    VkCommandBufferAllocateInfo offscreenAllocInfo{};
    offscreenAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    offscreenAllocInfo.commandPool = m_commandPool;
    offscreenAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    offscreenAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device, &offscreenAllocInfo, &m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("FramePresenter: vkAllocateCommandBuffers failed (offscreen)");
    }
}

void FramePresenter::OnResize(int width, int height)
{
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_resizeRequested = true;
}

void FramePresenter::RecreateSwapchain()
{
    if (m_pendingWidth <= 0 || m_pendingHeight <= 0) {
        // Minimized (or otherwise zero-sized) - nothing sensible to build
        // yet. Leave m_resizeRequested set so we keep retrying each frame
        // until the window has a real size again.
        return;
    }

    vkDeviceWaitIdle(m_device);

    m_swapchain.Recreate(m_pendingWidth, m_pendingHeight);

    // Per-swapchain-image semaphores must be rebuilt alongside the
    // swapchain, since the image count can change (or just to be safe).
    m_frameSync.RecreateRenderFinishedSemaphores(m_swapchain.ImageCount());

    m_resizeRequested = false;
}

void FramePresenter::Present(FrameRecorder& frameRecorder, const std::function<void(VkCommandBuffer)>& recordExtra)
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

    const VkFence fence = m_frameSync.InFlightFence(m_currentFrame);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_device, m_swapchain.Native(), std::numeric_limits<std::uint64_t>::max(),
        m_frameSync.ImageAvailableSemaphore(m_currentFrame), VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed (VkResult=" + std::to_string(acquireResult) + ")");
    }

    vkResetFences(m_device, 1, &fence);

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
    frameRecorder.RecordFrame(cmd, target, ColorFormat(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, recordExtra);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed");
    }

    const VkSemaphore waitSemaphore = m_frameSync.ImageAvailableSemaphore(m_currentFrame);
    const VkSemaphore signalSemaphore = m_frameSync.RenderFinishedSemaphore(imageIndex);
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

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
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

    const VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed (VkResult=" + std::to_string(presentResult) + ")");
    }

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

void FramePresenter::RenderOffscreen(
    FrameRecorder& frameRecorder, RenderTexture& target, const std::function<void(VkCommandBuffer)>& recordExtra)
{
    const VkFence offscreenFence = m_frameSync.OffscreenFence();
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkResetFences(m_device, 1, &offscreenFence);

    vkResetCommandBuffer(m_offscreenCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(m_offscreenCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed (offscreen)");
    }

    frameRecorder.RecordFrame(m_offscreenCommandBuffer, target.Target(), ColorFormat(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, recordExtra);

    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen)");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_offscreenCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed (offscreen)");
    }

    // Synchronous for now - see the declaration comment in Renderer.h.
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
}

} // namespace gte
