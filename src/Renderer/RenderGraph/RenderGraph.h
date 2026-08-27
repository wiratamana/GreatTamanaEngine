#pragma once

// Phase 6 (RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md, part 6 of the
// wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - "Put Everything
// Together": the class that assembles Phases 1-5 (RenderGraphTypes,
// RenderGraphBuilder, RenderGraphCompiler, RenderGraphResourcePool,
// RenderGraphBarrierPlanner) into one coherent, callable object. A caller
// builds a RenderGraphBuilder fresh every call (via the `build` callback
// below), declares passes/resources against it, and RenderGraph compiles
// it, resolves every declared virtual resource to a real physical one
// (imported as-is, or pooled/reused via RenderGraphResourcePool), emits
// every barrier a pass's declared reads/writes require, records each
// surviving pass's real Vulkan work (vkCmdBeginRendering/vkCmdSetViewport/
// vkCmdSetScissor -> that pass's own `execute` callback -> vkCmdEndRendering),
// and remembers each pass's own DrawStats (and, in a future phase, real GPU
// timing) under its declared name for LastKnownStatsFor() to serve back.
//
// By the end of this phase, RenderGraph is fully capable of replacing
// FrameRecorder + the pass-orchestration half of FramePresenter - but
// NOTHING calls it yet (see RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md's own
// "must not leave the engine in a worse state" rule) - that cut-over is
// Phase 7's job specifically.
//
// --- Execute() cardinality (RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md's own
// V2 Revision Note 2) ---
//
// This engine has TWO genuinely different submission/synchronization
// regimes today (see FramePresenter.cpp): RenderOffscreen() (Game/Scene)
// records into its own dedicated command buffer and blocks SYNCHRONOUSLY on
// its own fence before returning; Present() (the swapchain) is deliberately
// NON-blocking/pipelined across kFramesInFlight command buffers. A render
// graph execution model must respect both, never silently merge them into
// one shared command buffer/one Execute() call (that would either force
// Present to become synchronous too - a real frame-pacing regression - or
// require unspecified multi-submission machinery). Per Phase 0's own
// decision: RenderGraph::Execute() is called exactly TWICE per frame, once
// per regime, tagged via ExecuteTimingMode below - never once, never more
// than twice.
//
// By CONVENTION (documented here, enforced by Phase 7's own call order, not
// by this class): the SynchronousImmediateReadback call happens FIRST each
// frame (covering Game view + Scene view together, since both already share
// that regime and their relative order doesn't matter), and is the ONE call
// that triggers RenderGraphResourcePool::BeginFrame() - see Execute()'s own
// implementation. The PipelinedDeferredReadback call (covering Present
// alone) happens second and does NOT re-trigger BeginFrame() - a pooled
// resource claimed during the first call must stay correctly marked
// "claimed this frame" through the second call too.

#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphNameSlotTable.h"
#include "RenderGraphResourcePool.h"
#include "RenderGraphTimestampPool.h"
#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../DrawStats.h"
#include "../GpuTiming.h"

