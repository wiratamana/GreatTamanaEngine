#pragma once

#include <volk.h>

#include "GpuTiming.h"
#include "Vulkan/VulkanQueryPool.h"

#include <array>
#include <cstdint>
#include <optional>

namespace gte {

class VulkanDevice;


// Owns the VkQueryPool behind the offscreen GPU timestamp measurements: 4 slots, a start/end pair for each of
// Offscreen0 and Offscreen1. Every Record*/Read* call is a cheap no-op when GTE_ENABLE_PROFILER is off or capture
// is disabled. Does not own the VulkanDevice, which must outlive it.
class GpuTimingService {
public:
    // `capability` comes from VulkanDevice::TimestampCapability(); a build without GTE_ENABLE_PROFILER forces
    // `supported` to false. `vulkanDevice` is used only for the one-time pool reset and is not stored.
    GpuTimingService(VulkanDevice& vulkanDevice, const GpuTimestampCapability& capability);
    ~GpuTimingService() = default;

    GpuTimingService(const GpuTimingService&) = delete;
    GpuTimingService& operator=(const GpuTimingService&) = delete;

    GpuTimingService(GpuTimingService&&) = default;
    GpuTimingService& operator=(GpuTimingService&&) = default;

    // Permanent support (device capable and profiler compiled in); independent of SetCaptureEnabled().
    bool IsSupported() const noexcept { return m_capability.supported; }

    // Run-time capture switch; defaults to true.
    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Records a query reset for the pass's two slots, then a TOP_OF_PIPE timestamp. No-op unless supported and
    // capturing. `slot` must be Offscreen0 or Offscreen1.
    void RecordOffscreenPassStart(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept;
    // Records a BOTTOM_OF_PIPE timestamp; same guard as the start call.
    void RecordOffscreenPassEnd(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept;
    // Reads the start/end pair, converts to milliseconds, caches and returns it. Call only after the caller's
    // own fence wait; this never waits.
    GpuTimingSample ReadOffscreenResultNow(GpuTimingSlot slot) noexcept;

    // Last sample cached by a Read* call; never touches Vulkan.
    GpuTimingSample LastKnown(GpuTimingSlot slot) const noexcept;

private:
    // Whole-pool slot of the START timestamp (END is +1). Offscreen0/Offscreen1 only.
    static std::uint32_t OffscreenSlotBase(GpuTimingSlot slot) noexcept;

    // Reads the two slots at `slotBase` into a sample; call only once a read is known to be safe.
    GpuTimingSample ReadResultAt(std::uint32_t slotBase) noexcept;

    // One-time whole-pool reset on a throwaway command pool: the service is built before GpuResourceFactory, so
    // it cannot use ImmediateSubmit(). The submit still goes through VulkanDevice::SubmitToGraphicsQueue().
    void WarmUpResetEntirePool(VulkanDevice& vulkanDevice);

    VkDevice m_device = VK_NULL_HANDLE;
    GpuTimestampCapability m_capability;
    bool m_captureEnabled = true;

    // Empty whenever !m_capability.supported.
    std::optional<VulkanQueryPool> m_queryPool;

    // One cached sample per logical pass; the raw query slots are not mirrored 1:1.
    std::array<GpuTimingSample, kGpuTimingSlotCount> m_lastKnown{};
};

} // namespace gte
