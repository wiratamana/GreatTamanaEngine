#pragma once

#include <volk.h>

#include "GpuTiming.h"
#include "Vulkan/VulkanQueryPool.h"

#include <array>
#include <cstdint>
#include <optional>

namespace gte {

// Phase 4B (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - owns the one
// VkQueryPool (via VulkanQueryPool) backing every GPU timestamp measurement
// this engine ever takes, plus the actual vkCmdResetQueryPool/
// vkCmdWriteTimestamp2/vkGetQueryPoolResults call sites. FramePresenter only
// ever calls INTO this class - it never issues those Vulkan calls itself,
// keeping FramePresenter focused on ORCHESTRATING *when* to call these (the
// same division of labor it already has with VulkanSwapchain/
// VulkanFrameSync), never on the raw query-pool mechanics themselves.
//
// Fixed, 8-slot pool layout, decided once and never resized/recreated for
// this object's entire lifetime:
//   0,1 - GpuTimingSlot::Offscreen0 start/end
//   2,3 - GpuTimingSlot::Offscreen1 start/end
//   4,5 - GpuTimingSlot::SwapchainPresent, frame-in-flight 0, start/end
//   6,7 - GpuTimingSlot::SwapchainPresent, frame-in-flight 1, start/end
// See OffscreenSlotBase()/PresentSlotBase() below for the exact indexing.
//
// Gated by BOTH a compile-time switch (GTE_ENABLE_PROFILER - see this
// class's own constructor) and a runtime switch (SetCaptureEnabled(),
// mirroring Profiling::FrameProfiler::SetCaptureEnabled() exactly, by name
// and by shape) - when EITHER is "off", every Record*/Read* method below is
// a safe, cheap no-op that never touches Vulkan at all. See AGENTS.md's
// "Profiling" section for the two-layer convention this mirrors, and
// PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own Step 2.3 for why GPU
// timestamp queries (unlike Phase 3's DrawStats accumulation, which rides
// for free on an already-necessary per-frame iteration) need this gate at
// all: a vkCmdResetQueryPool/vkCmdWriteTimestamp2/vkGetQueryPoolResults call
// is genuinely additional, non-free GPU/driver work that must be skippable
// both at compile time and at runtime.
//
// Phase 4B itself wires up this WHOLE class - every method below already
// exists and is fully implemented - but nothing in FramePresenter calls any
// of the Record*/Read* methods yet; that wiring is Phase 4C (Offscreen)/4D
// (Present)'s own job. This class is Tier 2 (needs a real VkDevice/VkQueue),
// the same accepted bucket as Buffer/RenderTexture/Pipeline (see
// TESTING.md) - only the pure tri-state RESOLUTION logic it builds on
// (ResolveGpuTimingStatus(), GpuTiming.h) is pulled out as Tier-1-testable.
//
// Does NOT own the VkDevice/VkQueue passed in - both must outlive this
// object, same non-ownership convention as every other Vulkan/*-adjacent
// class in this engine.
class GpuTimingService {
public:
    // `capability` is whatever VulkanDevice::TimestampCapability() actually
    // reported for this device (see GpuTiming.h) - this constructor may
    // still override `supported` to false internally (see its own body)
    // when GTE_ENABLE_PROFILER is compiled OFF, regardless of what the
    // device itself can do. `graphicsQueue`/`graphicsQueueFamily` are only
    // used for this constructor's own one-time, up-front, whole-pool
    // reset (see the .cpp) - never stored.
    GpuTimingService(
        VkDevice device, VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily, const GpuTimestampCapability& capability);
    ~GpuTimingService() = default;

    GpuTimingService(const GpuTimingService&) = delete;
    GpuTimingService& operator=(const GpuTimingService&) = delete;

    GpuTimingService(GpuTimingService&&) = default;
    GpuTimingService& operator=(GpuTimingService&&) = default;

    // Whether this service can EVER produce real GPU timing data for the
    // rest of this process's lifetime - false whenever the device itself
    // reported no timestamp support (GpuTimestampCapability::supported ==
    // false) OR this was built with GTE_ENABLE_PROFILER=OFF (see the
    // constructor). Deliberately independent of SetCaptureEnabled() below:
    // a capable, compiled-in service with capture currently disabled still
    // reports IsSupported() == true - capture is a TEMPORARY condition,
    // support is a PERMANENT one for this process, and the two must never
    // be confused (see ResolveGpuTimingStatus(), GpuTiming.h).
    bool IsSupported() const noexcept { return m_capability.supported; }

