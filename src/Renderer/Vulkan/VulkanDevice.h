#pragma once

#include <volk.h>

#include "../GpuTiming.h"

#include <cstdint>

namespace gte {

// RAII wrapper around a chosen VkPhysicalDevice + the VkDevice (logical
// device) created for it, plus the graphics/present queues pulled from that
// device. Owns the VkDevice for its entire lifetime: created in the
// constructor, destroyed in the destructor. Does NOT own the VkInstance or
// VkSurfaceKHR passed in - both must outlive this device.
//
// Selection picks the first suitable discrete GPU (falling back to any
// suitable device) that has:
//   - a queue family supporting VK_QUEUE_GRAPHICS_BIT
//   - a queue family with presentation support for the given surface
//     (may be the same family as graphics)
//   - VK_KHR_swapchain support
//   - VkPhysicalDeviceVulkan13Features::dynamicRendering support (used
//     instead of classic VkRenderPass/VkFramebuffer - simpler, and what
//     Dear ImGui's Vulkan backend supports natively)
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

    // Picks the best depth-buffer format this physical device actually
    // supports as a depth/stencil attachment (queried via
    // vkGetPhysicalDeviceFormatProperties(), never assumed) - the single
    // source of truth every depth-tested Pipeline/DepthBuffer is built
    // against, the exact same "ask the device, don't hardcode a literal"
    // discipline Renderer::ColorFormat() already applies to the swapchain's
    // surface format (see AGENTS.md, "Render Target Format Matching").
    // Prefers a depth-only format (VK_FORMAT_D32_SFLOAT) when available -
    // simpler layout/aspect-mask handling than a combined depth+stencil
    // format, and this engine has no stencil use today - falling back to a
    // combined depth+stencil format only if that isn't supported (rare on
    // desktop GPUs). Throws std::runtime_error if this device somehow
    // supports none of the candidates, which the Vulkan spec guarantees
    // cannot happen (at least one of D16_UNORM/X8_D24_UNORM_PACK32/
    // D32_SFLOAT must support this usage on every conformant
    // implementation).
    VkFormat PickDepthFormat() const;

    VkQueue GraphicsQueue() const noexcept { return m_graphicsQueue; }
    VkQueue PresentQueue() const noexcept { return m_presentQueue; }
    std::uint32_t GraphicsQueueFamily() const noexcept { return m_graphicsFamily; }
    std::uint32_t PresentQueueFamily() const noexcept { return m_presentFamily; }

    // Whether (and how precisely) this physical device can do GPU
    // timestamp queries - queried ONCE, in the constructor (see
    // QueryTimestampCapability()), and never re-checked afterward (Vulkan
    // device capabilities do not change at runtime). Phase 4A
    // (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - no VkQueryPool is
    // created anywhere yet; this is purely a capability probe. See
    // Renderer/GpuTiming.h for GpuTimestampCapability's own definition and
    // InterpretTimestampCapability()'s pure decision logic.
    const GpuTimestampCapability& TimestampCapability() const noexcept { return m_timestampCapability; }

private:
    void PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void CreateLogicalDevice();
    // Queries this physical device's raw timestamp-related limits
    // (vkGetPhysicalDeviceProperties()'s
    // limits.timestampComputeAndGraphics/limits.timestampPeriod,
    // vkGetPhysicalDeviceQueueFamilyProperties()'s
    // families[m_graphicsFamily].timestampValidBits) and interprets them
    // via InterpretTimestampCapability() (Renderer/GpuTiming.h) - mirrors
    // PickDepthFormat()'s own "ask the device once, expose via accessor"
    // shape, except this one is eagerly computed and cached in the
    // constructor rather than callable on demand, since every consumer
    // needs the same fixed answer for this device's entire lifetime.
    // Never throws - an unsupported result is a completely normal outcome
    // (see the Vulkan spec: a timestampPeriod of 0 or timestampValidBits
    // of 0 are BOTH valid "not supported" signals, never assumed
    // otherwise).
    void QueryTimestampCapability();
    void Destroy() noexcept;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = 0;
    std::uint32_t m_presentFamily = 0;

    GpuTimestampCapability m_timestampCapability;
};

} // namespace gte