#include <volk.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Fully specifies the `struct PassContext;` forward-declared by Phase 1
// (RenderGraphTypes.h) - PassRecord::execute is a
// `std::function<void(PassContext&)>`, invoked exactly once per surviving
// pass by RenderGraph::Execute() below (see PassRecord's own doc comment).
//
// Deliberately kept as small as this phase actually needs - see
// RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md's own "Step 5: Their
// Role": "it is far easier to ADD a method to PassContext later than to
// have over-designed it now against imagined future passes". Both
// `resolveReadTexture`/`recordDraw` are plain std::function fields (rather
// than a virtual interface/friend-only private state) so this struct stays
// a simple, copyable value with no dependency on RenderGraph's own private
// implementation types.
struct PassContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    // The extent of this pass's own resolved color attachment (zero for a
    // pass that declared no ColorAttachmentWrite - e.g. a future
    // transfer-only/compute-only pass, which never gets a
    // vkCmdBeginRendering bracket at all - see RenderGraph::Execute()'s own
    // implementation comment).
    VkExtent2D colorAttachmentExtent{};

    struct ResolvedTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    // Resolves a texture this pass declared as a READ (via
    // PassBuilder::ReadTexture()) into its already-live VkImageView/
    // VkSampler pair, wired up by RenderGraph::Execute() right before
    // invoking this pass's `execute` callback. Returns a null view/sampler
    // for a handle that never resolved to a physical texture this call
    // (including an imported resource, which carries no VkSampler of its
    // own - see RenderGraphBuilder::ImportTexture()'s own TextureImportInfo,
    // which has no sampler field).
    std::function<ResolvedTexture(TextureHandle)> resolveReadTexture;

    // Phase 6 (COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md) -
    // a plain alias of resolveReadTexture() above with a name that no
    // longer implies "reads only": resolves ANY texture handle this pass
    // declared, whether via ReadTexture() OR PassBuilder::WriteTexture()
    // (a compute shader's RWTexture output) - a write-only handle is
    // resolved just as early as a read one (RenderGraph::Execute() resolves
    // every declared read AND write before a pass's own `execute` callback
    // runs), so this is safe to call for either direction. Intended for a
    // compute pass's `execute` callback to use when rewriting its own
    // ComputeDescriptorSet (see Renderer/ComputeDescriptorSet.h) against
    // the CURRENT physical resource behind a declared handle, right before
    // calling Renderer::Dispatch().
    std::function<ResolvedTexture(TextureHandle)> resolveTexture;

    // The buffer sibling of resolveTexture() above - resolves a declared
    // BufferHandle (via ReadBuffer()/WriteBuffer()) into its CURRENT
    // physical VkBuffer, for the exact same "rewrite my own
    // ComputeDescriptorSet before dispatching" use case. Returns
    // VK_NULL_HANDLE for a handle that never resolved to a physical buffer
    // this call.
    std::function<VkBuffer(BufferHandle)> resolveBuffer;

    // Called by a pass's `execute` callback immediately alongside issuing a
    // real vkCmdDraw/vkCmdDrawIndexed, so this pass's own DrawStats tally
    // stays fused to the exact call site that actually issued the draw -
    // mirroring Renderer::Submit()'s existing shape/semantics (see
    // AGENTS.md's "Profiling" section, AccumulateDrawStats()'s own
    // correctness rule) but scoped per-PASS here instead of per-frame-queue,
    // since a pass's execute callback is opaque, caller-authored Vulkan code
    // RenderGraph itself never sees the inside of (unlike FrameRecorder's
    // own single, closed draw-queue loop).
    std::function<void(bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount)> recordDraw;
};

// Which of this engine's two real submission regimes an Execute() call
// belongs to - see this header's own top comment for the full reasoning.
enum class ExecuteTimingMode : std::uint8_t {
    SynchronousImmediateReadback,
    PipelinedDeferredReadback,
};

// PassGpuStats itself now lives in RenderGraphSnapshot.h (Phase 8 -
// RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md), so the Editor's
// "Render Graph" panel snapshot-building code (BuildRenderGraphSnapshot())
// and this class's own LastKnownStatsFor()/UpdateDrawStatsFor()/
// UpdateTimingFor() below share exactly one definition rather than two.
// `timing` is a genuine gte::GpuTimingSample (Renderer-local tri-state, see
// GpuTiming.h) - was UNCONDITIONALLY Status::Absent through Phase 6/7/8;
// as of B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md, see this header's own "GPU
// TIMING NOTE" below) it is now real, driver-measured data for every
// surviving pass whenever this class's own GpuTimestampCapability reports
// support and capture is enabled. `drawStats` is real, fused-per-draw-call
// data (see PassContext::recordDraw above) and always has been.

