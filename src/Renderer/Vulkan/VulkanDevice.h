#pragma once

#include <volk.h>

#include "../Culling/ClusterCullMath.h"
#include "../GpuTiming.h"
#include "GpuWaitService.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <type_traits>

namespace gte {

static_assert(std::is_trivially_copyable_v<IndirectDrawCapsView>);

// RAII wrapper around a VkPhysicalDevice, its VkDevice and its graphics and present queues. The instance and
// surface must outlive it. Not movable: the wait service points at this object's submit mutex.
class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

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

    // Indirect draw capabilities, probed once before vkCreateDevice(). maxDrawIndirectCount is 0 unless
    // multiDrawIndirect is enabled.
    const IndirectDrawCapsView& IndirectDraw() const noexcept { return m_indirectDrawCaps; }

    // Whether the device supports BLIT_SRC and BLIT_DST for the engine's depth format, probed once.
    // Depth blit support is not guaranteed and is often missing; any pass that sets
    // BlitSpec::srcIsDepth/dstIsDepth must check it (RenderGraph asserts on it).
    bool SupportsDepthBlit() const noexcept { return m_supportsDepthBlit; }

    // Anisotropy level material samplers may use: 1 when samplerAnisotropy is unsupported (and not
    // enabled on the device), else min(8, device limit). Probed once before vkCreateDevice().
    float MaterialAnisotropy() const noexcept { return m_materialAnisotropy; }

    // The one place that blocks on the GPU. Thread-safe, so it is reachable from const callers.
    GpuWaitService& Waits() const noexcept { return *m_waits; }

    // The one choke point for graphics-queue submits (VkQueue is externally synchronized); refused once the device is lost.
    // The only other submit is GpuWaitService::DrainGraphicsQueue, under the same mutex.
    VkResult SubmitToGraphicsQueue(const VkSubmitInfo2& submit, VkFence fence) const
    {
        if (m_waits->DeviceLost()) {
            return VK_ERROR_DEVICE_LOST;
        }
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

    IndirectDrawCapsView m_indirectDrawCaps{};
    bool m_supportsDepthBlit = false;
    float m_materialAnisotropy = 1.0f;

    // Guards SubmitToGraphicsQueue() and the wait service's drain submit; mutable so submit can stay const.
    mutable std::mutex m_graphicsQueueSubmitMutex;

    // Declared after the mutex it uses; destroyed before the device.
    mutable std::optional<GpuWaitService> m_waits;
};

} // namespace gte