    // Mirrors Profiling::FrameProfiler::SetCaptureEnabled() by name and by
    // shape on purpose (see Renderer::SetGpuTimingCaptureEnabled(), which
    // Application::Run() will call once per frame starting in Phase 4C) -
    // the RUNTIME layer of this class's two-layer on/off gate. Defaults to
    // true, so a caller that never calls this at all (true throughout
    // Phase 4B, since Application.cpp isn't wired to call it until Phase
    // 4C) behaves exactly as if this whole feature were simply "always
    // capturing" - matching FrameProfiler's own default.
    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Records vkCmdResetQueryPool (both slots for this pass) followed by a
    // TOP_OF_PIPE timestamp write into `cmd` - a safe no-op (no Vulkan call
    // at all) whenever !IsSupported() || !IsCaptureEnabled(). `slot` must be
    // GpuTimingSlot::Offscreen0 or GpuTimingSlot::Offscreen1 (never
    // SwapchainPresent - see RecordPresentPassStart() below for that one).
    // Not yet called from anywhere in production code - see this class's
    // own doc comment above; wired up starting in Phase 4C.
    void RecordOffscreenPassStart(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept;
    // Records a BOTTOM_OF_PIPE timestamp write into `cmd` - same guard as
    // RecordOffscreenPassStart() above.
    void RecordOffscreenPassEnd(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept;
    // Reads back the two timestamps written by the RecordOffscreenPassStart()/
    // RecordOffscreenPassEnd() pair above, converts to milliseconds, caches
    // the result (see LastKnown() below), and returns it. Safe to call ONLY
    // once the caller has already confirmed (via its own fence wait) that
    // the corresponding submission is fully complete - never adds a wait of
    // its own (see PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own "no new
    // stall, ever" refusal).
    GpuTimingSample ReadOffscreenResultNow(GpuTimingSlot slot) noexcept;

    // Same shape as the offscreen trio above, but indexed by frame-in-flight
    // index (0..kGpuTimingFramesInFlight-1, GpuTiming.h) rather than a
    // GpuTimingSlot - see PresentSlotBase() below.
    void RecordPresentPassStart(VkCommandBuffer cmd, std::uint32_t frameInFlightIndex) noexcept;
    void RecordPresentPassEnd(VkCommandBuffer cmd, std::uint32_t frameInFlightIndex) noexcept;
    // Unlike ReadOffscreenResultNow() above, this ALSO honors the
    // Present-path's own warm-up bookkeeping (see MarkPresentSlotWritten()
    // below): returns (and caches) Absent, without touching Vulkan at all,
    // until this exact frame-in-flight slot has genuinely been written at
    // least once - see PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own
    // "why a per-slot flag, not a frame-count heuristic" reasoning (Phase
    // 4D).
    GpuTimingSample ReadPresentResultIfAvailable(std::uint32_t frameInFlightIndex) noexcept;
    // Marks that RecordPresentPassStart()/RecordPresentPassEnd() actually
    // recorded a real write for this frame-in-flight slot this call - a
    // safe no-op under the same !IsSupported()||!IsCaptureEnabled() guard as
    // the Record* methods above, so a caller can call this unconditionally
    // right after them with no separate `if` of its own.
    void MarkPresentSlotWritten(std::uint32_t frameInFlightIndex) noexcept;

    // A cheap, side-effect-free read of whatever was last cached for `slot`
    // by one of the Read* methods above - never touches Vulkan. Safe to
    // call any number of times, including zero times in a frame where the
    // corresponding pass didn't run (in which case it simply still holds
    // whatever it last held).
    GpuTimingSample LastKnown(GpuTimingSlot slot) const noexcept;

private:
    // Raw, whole-pool slot index for a given offscreen GpuTimingSlot's START
    // timestamp (its END is always this + 1) - valid only for
    // Offscreen0/Offscreen1 (PresentSlotBase() below is Present's
    // equivalent).
    static std::uint32_t OffscreenSlotBase(GpuTimingSlot slot) noexcept;
    // Raw, whole-pool slot index for a given frame-in-flight index's START
    // timestamp (its END is always this + 1) - offset past the 4 offscreen
    // slots above.
    static std::uint32_t PresentSlotBase(std::uint32_t frameInFlightIndex) noexcept;

    // Actually issues vkGetQueryPoolResults for the 2-slot range starting at
    // `slotBase` and converts the result to a Status::Present
    // GpuTimingSample - only ever called once a caller has already resolved
    // (via ResolveGpuTimingStatus()) that a real read is safe/expected.
    GpuTimingSample ReadResultAt(std::uint32_t slotBase) noexcept;

    // One-time, up-front, whole-pool reset via a throwaway one-shot command
    // buffer - see the .cpp for the full reasoning. Deliberately
    // self-contained (its own throwaway VkCommandPool, never
    // Renderer::ImmediateSubmit()) since GpuTimingService is constructed
    // before GpuResourceFactory even exists (see Renderer.cpp's member
    // declaration order) and so cannot depend on it.
    void WarmUpResetEntirePool(VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily);

    VkDevice m_device = VK_NULL_HANDLE;
    GpuTimestampCapability m_capability;
    bool m_captureEnabled = true;

    // Empty (std::nullopt) whenever !m_capability.supported - see the
    // constructor's own #if GTE_ENABLE_PROFILER handling. Every Record*/
    // Read* method below checks IsSupported() before ever touching this.
    std::optional<VulkanQueryPool> m_queryPool;

    // One cached sample per LOGICAL pass (Offscreen0/Offscreen1/
    // SwapchainPresent, kGpuTimingSlotCount == 3) - the 8 raw query slots
    // above are an implementation detail this cache doesn't mirror 1:1.
    std::array<GpuTimingSample, kGpuTimingSlotCount> m_lastKnown{};

    // Present-path warm-up bookkeeping - see ReadPresentResultIfAvailable()/
    // MarkPresentSlotWritten() above.
    std::array<bool, kGpuTimingFramesInFlight> m_presentSlotEverWritten{};
};

} // namespace gte
