#pragma once

// GPU timestamp capability and pure conversion math. Vulkan-free, so it is testable without a device.

#include <cstdint>

namespace gte {

// Frames in flight of the presenter; the pipelined timestamp regions and the presenter's slots both use it.
inline constexpr std::uint32_t kGpuFramesInFlight = 2;

// What one physical device can do for timestamp queries; resolved once at device creation.
// `supported == false` is a normal outcome, not an error.
struct GpuTimestampCapability {
    bool supported = false;
    float timestampPeriodNs = 0.0f;
    std::uint32_t validBits = 0;
};

// Interprets the device's raw timestamp limits.
// A zero period or zero validBits each mean "not supported"; `supported` needs all three inputs to agree.
GpuTimestampCapability InterpretTimestampCapability(
    bool timestampComputeAndGraphics, float timestampPeriod, std::uint32_t validBits) noexcept;

// Converts a raw timestamp delta to milliseconds. Both ticks are masked to validBits before subtracting, so one
// counter wraparound stays correct. Returns 0 when timestampPeriodNs <= 0.
double ConvertTimestampDeltaToMilliseconds(
    std::uint64_t startTicks, std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t validBits) noexcept;

} // namespace gte
