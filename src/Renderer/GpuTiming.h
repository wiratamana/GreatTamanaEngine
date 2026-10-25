#pragma once

#include <cstdint>

namespace gte {

// GPU timestamp capability probing and pure conversion/indexing math. Vulkan-free so it is testable without a
// device; VulkanDevice.h includes it to share GpuTimestampCapability.

// What one physical device can do for timestamp queries; resolved once at device creation.
// `supported == false` is a normal outcome, not an error.
struct GpuTimestampCapability {
    bool supported = false;
    float timestampPeriodNs = 0.0f;
    std::uint32_t validBits = 0;
};

// Interprets the device's raw timestamp limits (VulkanDevice passes the real queried values).
// A zero period or zero validBits each mean "not supported"; `supported` needs all three inputs to agree.
GpuTimestampCapability InterpretTimestampCapability(
    bool timestampComputeAndGraphics, float timestampPeriod, std::uint32_t validBits) noexcept;

// One resolved timing measurement; a Renderer-local tri-state so Renderer needs no Profiling header.
struct GpuTimingSample {
    enum class Status : std::uint8_t {
        Absent,
        Present,
        Unsupported,
    };

    Status status = Status::Absent;
    double milliseconds = 0.0;
};

// Priority: Unsupported (permanent) beats Absent (no data right now: capture off or not yet written) beats Present.
inline GpuTimingSample::Status ResolveGpuTimingStatus(
    bool supported, bool captureEnabled, bool hasWrittenData) noexcept
{
    if (!supported) {
        return GpuTimingSample::Status::Unsupported;
    }
    if (!captureEnabled || !hasWrittenData) {
        return GpuTimingSample::Status::Absent;
    }
    return GpuTimingSample::Status::Present;
}

// Converts a raw timestamp delta to milliseconds. Both ticks are masked to validBits before subtracting, so one
// counter wraparound stays correct. Returns 0 when timestampPeriodNs <= 0.
double ConvertTimestampDeltaToMilliseconds(
    std::uint64_t startTicks, std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t validBits) noexcept;

// Frames in flight the pipelined pass timers cycle through; must equal FramePresenter::kFramesInFlight.
inline constexpr std::uint32_t kGpuTimingFramesInFlight = 2;

} // namespace gte
