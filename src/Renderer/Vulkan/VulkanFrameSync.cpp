#include "VulkanFrameSync.h"

#include <stdexcept>
#include <utility>

namespace gte {

VulkanFrameSync::VulkanFrameSync(VkDevice device, std::uint32_t framesInFlight, std::uint32_t swapchainImageCount)
    : m_device(device)
{
    CreatePerFrameObjects(framesInFlight);
    CreateRenderFinishedSemaphores(swapchainImageCount);

    // Starts signaled, same reasoning as the in-flight fences created in
    // CreatePerFrameObjects(): the first RenderOffscreen() call must not
    // block waiting on a fence that was never submitted.
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_offscreenFence) != VK_SUCCESS) {
        Destroy();
        throw std::runtime_error("VulkanFrameSync: failed to create offscreen fence.");
    }
}

VulkanFrameSync::~VulkanFrameSync()
{
    Destroy();
}

VulkanFrameSync::VulkanFrameSync(VulkanFrameSync&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_imageAvailableSemaphores(std::move(other.m_imageAvailableSemaphores))
    , m_inFlightFences(std::move(other.m_inFlightFences))
    , m_renderFinishedSemaphores(std::move(other.m_renderFinishedSemaphores))
    , m_offscreenFence(std::exchange(other.m_offscreenFence, VK_NULL_HANDLE))
{
}

VulkanFrameSync& VulkanFrameSync::operator=(VulkanFrameSync&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_imageAvailableSemaphores = std::move(other.m_imageAvailableSemaphores);
        m_inFlightFences = std::move(other.m_inFlightFences);
        m_renderFinishedSemaphores = std::move(other.m_renderFinishedSemaphores);
        m_offscreenFence = std::exchange(other.m_offscreenFence, VK_NULL_HANDLE);
    }
    return *this;
}

void VulkanFrameSync::CreatePerFrameObjects(std::uint32_t framesInFlight)
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Sized (with every entry starting VK_NULL_HANDLE) before the creation
    // loop below, so that if any single vkCreate* call fails partway
    // through, Destroy() - called right before the throw - can safely walk
    // the whole vector and only destroy the handles that actually got
    // created, instead of leaking them.
    m_imageAvailableSemaphores.resize(framesInFlight, VK_NULL_HANDLE);
    m_inFlightFences.resize(framesInFlight, VK_NULL_HANDLE);

    for (std::uint32_t i = 0; i < framesInFlight; ++i) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            Destroy();
            throw std::runtime_error("VulkanFrameSync: failed to create per-frame semaphore/fence.");
        }
    }
}

void VulkanFrameSync::CreateRenderFinishedSemaphores(std::uint32_t swapchainImageCount)
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Same "resize with VK_NULL_HANDLE first" reasoning as
    // CreatePerFrameObjects() above.
    m_renderFinishedSemaphores.resize(swapchainImageCount, VK_NULL_HANDLE);
    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            Destroy();
            throw std::runtime_error("VulkanFrameSync: failed to create per-image render-finished semaphore.");
        }
    }
}

void VulkanFrameSync::RecreateRenderFinishedSemaphores(std::uint32_t swapchainImageCount)
{
    DestroyRenderFinishedSemaphores();
    CreateRenderFinishedSemaphores(swapchainImageCount);
}

void VulkanFrameSync::DestroyRenderFinishedSemaphores() noexcept
{
    for (VkSemaphore semaphore : m_renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
    m_renderFinishedSemaphores.clear();
}

void VulkanFrameSync::Destroy() noexcept
{
    DestroyRenderFinishedSemaphores();

    for (VkSemaphore semaphore : m_imageAvailableSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
    m_imageAvailableSemaphores.clear();

    for (VkFence fence : m_inFlightFences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, fence, nullptr);
        }
    }
    m_inFlightFences.clear();

    if (m_offscreenFence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_offscreenFence, nullptr);
        m_offscreenFence = VK_NULL_HANDLE;
    }
}

} // namespace gte
