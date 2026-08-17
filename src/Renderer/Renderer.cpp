#include "Renderer.h"

#include "../Window/Window.h"

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
    , m_swapchain(std::move(other.m_swapchain))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
    , m_commandBuffers(other.m_commandBuffers)
    , m_imageAvailableSemaphores(other.m_imageAvailableSemaphores)
    , m_inFlightFences(other.m_inFlightFences)
    , m_renderFinishedSemaphores(std::move(other.m_renderFinishedSemaphores))
    , m_currentFrame(other.m_currentFrame)
    , m_resizeRequested(other.m_resizeRequested)
    , m_pendingWidth(other.m_pendingWidth)
    , m_pendingHeight(other.m_pendingHeight)
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
        m_swapchain = std::move(other.m_swapchain);

        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
        m_commandBuffers = other.m_commandBuffers;
        m_imageAvailableSemaphores = other.m_imageAvailableSemaphores;
        m_inFlightFences = other.m_inFlightFences;
        m_renderFinishedSemaphores = std::move(other.m_renderFinishedSemaphores);
        other.m_commandBuffers.fill(VK_NULL_HANDLE);
        other.m_imageAvailableSemaphores.fill(VK_NULL_HANDLE);
        other.m_inFlightFences.fill(VK_NULL_HANDLE);

        m_currentFrame = other.m_currentFrame;
        for (int i = 0; i < 4; ++i) {
            m_clearColor[i] = other.m_clearColor[i];
        }
        m_resizeRequested = other.m_resizeRequested;
        m_pendingWidth = other.m_pendingWidth;
        m_pendingHeight = other.m_pendingHeight;
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

void Renderer::RecordClearCommands(VkCommandBuffer cmd, std::uint32_t imageIndex)
{
    VkImage image = m_swapchain.Image(imageIndex);

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
    toColorAttachment.image = image;
    toColorAttachment.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo toColorAttachmentDep{};
    toColorAttachmentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toColorAttachmentDep.imageMemoryBarrierCount = 1;
    toColorAttachmentDep.pImageMemoryBarriers = &toColorAttachment;
    vkCmdPipelineBarrier2(cmd, &toColorAttachmentDep);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchain.ImageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {
        { m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }
    };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, m_swapchain.Extent() };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    // Future draw calls (engine geometry, then Dear ImGui's
    // ImGui_ImplVulkan_RenderDrawData()) get recorded here, between
    // vkCmdBeginRendering/vkCmdEndRendering.
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    toPresent.dstAccessMask = VK_ACCESS_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = image;
    toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo toPresentDep{};
    toPresentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toPresentDep.imageMemoryBarrierCount = 1;
    toPresentDep.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(cmd, &toPresentDep);
}

void Renderer::Present()
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

    RecordClearCommands(cmd, imageIndex);

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

} // namespace gte