// See this header's own top comment for Execute()'s two-calls-per-frame
// contract, and RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md for the
// full design this class implements.
//
// GPU TIMING NOTE - UPDATED by B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md),
// which closes the gap this note used to describe. Phase 6/7 deliberately
// left every PassGpuStats::timing as Status::Absent forever (see the
// history preserved in RENDERGRAPH_PHASE6_COMPLETION_REPORT.md) - actually
// wiring up real timestamp queries was named the single highest-priority
// follow-up by three completion reports in a row (Phase 6, 7, 8) and is
// what B.1 implements: this class now owns a RenderGraphTimestampPool
// (RenderGraphTimestampPool.h) - a brand-new, dedicated, name-keyed
// VkQueryPool pair (one per ExecuteTimingMode regime, mirroring
// m_synchronousTimingSlots/m_pipelinedTimingSlots below exactly) - rather
// than reusing/extending GpuTimingService's already-shipped fixed 3-slot
// design, which a full-repository grep confirmed has zero remaining real
// (non-nullopt) production callers as of Phase 7's migration (see
// B1_REAL_GPU_TIMING_STRATEGY_v1.md, Step 3.1/3.3, and its own completion
// report for the exact grep evidence). Every surviving pass's real,
// driver-measured GPU time is now written into m_lastKnownStats via
// UpdateTimingFor() below - a synchronous-regime pass's timing is finalized
// by FinalizeSynchronousGpuTiming() (called once by Application::Run(),
// right after Renderer::EndOffscreenRenderGraphRecording() returns); a
// pipelined-regime pass's timing is read back at the very top of its own
// NEXT ExecuteCompiledGraph() call, kGpuTimingFramesInFlight frames later,
// exactly at the point FramePresenter::PresentViaRenderGraph()'s own
// pre-existing per-frame-in-flight fence wait already proves it's safe -
// never a new GPU wait added anywhere purely to fetch a timing result
// sooner. Gated by the exact same two-layer on/off convention as
// GpuTimingService (GTE_ENABLE_PROFILER at compile time,
// SetGpuTimingCaptureEnabled() at runtime - see AGENTS.md, "Profiling").
class RenderGraph {
public:
    explicit RenderGraph(Renderer& renderer);

    // `build` is invoked exactly once, synchronously, right here: it
    // receives a fresh RenderGraphBuilder& to declare this call's passes/
    // resources into (mirrors Phase 2's own AddPass()/CreateTexture()/
    // ImportTexture() exactly) and returns the handle(s) that are this
    // call's real, externally-observable outputs (Phase 3's `finalOutputs`
    // root set - e.g. the swapchain image TextureHandle for a Present-only
    // call, or {gameViewHandle, sceneViewHandle} for the offscreen call).
    //
    // `cmd` must already be a command buffer appropriate for `timingMode`'s
    // regime, already in the recording state (vkBeginCommandBuffer already
    // called) - RenderGraph never allocates/begins/ends/submits it itself,
    // exactly like FrameRecorder::RecordFrame() today.
    //
    // May throw std::runtime_error if RenderGraphCompiler::Compile() detects
    // a dependency cycle (structurally unreachable through any graph
    // declared via RenderGraphBuilder today - see
    // RENDERGRAPH_PHASE3_COMPLETION_REPORT.md - but kept as genuinely
    // correct defensive code). This function deliberately does NOT catch
    // that exception itself - per
    // RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md's own Step 3.5,
    // catching/logging/re-throwing-in-debug is the CALLER's job (Phase 7's
    // Application::Run() call sites), so a graph-declaration bug is never
    // silently swallowed into "quietly skip this frame's rendering".
    template <typename BuildFn>
    void Execute(VkCommandBuffer cmd, ExecuteTimingMode timingMode, BuildFn&& build)
    {
        RenderGraphBuilder builder;
        const std::vector<TextureHandle> finalOutputs = build(builder);
        ExecuteCompiledGraph(cmd, timingMode, builder.Finish(), finalOutputs);
    }

    // Keyed by the SAME string literal `name` passed to
    // RenderGraphBuilder::AddPass() (compared by pointer first, then
    // strcmp() as a fallback - mirrors RenderGraphNameSlotTable's own rule).
    // Returns a default-constructed PassGpuStats (DrawStats{}, an Absent
    // GpuTimingSample) for a pass name this RenderGraph has never executed
    // - never garbage, never a stale value from an unrelated name.
    PassGpuStats LastKnownStatsFor(const char* passName) const;

