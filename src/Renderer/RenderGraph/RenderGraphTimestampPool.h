#pragma once

// B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the render-graph-native source
// of real GPU timestamp measurements: owns TWO independent, fixed-size
// VkQueryPools (one per gte::rg::RenderGraph::ExecuteTimingMode regime -
// see RenderGraph.h's own top-of-file comment for why the synchronous-
// offscreen and pipelined-present regimes must never share one slot
// range), keyed by an arbitrary, name-assigned slot index
// (RenderGraphNameSlotTable) rather than a fixed 3-value enum - the
// generalization GpuTimingService's own fixed GpuTimingSlot design was
// always going to need once real, named render-graph passes existed to
// time (see this campaign's own "GPU TIMING NOTE" in RenderGraph.h).
//
// Deliberately a BRAND-NEW, dedicated class rather than an in-place
// extension of GpuTimingService (Vulkan/VulkanQueryPool.h/GpuTimingService.h)
// - confirmed, by a full-repository grep performed as part of this same
// change, that GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent have
// ZERO remaining real (non-nullopt) production callers as of Phase 7's
// Application::Run() migration onto the render graph (Renderer::RenderOffscreen()
// is only ever called with std::nullopt, by src/Editor/AssetPreviewMesh.cpp/
// BoneViewerWindow.cpp's own independent previews; Renderer::Present() -
// the legacy FrameRecorder-based path - has no production callers left at
// all). Modifying already-shipped, Tier-2 (manually-verified) production
// code with no real consumer left is a materially riskier, unforced change
// than adding this new, small, focused class alongside it - see
// B1_REAL_GPU_TIMING_STRATEGY_v1.md, Step 3.3/3.1 for the full reasoning.
// GpuTimingService/VulkanQueryPool/GpuTimingSlot are UNTOUCHED by this
// change - AssetPreviewMesh/BoneViewerWindow keep working exactly as
// before.
//
// Mirrors GpuTimingService's own two-layer on/off gate exactly
// (GTE_ENABLE_PROFILER at compile time forces IsSupported() to false
// regardless of the device's own reported capability; SetCaptureEnabled()
// at runtime), and reuses gte::VulkanQueryPool (Vulkan/VulkanQueryPool.h)
// as the actual RAII VkQueryPool owner for both regimes' pools, rather
// than hand-rolling a second copy of that same thin wrapper.
//
// This class is Tier 2 (needs a real VkDevice/VkQueue to construct/use at
// all) - the same accepted "no automated GPU-backed test coverage yet"
// bucket VulkanQueryPool/GpuTimingService/every Renderer/Vulkan/ class
// already lives in (see AGENTS.md, "Testability & Regression Safety").

#include "RenderGraphNameSlotTable.h"
#include "../GpuTiming.h"
#include "../Vulkan/VulkanQueryPool.h"

#include <volk.h>

#include <cstdint>
#include <optional>

namespace gte::rg {

class RenderGraphTimestampPool {
public:
    // One resolved [begin, end) raw GPU timestamp-counter pair for a single
    // slot - the caller (RenderGraph) is responsible for converting this
    // into milliseconds via gte::ConvertTimestampDeltaToMilliseconds() and
    // deciding its GpuTimingSample::Status via gte::ResolveGpuTimingStatus(),
    // exactly as GpuTimingService::ReadResultAt() already does for the old
    // fixed-enum system - this class stays a thin, "dumb" RAII/Vulkan-call
    // shell with no timing-conversion opinion of its own, same division of
    // labor VulkanQueryPool already has relative to GpuTimingService.
    struct RawTicks {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
    };

    // `capability` is whatever VulkanDevice::TimestampCapability() actually
    // reported (see Renderer::GetVulkanContextInfo()) - this constructor
    // may still override `supported` to false internally when
    // GTE_ENABLE_PROFILER is compiled OFF, regardless of what the device
    // itself can do (mirrors GpuTimingService's own constructor exactly).
    // `graphicsQueue`/`graphicsQueueFamily` are only used for this
    // constructor's own one-time, up-front, whole-pool warm-up reset (see
    // the .cpp) - never stored. `synchronousSlotBudget`/`pipelinedSlotBudget`
    // must match RenderGraph's own RenderGraphNameSlotTable budgets exactly
    // (see RenderGraph.h's kSynchronousTimingSlotBudget/
    // kPipelinedTimingSlotBudget) - a name-slot table assigning slot N must
    // always have a corresponding query-pool slot N to write into.
    // `pipelinedFramesInFlight` must match gte::kGpuTimingFramesInFlight
    // (GpuTiming.h) - reused here rather than hardcoded, for the same
    // "kept in sync by a named, shared constant" reason GpuTimingService
    // already documents for that constant.
    RenderGraphTimestampPool(VkDevice device, VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily,
        const GpuTimestampCapability& capability, std::uint32_t synchronousSlotBudget,
        std::uint32_t pipelinedSlotBudget, std::uint32_t pipelinedFramesInFlight);
    ~RenderGraphTimestampPool() = default;

