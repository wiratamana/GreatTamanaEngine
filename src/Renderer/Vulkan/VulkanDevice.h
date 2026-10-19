#pragma once

#include <volk.h>

#include "../GpuTiming.h"

#include <cstdint>
#include <mutex>

namespace gte {

// RAII wrapper around a VkPhysicalDevice and the VkDevice created for it, plus its graphics and present
// queues. Does not own the VkInstance or VkSurfaceKHR; both must outlive it. Selection takes the first
// suitable discrete GPU (else any suitable one) with graphics, present, VK_KHR_swapchain and dynamicRendering.
class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VulkanDevice(VulkanDevice&& other) noexcept;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;

    VkPhysicalDevice Physical() const noexcept { return m_physicalDevice; }
    VkDevice Native() const noexcept { return m_device; }

    // The depth format every depth-tested pipeline and depth buffer is built against, queried from the
    // device (never hardcoded). Prefers depth-only D32_SFLOAT over a combined depth+stencil format.
    // Throws std::runtime_error if no candidate is supported, which the spec rules out.
    VkFormat PickDepthFormat() const;

    VkQueue GraphicsQueue() const noexcept { return m_graphicsQueue; }
    VkQueue PresentQueue() const noexcept { return m_presentQueue; }
    std::uint32_t GraphicsQueueFamily() const noexcept { return m_graphicsFamily; }
    std::uint32_t PresentQueueFamily() const noexcept { return m_presentFamily; }

    // GPU timestamp capability, probed once in the constructor (device capabilities do not change).
    // See GpuTiming.h for the type and its decision logic.
    const GpuTimestampCapability& TimestampCapability() const noexcept { return m_timestampCapability; }

    // Whether the device supports drawIndirectCount (vkCmdDrawIndexedIndirectCount), probed once before
    // vkCreateDevice(). Both the real and the fixed-count fallback paths must compile; only this probe
    // chooses between them.
    bool SupportsDrawIndirectCount() const noexcept { return m_supportsDrawIndirectCount; }

    // Whether the device supports BLIT_SRC and BLIT_DST for the engine's depth format, probed once.
    // Depth blit support is not guaranteed and is often missing; any pass that sets
    // BlitSpec::srcIsDepth/dstIsDepth must check it (RenderGraph asserts on it).
    bool SupportsDepthBlit() const noexcept { return m_supportsDepthBlit; }

    // Anisotropy level material samplers may use: 1 when samplerAnisotropy is unsupported (and not
    // enabled on the device), else min(8, device limit). Probed once before vkCreateDevice().
    float MaterialAnisotropy() const noexcept { return m_materialAnisotropy; }

    // The one choke point for submits to the graphics queue: VkQueue access must be externally
    // synchronized and several systems submit to it. Nothing else may call vkQueueSubmit on it.
    VkResult SubmitToGraphicsQueue(const VkSubmitInfo2& submit, VkFence fence) const
    {
        std::lock_guard lock(m_graphicsQueueSubmitMutex);
        return vkQueueSubmit2(m_graphicsQueue, 1, &submit, fence);
    }

private:
    void PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void CreateLogicalDevice();
    // Reads the device's timestamp limits and caches the interpreted capability. Never throws: an
    // unsupported result (period or valid bits of 0) is normal.
    void QueryTimestampCapability();
    void Destroy() noexcept;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = 0;
    std::uint32_t m_presentFamily = 0;

    GpuTimestampCapability m_timestampCapability;

    bool m_supportsDrawIndirectCount = false;
    bool m_supportsDepthBlit = false;
    float m_materialAnisotropy = 1.0f;

    // Guards SubmitToGraphicsQueue(); mutable so that method can stay const.
    mutable std::mutex m_graphicsQueueSubmitMutex;
};

} // namespace gte