    // Phase 8 (RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md) - the
    // full displayable snapshot (RenderGraphSnapshot.h) of the most recent
    // Execute() call for the given regime: which passes ran (in real
    // execution order) vs. were culled (and why they were never reachable),
    // each surviving pass's declared reads/writes and current
    // LastKnownStatsFor()-sourced stats, and every declared resource's
    // computed lifetime. Updated at the end of every ExecuteCompiledGraph()
    // call for ITS OWN `timingMode` only - the OTHER regime's snapshot is
    // left untouched (mirrors how the two regimes' RenderGraphNameSlotTable
    // instances are already kept fully independent - see this class's own
    // "GPU TIMING NOTE"). Returns a default-constructed (empty)
    // RenderGraphSnapshot before this RenderGraph has ever executed that
    // regime at all - never garbage.
    const RenderGraphSnapshot& LastSnapshot(ExecuteTimingMode mode) const noexcept;

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - must be called EXACTLY
    // once, by Application::Run(), immediately after
    // Renderer::EndOffscreenRenderGraphRecording() returns (i.e. after that
    // call's own fence wait has already completed) - reads back every
    // timestamp pair written during the immediately-preceding
    // SynchronousImmediateReadback Execute() call, converts each to
    // milliseconds, and merges the result into m_lastKnownStats via
    // UpdateTimingFor() - never touching that same entry's drawStats,
    // which was already written synchronously during Execute() itself (see
    // this class's own "GPU TIMING NOTE"). A safe no-op on a frame where
    // the offscreen regime didn't run at all this frame (both Game/Scene
    // panels hidden) - the caller simply never calls it in that case,
    // mirroring how Application::Run() already only calls
    // EndOffscreenRenderGraphRecording() inside that same guard.
    void FinalizeSynchronousGpuTiming();

    // B.1 - the runtime layer of this class's own GPU-timing on/off gate,
    // driven once per frame by Application::Run() alongside its existing
    // Renderer::SetGpuTimingCaptureEnabled() call - takes a plain bool
    // (never a Profiling::-namespaced type), matching every other
    // Renderer<->Profiling bridge's own convention (see AGENTS.md,
    // "Profiling").
    void SetGpuTimingCaptureEnabled(bool enabled) noexcept { m_timestampPool.SetCaptureEnabled(enabled); }

private:
    struct PhysicalTexture {
        bool resolved = false;
        bool isImported = false;
        bool hasDepth = false;
        RenderTarget target;
        VkSampler sampler = VK_NULL_HANDLE;
        ResourceState colorState;
        ResourceState depthState;
    };

    struct PhysicalBuffer {
        bool resolved = false;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        ResourceState state;
    };

    struct NamedStats {
        const char* name = nullptr;
        PassGpuStats stats;
    };

    void ExecuteCompiledGraph(VkCommandBuffer cmd, ExecuteTimingMode timingMode, CompiledGraphInput input,
        const std::vector<TextureHandle>& finalOutputs);

    void EnsureTextureResolved(
        std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures);
    void EnsureBufferResolved(
        std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalBuffer>& physicalBuffers);

