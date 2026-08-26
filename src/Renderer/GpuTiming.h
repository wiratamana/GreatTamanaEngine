#pragma once

#include <cstdint>

namespace gte {

// Phase 4A (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - GPU timestamp
// capability probing + pure conversion/indexing math, and NOTHING else: no
// VkQueryPool is created anywhere in this phase, no vkCmdWriteTimestamp2/
// vkCmdResetQueryPool/vkGetQueryPoolResults call exists yet (that's Phase
// 4B's job - see GpuTimingService, added then). Deliberately
// Vulkan-header-free (no <volk.h>, no vulkan/vulkan.h anywhere in this
// file) - mirrors src/Renderer/DrawStats.h's own precedent exactly, so this
// whole file stays trivially Tier-1-testable with no live VkDevice at all
// (see tests/Renderer/GpuTimingTests.cpp). VulkanDevice.h
// (Renderer/Vulkan/VulkanDevice.h) includes this header so its own
// GpuTimestampCapability accessor shares this one definition rather than a
// second, structurally identical copy living in two places.

// What one physical device can actually do re: GPU timestamp queries -
// resolved once, at device-creation time (see
// VulkanDevice::TimestampCapability()), and never re-checked afterward
// (Vulkan device capabilities do not change at runtime). `supported ==
// false` is a completely normal, silently handled outcome on real hardware
// (see AGENTS.md's "degrade gracefully" convention throughout this
// codebase) - never treated as an error anywhere this is consumed.
struct GpuTimestampCapability {
    bool supported = false;
    float timestampPeriodNs = 0.0f;
    std::uint32_t validBits = 0;
};

// Pure interpretation of a device's raw timestamp-related limits into a
// GpuTimestampCapability - extracted specifically so this DECISION can be
// unit-tested directly with hand-fabricated inputs (see
// tests/Renderer/GpuTimingTests.cpp), without needing a real
// VkPhysicalDevice. VulkanDevice's own constructor calls this with real,
// device-queried arguments (vkGetPhysicalDeviceProperties()'s
// limits.timestampComputeAndGraphics/limits.timestampPeriod, and
// vkGetPhysicalDeviceQueueFamilyProperties()'s
// families[graphicsFamily].timestampValidBits) - see VulkanDevice.cpp's
// QueryTimestampCapability().
//
// `timestampPeriod == 0.0f` and `validBits == 0` are BOTH independently
// valid "not supported" signals per the Vulkan spec (never assumed
// positive/non-zero) - `supported` is only ever true when ALL THREE inputs
// indicate real support. Never throws - an "unsupported" result is a
// perfectly normal outcome, not an error condition.
GpuTimestampCapability InterpretTimestampCapability(
    bool timestampComputeAndGraphics, float timestampPeriod, std::uint32_t validBits) noexcept;

// One of the three logical GPU passes this engine can time. Deliberately
// GENERIC names (not GameView/SceneView) - Renderer/FramePresenter must
// never need to know Editor-facing pass naming; that mapping is
// Application.cpp's job alone, exactly as it already is for
// Profiling::GpuPass (see AGENTS.md's "Profiling" section, and
// PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's "Overall API surface"
// section).
enum class GpuTimingSlot : std::uint32_t {
    Offscreen0 = 0,
    Offscreen1 = 1,
    SwapchainPresent = 2,
};

inline constexpr std::uint32_t kGpuTimingSlotCount = 3;

// A single resolved GPU timing measurement for one GpuTimingSlot - the
// Renderer-local tri-state mirror of Profiling::GpuSampleStatus,
// deliberately a SEPARATE type (never Profiling::GpuSampleStatus itself) -
// keeps Renderer/FramePresenter/this file completely free of any
// Profiling/ header. Bridging the two tri-states into a real
// Profiling::GpuPass/SetGpuPassTiming() call is Application.cpp's job
// alone (see PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own Phase 4C).
struct GpuTimingSample {
    enum class Status : std::uint8_t {
        Absent,
        Present,
        Unsupported,
    };

    Status status = Status::Absent;
    double milliseconds = 0.0;
};

// Phase 4B (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - the pure
// tri-state PRIORITY decision GpuTimingService::ReadOffscreenResultNow()/
// ReadPresentResultIfAvailable() both build on, extracted specifically so
// this DECISION can be unit-tested directly with all 8 combinations of its
// 3 boolean inputs, without needing a real VkDevice/VkQueryPool (mirrors
// InterpretTimestampCapability()'s own "extract the pure decision, keep the
// live-device call site a thin wrapper around it" precedent above).
//
// Priority order, deliberately fixed: `Unsupported` (this device/build can
// NEVER produce this measurement - a permanent condition) wins over
// `Absent` (no data available RIGHT NOW - a temporary condition, e.g.
// capture currently disabled, or a Present-path slot not yet warmed up)
// wins over `Present` (a real, freshly-read measurement). `Unsupported`
// must never be confused with a temporarily-disabled `Absent`, and vice
// versa - see GpuTimingService::SetCaptureEnabled()'s own doc comment.
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

// Converts a [startTicks, endTicks) raw GPU timestamp-counter delta into
// milliseconds, given this device's timestampPeriodNs (nanoseconds per
// tick) and validBits (the counter's actual bit width - a Vulkan timestamp
// counter is not guaranteed a full 64 bits). Pure, allocation-free, no
// Vulkan dependency whatsoever - mirrors DrawStats.h's
// AccumulateDrawStats() in spirit (a small, hand-verifiable pure
// transform, safe to call every frame at zero real cost).
//
// Both startTicks/endTicks are masked to validBits BEFORE subtracting, so
// a delta computed as ((end & mask) - (start & mask)) & mask stays correct
// across exactly one counter wraparound - the only case that can
// plausibly ever be hit within one single frame's measurement bracket.
//
// Returns 0.0 immediately if timestampPeriodNs <= 0.0f - this should never
// actually be reached in practice (callers are expected to check
// GpuTimestampCapability::supported first), but costs nothing to guard and
// removes an entire class of "what if this is called wrong" question.
double ConvertTimestampDeltaToMilliseconds(
    std::uint64_t startTicks, std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t validBits) noexcept;

// How many swapchain frames-in-flight the Present-pass query pool needs
// its own independent slot set for. Kept as ITS OWN constant (rather than
// reaching into FramePresenter's private kFramesInFlight) because this
// file is deliberately designed to have ZERO dependency on
// FramePresenter.h - it is a lower-level, Vulkan-free pure-math file
// FramePresenter itself will depend on (starting in Phase 4B/4D), never
// the other way around. Must always agree in VALUE with
// FramePresenter::kFramesInFlight - kept as two separate named constants
// on purpose (see PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own design
// decision log) rather than one shared symbol; a code comment
// cross-referencing the two by name is how they're kept from silently
// drifting apart, not a shared #include.
inline constexpr std::uint32_t kGpuTimingFramesInFlight = 2;

// The raw, Present-pass-LOCAL query-pool slot index where a given
// frame-in-flight index's START timestamp lives; its END timestamp always
// lives at PresentTimestampSlotBase(frameInFlightIndex) + 1. Pure integer
// math, deliberately simple/readable over a "clever" bit-packing scheme -
// see PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own fixed 8-slot pool
// layout (Phase 4B adds the actual VkQueryPool this indexes into, offset
// by whatever fixed base the Offscreen0/Offscreen1 slots occupy - this
// function only ever returns the Present-local 0/2 pair, not a final,
// whole-pool index).
constexpr std::uint32_t PresentTimestampSlotBase(std::uint32_t frameInFlightIndex) noexcept
{
    return frameInFlightIndex * 2;
}

} // namespace gte
