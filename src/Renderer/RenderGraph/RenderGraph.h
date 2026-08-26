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
// and this class's own LastKnownStatsFor()/RecordStatsFor() below share
// exactly one definition rather than two. `timing` is a genuine
// gte::GpuTimingSample (Renderer-local tri-state, see GpuTiming.h) -
// DELIBERATELY always Status::Absent as of Phase 6/7 (see this header's own
// "GPU timing" note below); `drawStats` is real, fused-per-draw-call data
// (see PassContext::recordDraw above).

// See this header's own top comment for Execute()'s two-calls-per-frame
// contract, and RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md for the
// full design this class implements.
//
// GPU TIMING NOTE (a deliberate, documented scope decision for THIS
// implementation - see RENDERGRAPH_PHASE6_COMPLETION_REPORT.md for the full
// reasoning): the strategy document's own Step 3.2 asks for
// GpuTimingService's fixed 3-slot VkQueryPool to be REPLACED by a
// generalized, name-keyed pool. Actually replacing GpuTimingService's
// production-shipping pool/API in THIS phase - before Phase 7 has migrated
// a single real call site onto RenderGraph - would be a materially risky,
// unforced change to already-shipped, Tier-2 (manually-verified) code with
// no real consumer yet, directly against this campaign's own "nothing
// outside src/Renderer/RenderGraph/ calls into this yet" discipline. This
// class instead owns two RenderGraphNameSlotTable instances (one per
// ExecuteTimingMode regime - see m_synchronousTimingSlots/
// m_pipelinedTimingSlots below), which already implement and prove out the
// PURE name -> slot assignment/reuse/overflow-degradation logic Step 3.2
// asks for (see RenderGraphNameSlotTable.h and its own Tier-1 tests) and
// are exercised on every single Execute() call - but no VkQueryPool/
// vkCmdWriteTimestamp2 call is issued anywhere in this class yet, so every
// PassGpuStats::timing this phase ever produces is Status::Absent, never a
// fabricated non-zero value (matching this engine's own "never default a
// GPU measurement that doesn't have a real value this frame to a bare
// numeric 0" rule - see AGENTS.md, "Profiling"). Wiring these slot tables
// to a real, generalized VkQueryPool (replacing GpuTimingService's fixed
// enum, exactly as Step 3.2 describes) is Phase 7's job, at the point a
// real, in-production set of pass names actually exists to time.
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

    void RecordStatsFor(const char* name, const PassGpuStats& stats);

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
    RenderGraphNameSlotTable m_synchronousTimingSlots{ kSynchronousTimingSlotBudget };
    RenderGraphNameSlotTable m_pipelinedTimingSlots{ kPipelinedTimingSlotBudget };

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