    void ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage, const CompiledGraphInput& input,
        std::vector<PhysicalTexture>& physicalTextures, std::vector<PhysicalBuffer>& physicalBuffers);

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - replaces the old, single
    // combined RecordStatsFor(): drawStats and timing are now written by
    // TWO INDEPENDENT call sites (the per-pass loop below, and either
    // FinalizeSynchronousGpuTiming() or this class's own pipelined-regime
    // readback preamble) - mirroring Profiling::GpuPassSample's own
    // "timingStatus/countStatus split, never a single combined status"
    // rule exactly (see AGENTS.md, "Profiling"). UpdateDrawStatsFor() only
    // ever touches an entry's drawStats; UpdateTimingFor() only ever
    // touches its timing - neither may clobber the other's already-correct
    // data with a stale default.
    void UpdateDrawStatsFor(const char* name, const DrawStats& drawStats);
    void UpdateTimingFor(const char* name, const GpuTimingSample& timing);

    // Converts one RenderGraphTimestampPool::RawTicks pair into a real
    // GpuTimingSample, using this class's own m_timestampPool for both its
    // capability/capture-enabled state (via ResolveGpuTimingStatus()) and
    // its GpuTimestampCapability (via ConvertTimestampDeltaToMilliseconds())
    // - only ever called once the caller already knows `hasWrittenData` is
    // true for the raw ticks being converted (i.e. a real reset+write pair
    // was actually issued for this slot beforehand).
    GpuTimingSample ResolveAndConvertTiming(const RenderGraphTimestampPool::RawTicks& raw) const;

    // Two generously-sized, independently-fixed slot budgets - see this
    // class's own "GPU TIMING NOTE" above. Never resized at runtime; a
    // frame that declares more distinct pass names (within one regime) than
    // its own budget degrades gracefully (RenderGraphNameSlotTable::
    // AssignOrGetSlot() returns kNoNameSlot for the overflowing name)
    // forever, matching RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md's
    // own accepted MVP limit.
    static constexpr std::uint32_t kSynchronousTimingSlotBudget = 16;
    static constexpr std::uint32_t kPipelinedTimingSlotBudget = 8;

    RenderGraphResourcePool m_resourcePool;

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - constructed from
    // Renderer::GetVulkanContextInfo()'s own device/graphicsQueue/
    // graphicsQueueFamily/timestampCapability fields (see RenderGraph.cpp) -
    // declared right after m_resourcePool so it's already available by the
    // time ExecuteCompiledGraph() below needs it.
    RenderGraphTimestampPool m_timestampPool;
    RenderGraphNameSlotTable m_synchronousTimingSlots{ kSynchronousTimingSlotBudget };
    RenderGraphNameSlotTable m_pipelinedTimingSlots{ kPipelinedTimingSlotBudget };

    // B.1 - pipelined-regime bookkeeping: incremented once per real
    // PipelinedDeferredReadback Execute() call (never on a frame where
    // FramePresenter::PresentViaRenderGraph() skipped calling Execute() at
    // all - minimized window, pending resize, just-recreated swapchain),
    // so this always advances in lockstep with FramePresenter's own
    // m_currentFrame cadence (both only ever advance on a genuine "this
    // frame actually presented" event) - see B1_REAL_GPU_TIMING_STRATEGY_v1.md,
    // Step 3.7.
    std::uint32_t m_pipelinedFrameCounter = 0;

    // Per-(slot, frame-in-flight-buffer) "has this exact slice ever been
    // written" flags - correctly handles the first kGpuTimingFramesInFlight
    // pipelined frames of a session (or any capture-disabled/hidden-pass
    // gap) without a fragile frame-count heuristic, generalizing Phase 4D's
    // own single warm-up flag (GpuTimingService) to
    // kPipelinedTimingSlotBudget * kGpuTimingFramesInFlight independent
    // ones. A plain fixed 2D array - no heap allocation, matching this
    // engine's "nothing in the per-frame hot path may allocate" convention
    // (see AGENTS.md, "Profiling").
    bool m_pipelinedHasWritten[kPipelinedTimingSlotBudget][kGpuTimingFramesInFlight]{};

    // Per-pass-name last-known stats - a plain vector (never a hash map),
    // matching this engine's "no hashing on the hot path" convention (see
    // AGENTS.md) given this engine declares single-digit pass counts today.
    std::vector<NamedStats> m_lastKnownStats;

    // Phase 8 - one persistent RenderGraphSnapshot per ExecuteTimingMode
    // regime, overwritten in full at the end of every ExecuteCompiledGraph()
    // call for that regime - see LastSnapshot() above.
    RenderGraphSnapshot m_synchronousSnapshot;
    RenderGraphSnapshot m_pipelinedSnapshot;
};

} // namespace gte::rg