    RenderGraphTimestampPool(const RenderGraphTimestampPool&) = delete;
    RenderGraphTimestampPool& operator=(const RenderGraphTimestampPool&) = delete;
    RenderGraphTimestampPool(RenderGraphTimestampPool&&) = delete;
    RenderGraphTimestampPool& operator=(RenderGraphTimestampPool&&) = delete;

    // Whether this pool can EVER produce real GPU timing data for the rest
    // of this process's lifetime - false whenever the device itself
    // reported no timestamp support OR this was built with
    // GTE_ENABLE_PROFILER=OFF. Deliberately independent of
    // IsCaptureEnabled() below - see GpuTimingService::IsSupported()'s own
    // doc comment for why the two must never be confused.
    bool IsSupported() const noexcept { return m_capability.supported; }
    const GpuTimestampCapability& Capability() const noexcept { return m_capability; }

    // The runtime layer of this class's two-layer on/off gate - mirrors
    // GpuTimingService::SetCaptureEnabled() by name/shape exactly. Driven
    // once per frame by RenderGraph::SetGpuTimingCaptureEnabled(), which
    // Application::Run() calls alongside its existing
    // Renderer::SetGpuTimingCaptureEnabled() call.
    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Resets this slot's own 2-query range (this slot's previous begin/end
    // pair, if any) and immediately writes a fresh TOP_OF_PIPE timestamp
    // into the first of the two - a safe no-op (no Vulkan call at all)
    // whenever !IsSupported() || !IsCaptureEnabled() || slot == kNoNameSlot.
    //
    // Deliberately reset-then-write in ONE call, with no separate
    // "reset the whole regime's range up front" step - this is safe in
    // BOTH regimes because the caller (RenderGraph::ExecuteCompiledGraph())
    // never reaches this call until it has already, independently, proven
    // (via a pre-existing fence wait it did not add for this purpose) that
    // whatever this exact slot was last used for has fully completed:
    // the SYNCHRONOUS regime's previous call already blocked on
    // Renderer::EndOffscreenRenderGraphRecording() before this frame's
    // Execute() call could even begin recording; the PIPELINED regime's
    // `bufferIndex` (a frame-in-flight index) was already fence-waited on
    // by FramePresenter::PresentViaRenderGraph() BEFORE it ever calls
    // RenderGraph::Execute() at all. See B1_REAL_GPU_TIMING_STRATEGY_v1.md,
    // Step 3.6/3.7, and mirrors GpuTimingService::RecordOffscreenPassStart()/
    // RecordPresentPassStart()'s own identical reset-then-write convention.
    void WriteBegin(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept;

    // Writes a BOTTOM_OF_PIPE timestamp into this slot's second query -
    // same guard as WriteBegin() above.
    void WriteEnd(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept;

    // Reads back the [begin, end) tick pair most recently written for this
    // slot via vkGetQueryPoolResults() - the caller must already know (via
    // its own synchronization, never added by this call) that the
    // corresponding submission is complete. Returns RawTicks{} (both zero)
    // for slot == kNoNameSlot or !IsSupported() - the caller is expected to
    // have already resolved a real GpuTimingSample::Status before deciding
    // whether to call this at all (mirrors GpuTimingService::ReadResultAt()'s
    // own "only called once a real read is safe/expected" convention).
    RawTicks ReadBack(bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept;

private:
    std::uint32_t QueryBase(bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) const noexcept;
    void WarmUpResetPool(VulkanQueryPool& pool, VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily);

    VkDevice m_device = VK_NULL_HANDLE;
    GpuTimestampCapability m_capability;
    bool m_captureEnabled = true;

    // Needed by QueryBase() to compute a pipelined slot's raw query index -
    // see that method's own implementation.
    std::uint32_t m_pipelinedSlotBudget = 0;

    // Empty (std::nullopt) whenever !m_capability.supported - mirrors
    // GpuTimingService::m_queryPool's own optional-ness exactly. Every
    // WriteBegin()/WriteEnd()/ReadBack() call checks IsSupported() before
    // ever touching either of these.
    std::optional<VulkanQueryPool> m_synchronousPool;
    std::optional<VulkanQueryPool> m_pipelinedPool;
};

} // namespace gte::rg
