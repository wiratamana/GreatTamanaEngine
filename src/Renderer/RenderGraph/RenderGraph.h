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

#include "CommandBuffer.h" // task_manager/better-render-pass-1 campaign, PHASE3 - PassContext::Cmd()
#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphDebugMetadataSink.h" // editor-core-separation-25 campaign, PHASE4
#include "RenderGraphDebugTextureRegistry.h"
#include "RenderGraphDebugVolumeTextureRegistry.h"
#include "RenderGraphNameSlotTable.h"
#include "RenderGraphPersistentResourceCache.h" // editor-core-separation-27 campaign, PHASE7
#include "RenderGraphResourcePool.h"
#include "RenderGraphTimestampPool.h"
#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../DrawStats.h"
#include "../GpuTiming.h"

#include <string>

#include <volk.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// `PassContext` is forward-declared by RenderGraphTypes.h (`struct
// PassContext;`) and used, still only as a reference, by
// `PassRecord::execute` (`std::function<void(PassContext&)>`) - neither of
// those needs a COMPLETE type. This header fully specifies `PassContext`
// only at the very BOTTOM of this file, AFTER `class RenderGraph` below -
// render-pass-6 campaign, PHASE3 (item 2.7) gave `PassContext` plain,
// non-owning pointers into RenderGraph's own PRIVATE `PhysicalTexture`/
// `PhysicalBuffer`/`PhysicalVolumeTexture` nested types (replacing the six
// freshly-constructed `std::function` closures Phase 6 originally shipped),
// and naming a class's private nested type from the outside requires that
// class to have already granted friendship AND to have already been fully
// parsed (a nested type is only nameable via qualification once the
// compiler has actually seen it declared) - see `class RenderGraph`'s own
// `friend struct PassContext;` grant below, and `PassContext`'s own full
// definition/doc comment at the bottom of this file for the complete
// reasoning (confirmed via `ask_questions` during PHASE3's own
// implementation, see `PHASE3_COMPLETION_REPORT.md`).

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
// data (see PassContext::recordDraw below) and always has been.

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
        // editor-core-separation-25 campaign - forwarded into THIS call's
        // own fresh builder, before build(builder) ever runs, so every
        // pass this call declares (via AddRenderPass()) reaches the
        // installed sink, if any. BeginFrame() marks the start of a fresh
        // declaration sequence - see IPassDebugMetadataSink::BeginFrame()'s
        // own doc comment (RenderGraphDebugMetadataSink.h) for why this
        // must happen exactly here, exactly once, before any pass this
        // call declares.
        builder.SetDebugMetadataSink(m_debugMetadataSink);
        // editor-core-separation-27 campaign, PHASE7 - see
        // RenderGraphBuilder::SetPersistentResourceCache()'s own doc comment
        // for why all three values are bundled into this one call.
        builder.SetPersistentResourceCache(&m_persistentResourceCache, timingMode, m_persistentResourceFrameCounter);
        if (m_debugMetadataSink != nullptr) {
            m_debugMetadataSink->BeginFrame();
        }
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

    // network-impl-4 campaign, Phase 2
    // (task_manager/network-impl-4/PHASE2_RENDERGRAPH_INTEGRATION_AND_AUTO_REGISTRATION.md) -
    // the query surface behind GET /get_texture and GET /list_textures (see
    // src/Application/FrameCaptureBridge.h and src/Network/NetworkRoutes.h,
    // Phases 4/5). `name` is compared as a plain std::string - see
    // RenderGraphDebugTextureRegistry::FindByName()'s own doc comment for why
    // this is safe against an arbitrary, HTTP-supplied string. Returns
    // std::nullopt if this RenderGraph has never resolved a texture under this
    // exact name this session.
    //
    // NOTE (bare names, no "rg::" prefix): this class is already declared
    // inside namespace gte::rg, so DebugTextureSnapshot/ResourceState below
    // refer to gte::rg::DebugTextureSnapshot/gte::rg::ResourceState directly -
    // matching this header's own pre-existing style (see LastSnapshot() above).
    std::optional<DebugTextureSnapshot> DebugTextureSnapshotFor(const std::string& name) const;

    // Every texture name/snapshot ever registered this session - the primitive
    // behind GET /list_textures.
    std::vector<DebugTextureSnapshot> ListDebugTextures() const;

    // network-impl-6 campaign, Phase 2
    // (task_manager/network-impl-6/PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md) -
    // the volume-texture counterpart of DebugTextureSnapshotFor()/
    // ListDebugTextures() above, backed by m_debugVolumeTextures instead of
    // m_debugTextures. `name` is compared as a plain std::string - see
    // RenderGraphDebugVolumeTextureRegistry::FindByName()'s own doc comment.
    // Returns std::nullopt if this RenderGraph has never resolved a volume
    // texture under this exact name this session.
    std::optional<DebugVolumeTextureSnapshot> DebugVolumeTextureSnapshotFor(const std::string& name) const;

    // Every volume texture name/snapshot ever registered this session - the
    // volume-texture counterpart of ListDebugTextures() above.
    std::vector<DebugVolumeTextureSnapshot> ListDebugVolumeTextures() const;

    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 7 - called ONLY from
    // the small number of call sites that perform a graph-EXTERNAL manual
    // image-layout transition on an already-registered named texture right
    // after this RenderGraph's own ExecuteCompiledGraph() call returns (today:
    // Application::Run()'s two FinalizeRenderTextureForExternalSampling() call
    // sites for "GameView"/"SceneView", FramePresenter.cpp's own "Swapchain"
    // finalize, and ComputeBlurValidation::FinalizeForSampling()'s
    // "BlurredSceneOutput" finalize - see Phase 3, which corrected an earlier
    // revision's "only two call sites" undercount). A safe no-op if `name`
    // is not yet a known entry (see RenderGraphDebugTextureRegistry::
    // ApplyColorStateOverride()'s own doc comment).
    void NotifyDebugTextureStateOverride(const std::string& name, const ResourceState& newColorState);

    // The registry's own current monotonic "engine frame" counter value (see
    // RenderGraph.h's own m_debugTextureFrameCounter member doc comment) - a
    // caller (Phase 4/5) computes a snapshot's own "frames_since_update" as
    // `CurrentDebugTextureFrameCounter() - snapshot.lastUpdatedFrameCounter`
    // at the exact moment it services a request, never cached/stale.
    //
    // network-impl-6 campaign, Phase 2 - this SAME counter also stamps every
    // DebugVolumeTextureSnapshot's own lastUpdatedFrameCounter (there is
    // deliberately no separate "current debug VOLUME texture frame counter"
    // method - see PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md's own Step
    // 3.1) - a caller computing a volume snapshot's own "frames_since_update"
    // must call this exact same method too.
    //
    // IMPORTANT (see PHASE2's own Step 3.3a): this counter only ever
    // advances during a SynchronousImmediateReadback call. A texture that is
    // ONLY ever registered by the PipelinedDeferredReadback regime (today:
    // "Swapchain") therefore has its own "frames_since_update" freshness signal
    // driven entirely by how often the OFFSCREEN (Game/Scene) regime executes
    // elsewhere - NOT by how often that texture itself actually updates. This
    // is intentional (see PHASE0_MASTER_STRATEGY.md's own Locked Design
    // Decision 4 caveat), not a bug to fix by adding a second counter.
    std::uint64_t CurrentDebugTextureFrameCounter() const noexcept;

    // editor-core-separation-27 campaign, PHASE7 - called EXACTLY once per
    // real engine frame, by Core::BuildFrame(), as the very first thing it
    // does, strictly before either ExecuteTimingMode regime's Execute() call
    // runs that frame - see BIG_STEP_3 Section 8.
    void BeginPersistentResourceFrame() noexcept;

    // The counterpart of CurrentDebugTextureFrameCounter(), for THIS cache's
    // own dedicated, regime-agnostic counter - a caller must use THIS value,
    // never CurrentDebugTextureFrameCounter(), when computing
    // RenderGraphPersistentResourceCache::FramesUntilEviction()'s own
    // `currentFrame` argument.
    std::uint64_t CurrentPersistentResourceFrameCounter() const noexcept { return m_persistentResourceFrameCounter; }

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

    // editor-core-separation-25 campaign - installed EXACTLY ONCE per
    // session, by Editor-tier startup code (EditorHost's own constructor,
    // PHASE4) - mirrors GpuMemoryTracker::SetDebugNameObserver()'s own
    // "install once, on the one persistent owning object" placement
    // (docs/conventions/gpu-resource-memory-tracking.md), extended with a
    // SECOND, separate read-only pointer (see RenderGraphDebugMetadataSink.h's
    // own header comment for why one interface could not serve both
    // directions). A Player build that never calls either setter pays one
    // null-pointer branch per pass declaration (via the builder this
    // sink is forwarded into, every Execute() call) plus one more per
    // Execute() call (the BeginFrame() call below) plus one more per
    // ExecuteCompiledGraph() call (the metadataLookup construction below)
    // - and stores nothing.
    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }
    void SetDebugMetadataProvider(IPassDebugMetadataProvider* provider) noexcept { m_debugMetadataProvider = provider; }

private:
    // render-pass-6 campaign, PHASE3 (item 2.7) - `PassContext` (fully
    // defined at the bottom of this file, AFTER this class) needs read
    // access to `PhysicalTexture`/`PhysicalBuffer`/`PhysicalVolumeTexture`
    // immediately below - normally PRIVATE, implementation-only nested
    // types - so it can hold plain, non-owning `const std::vector<...>*`
    // pointers into them and implement its own resolveReadTexture()/
    // resolveBuffer()/resolveVolumeTexture() member functions. Confirmed via
    // `ask_questions` (see PHASE3_COMPLETION_REPORT.md, "private nested-type
    // visibility"): a narrow `friend` grant to `PassContext` alone, rather
    // than moving these three structs out to full `gte::rg` namespace scope
    // (which would have made them nameable/constructible from anywhere in
    // the engine for zero additional benefit - nothing outside RenderGraph's
    // own executor needs them).
    friend struct PassContext;

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

    // Atmosphere Scattering campaign, Phase 2
    // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md) - the
    // volume-texture sibling of PhysicalTexture/PhysicalBuffer above. Every
    // VolumeTextureHandle today is imported (see RenderGraphBuilder::
    // ImportVolumeTexture()) - there is deliberately no pooled/transient
    // counterpart yet, so `isImported` is always true in practice (see
    // this campaign's own Phase 2 completion report). No `hasDepth`/depth
    // state at all - a volume texture has no depth-companion concept the
    // way a 2D PhysicalTexture does.
    struct PhysicalVolumeTexture {
        bool resolved = false;
        bool isImported = false;
        VolumeTarget target;
        ResourceState state;
    };

    // better-render-pass-3 campaign, BLOCK5, Phase 3 - the TextureArray
    // sibling of PhysicalTexture/PhysicalBuffer/PhysicalVolumeTexture above.
    // Needs BOTH `isImported` (unlike PhysicalVolumeTexture, which is always
    // imported in practice) AND a real pooled-resolve path (see
    // EnsureTextureArrayResolved() below) - TextureArray IS pooled, unlike
    // VolumeTexture. A SINGLE `state` field (not a colorState/depthState
    // split like PhysicalTexture) - a TextureArray has ONE homogeneous
    // aspect (all-depth or all-color) across every layer, never a mixed
    // color+depth companion pair the way a 2D PhysicalTexture can be.
    struct PhysicalTextureArray {
        bool resolved = false;
        bool isImported = false;
        TextureArrayTarget target;
        ResourceState state;
    };

    // editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 2), PHASE5 - found-and-fixed the SAME dangling-pointer hazard
    // RenderGraphNameSlotTable.h's own file-header comment documents in
    // full: `name` used to be a raw, non-owning `const char*` (correct only
    // as long as every debugName is a process-lifetime string literal - an
    // assumption a Project Assembly's own FreeLibrary()'d debugName breaks).
    // This vector persists PAST any one pass's own single-frame declaration
    // lifetime (never pruned when a pass disappears), so it is - exactly
    // like RenderGraphNameSlotTable - a place a name must be OWNED, not
    // borrowed. Confirmed by the same live crash that hazard produced:
    // std::strcmp() against a stale `entry.name` here crashes on literally
    // the very first frame after ProjectAssemblyHost::UnloadProjectAssembly()
    // removes a pass whose name this vector had already cached.
    struct NamedStats {
        std::string name;
        PassGpuStats stats;
    };

    void ExecuteCompiledGraph(VkCommandBuffer cmd, ExecuteTimingMode timingMode, CompiledGraphInput input,
        const std::vector<TextureHandle>& finalOutputs);

    void EnsureTextureResolved(
        std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures);
    void EnsureBufferResolved(
        std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalBuffer>& physicalBuffers);
    // Atmosphere Scattering campaign, Phase 2.
    void EnsureVolumeTextureResolved(std::uint32_t index, const CompiledGraphInput& input,
        std::vector<PhysicalVolumeTexture>& physicalVolumeTextures);
    // better-render-pass-3 campaign, BLOCK5, Phase 3 - mirrors
    // EnsureVolumeTextureResolved() above for the IMPORT branch, AND
    // EnsureTextureResolved()'s own RenderGraphResourcePool::AcquireTexture()
    // call for the POOLED branch - TextureArray needs BOTH branches, unlike
    // VolumeTexture (import-only).
    void EnsureTextureArrayResolved(std::uint32_t index, const CompiledGraphInput& input,
        std::vector<PhysicalTextureArray>& physicalTextureArrays);

    void ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage, const CompiledGraphInput& input,
        std::vector<PhysicalTexture>& physicalTextures, std::vector<PhysicalBuffer>& physicalBuffers,
        std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
        std::vector<PhysicalTextureArray>& physicalTextureArrays);

    // render-pass-6 campaign, PHASE2 (item 2.6) - extracted, zero-behavior-
    // change decomposition of ExecuteCompiledGraph()'s own six interleaved
    // concerns - see PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md. Builds the
    // PassContext a pass's own `execute` callback uses - as of PHASE3 (item
    // 2.7) this is now a small, obviously-correct handful of pointer
    // assignments (see PassContext's own definition below) instead of six
    // freshly-constructed std::function closures.
    PassContext BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
        std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
        std::vector<PhysicalTextureArray>& physicalTextureArrays, DrawStats& passDrawStats);

    // One VkRenderingAttachmentInfo per pass.colorAttachments entry, in that
    // exact order (== shader layout(location = N) out) - identical logic to
    // what used to be written inline inside ExecuteCompiledGraph()'s
    // `if (hasColorWrite) { ... }` block's own color-attachment loop, PLUS
    // (appended at the end, after every pass.colorAttachments entry) a
    // single COLOR-kind array-layer attachment if pass.arrayLayerAttachment
    // is set and its underlying array is not a depth one (a depth-kind
    // array-layer attachment is handled entirely by BuildDepthAttachmentInfo()
    // below instead). Also fills `outResolvedExtents` with each attachment's
    // resolved VkExtent2D (in the same order), for
    // FindMismatchedColorAttachmentExtent()'s own existing pure decision
    // function to consume - the caller (ExecuteCompiledGraph()) is still the
    // one that calls FindMismatchedColorAttachmentExtent() and throws on
    // mismatch, unchanged.
    std::vector<VkRenderingAttachmentInfo> BuildColorAttachmentInfos(
        const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures,
        const std::vector<PhysicalTextureArray>& physicalTextureArrays,
        std::vector<VkExtent2D>& outResolvedExtents) const;

    // The depth attachment for this pass - either a plain Texture-kind depth
    // write, or an array-layer attachment whose underlying resource is a
    // DEPTH array (TextureArrayTarget::hasDepth == true). `extent` is only
    // meaningful when `info` is set, and is this attachment's own resolved
    // size - needed by the caller as a fallback render-area/viewport size
    // for a pass with ZERO color attachments (depth-only).
    struct DepthAttachmentResult {
        std::optional<VkRenderingAttachmentInfo> info;
        VkExtent2D extent{};
    };

    // Mirrors BuildColorAttachmentInfos() above, just for the single depth
    // attachment a pass may have. `depthHandle` alone is sufficient to
    // signal "no plain-Texture depth write this call": a default-constructed
    // TextureHandle (what the caller's own `TextureHandle depthHandle;`
    // local already is, unless the writes scan below finds a real
    // DepthStencilAttachmentReadWrite usage) has `index == kInvalidIndex`,
    // so `depthHandle.IsValid()` is exactly the `hasDepthWrite` signal this
    // method needs - no separate bool parameter required. Falls back to
    // pass.arrayLayerAttachment when `!depthHandle.IsValid()` and that
    // attachment's underlying array is a depth one; returns an empty
    // DepthAttachmentResult (info == std::nullopt) otherwise.
    DepthAttachmentResult BuildDepthAttachmentInfo(const PassRecord& pass,
        const std::vector<PhysicalTexture>& physicalTextures,
        const std::vector<PhysicalTextureArray>& physicalTextureArrays, TextureHandle depthHandle) const;

    // The two passive-registration loops that used to run inline at the bottom
    // of ExecuteCompiledGraph(), extracted verbatim (same fields, same skip
    // conditions, same Upsert() calls) - see network-impl-4/network-impl-6's
    // own original comments, preserved at the new call sites.
    void RegisterDebugTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
        const std::vector<PhysicalTexture>& physicalTextures);
    void RegisterDebugVolumeTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
        const std::vector<PhysicalVolumeTexture>& physicalVolumeTextures);

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

    // editor-core-separation-27 campaign, PHASE7 - mirrors m_resourcePool's
    // exact ownership shape (BIG STEP 3 of 4).
    RenderGraphPersistentResourceCache m_persistentResourceCache;

    // editor-core-separation-26 campaign, PHASE6 - non-owning, mirrors
    // RenderGraphResourcePool::m_renderer's own identical "pointer, not
    // reference, so the owning class stays assignable" shape and reasoning.
    // Renderer outlives this RenderGraph for its entire lifetime
    // (RenderGraph is a plain member of Core, constructed with Core's own
    // Renderer - see Core.h). Needed starting this phase so
    // ExecuteCompiledGraph()'s new PassKind::Blit branch can call
    // SupportsDepthBlit().
    Renderer* m_renderer = nullptr;

    // editor-core-separation-25 campaign - see SetDebugMetadataSink()/
    // SetDebugMetadataProvider() above. Both nullptr forever in a
    // Player-style build that links `gte_core` alone (never `gte_editor` -
    // this codebase has no `GTE_ENABLE_EDITOR` preprocessor macro anymore,
    // see AGENTS.md's "`gte_core` / `gte_editor` Library Separation"
    // section) - neither setter is ever called from Core, only from
    // Editor-tier startup code.
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
    IPassDebugMetadataProvider* m_debugMetadataProvider = nullptr;

    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - constructed from
    // Renderer::GetVulkanContextInfo()'s own device/graphicsQueue/
    // graphicsQueueFamily/timestampCapability fields (see RenderGraph.cpp) -
    // declared right after m_resourcePool so it's already available by the
    // time ExecuteCompiledGraph() below needs it.
    RenderGraphTimestampPool m_timestampPool;
    RenderGraphNameSlotTable m_synchronousTimingSlots{ kSynchronousTimingSlotBudget };
    RenderGraphNameSlotTable m_pipelinedTimingSlots{ kPipelinedTimingSlotBudget };

    // PHASE1 (render-pass-6 campaign, item 2.4) - names whose timing-slot
    // overflow has already been reported this process lifetime, per regime -
    // so a name that keeps overflowing every single frame is only ever logged
    // ONCE, not once per frame forever. A plain vector (never a hash set),
    // matching this engine's "no hashing on the hot path" convention (see
    // AGENTS.md) - overflow is expected to be a rare, one-time-per-name event,
    // never a steady-state hot path.
    // editor-core-separation-13 campaign, PHASE5 - OWNED std::string, not a
    // raw `const char*` - see RenderGraphNameSlotTable.h's own file-header
    // comment and NamedStats' own doc comment above for the full "why" (the
    // same class of dangling-pointer hazard: this vector persists forever,
    // past any one pass's own single-frame declaration lifetime).
    std::vector<std::string> m_reportedSynchronousOverflows;
    std::vector<std::string> m_reportedPipelinedOverflows;

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

    // network-impl-4 campaign, Phase 1/2 - the passive, name-keyed debug
    // texture registry (RenderGraphDebugTextureRegistry.h) plus the one
    // shared, monotonically increasing "engine frame" counter it is stamped
    // with - see this class's own DebugTextureSnapshotFor()/
    // ListDebugTextures()/NotifyDebugTextureStateOverride()/
    // CurrentDebugTextureFrameCounter() methods above, and
    // ExecuteCompiledGraph()'s own registration loop (RenderGraph.cpp) for
    // how this is kept passively up to date every call, in BOTH
    // ExecuteTimingMode regimes.
    RenderGraphDebugTextureRegistry m_debugTextures;

    // network-impl-6 campaign, Phase 2
    // (task_manager/network-impl-6/PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md) -
    // the volume-texture counterpart of m_debugTextures above, kept up to
    // date by ExecuteCompiledGraph()'s own second registration loop
    // (RenderGraph.cpp), stamped with this SAME m_debugTextureFrameCounter -
    // see CurrentDebugTextureFrameCounter()'s own doc comment above for why
    // there is deliberately no separate volume-only counter.
    RenderGraphDebugVolumeTextureRegistry m_debugVolumeTextures;

    // Increments once per REAL engine frame - i.e. once per
    // SynchronousImmediateReadback call (see ExecuteCompiledGraph()'s own
    // `if (!isPipelined)` block, alongside m_resourcePool.BeginFrame()) -
    // NEVER once per ExecuteCompiledGraph() call in general, since that
    // would double-count relative to a real engine frame given both
    // regimes share this same counter. See CurrentDebugTextureFrameCounter()'s
    // own doc comment above for the accepted "Swapchain" freshness caveat
    // this sharing implies.
    std::uint64_t m_debugTextureFrameCounter = 0;

    // editor-core-separation-27 campaign, PHASE7 - a NEW, dedicated,
    // regime-agnostic counter, deliberately SEPARATE from
    // m_debugTextureFrameCounter (see this class's own
    // CurrentDebugTextureFrameCounter() doc comment for why that one's
    // regime-gated advancement would be wrong to reuse here - see
    // BIG_STEP_3 Section 5.3/Section 8). Starts at 0;
    // BeginPersistentResourceFrame() pre-increments, so its first-ever real
    // value is 1 - matching PersistentResourceCacheEntry::lastRequestedFrame's
    // own "0 is never a real frame" sentinel convention.
    std::uint64_t m_persistentResourceFrameCounter = 0;
};

// Fully specifies the `struct PassContext;` forward-declared by Phase 1
// (RenderGraphTypes.h) - PassRecord::execute is a
// `std::function<void(PassContext&)>`, invoked exactly once per surviving
// pass by RenderGraph::Execute() below (see PassRecord's own doc comment).
//
// Deliberately kept as small as this phase actually needs - see
// RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md's own "Step 5: Their
// Role": "it is far easier to ADD a method to PassContext later than to
// have over-designed it now against imagined future passes".
//
// render-pass-6 campaign, PHASE3 (item 2.7) - REPLACES the six freshly-
// constructed `std::function` closures (one heap-capture-shaped object +
// one indirect vtable-style call per resolve/record, PER PASS, PER
// Execute() call) Phase 6 originally shipped with plain, non-owning data:
// `resolveReadTexture`/`resolveTexture`/`resolveBuffer`/`resolveVolumeTexture`
// are now ordinary, non-virtual member functions indexing plain
// `const std::vector<RenderGraph::PhysicalX>*` pointers (never null for a
// PassContext actually built by RenderGraph::BuildPassContext() - only a
// default-constructed PassContext, never handed to a real pass, leaves them
// null); `recordDraw`/`recordIndirectDraw` are small, non-owning CALLABLE
// STRUCT fields (`RecordDrawFn`/`RecordIndirectDrawFn` below) rather than
// bare member functions - see their own doc comment below for exactly why
// that distinction is load-bearing, not stylistic. PassContext is never
// stored/copied beyond one pass's own `execute` call (see this struct's own
// doc comment above), so every one of these raw pointers' lifetime is
// always safely bounded by that same call - they all point into
// RenderGraph::ExecuteCompiledGraph()'s own stack-local physicalX vectors/
// passDrawStats local, which outlive the entire per-pass loop iteration
// that hands a PassContext to `pass.execute()`.
//
// Every pass author's call site is BYTE-FOR-BYTE UNCHANGED by this phase -
// `ctx.resolveReadTexture(handle)`, `ctx.recordDraw(...)`,
// `m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw)`, `if
// (ctx.recordIndirectDraw) { ctx.recordIndirectDraw(); }`, etc. all compile
// and behave identically - confirmed by a full grep audit of every real
// production call site (see PHASE3_COMPLETION_REPORT.md).
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

    // Atmosphere Scattering campaign, Phase 2
    // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md) - the
    // volume-texture sibling of ResolvedTexture above. RenderGraph never
    // owns a VolumeTexture (every VolumeTextureHandle today is imported, see
    // RenderGraphBuilder::ImportVolumeTexture()), so this returns a plain
    // ResolvedVolumeTexture{view}, never a "VolumeTexture&" - there is no
    // such owning reference for RenderGraph to hand out, it only ever
    // tracks the plain VolumeTarget an external owner supplied.
    struct ResolvedVolumeTexture {
        VkImageView view = VK_NULL_HANDLE;
    };

    // better-render-pass-3 campaign, BLOCK5, Phase 3 - the TextureArray
    // sibling of ResolvedVolumeTexture above - "view only", no sampler field
    // (see this struct's own resolveTextureArray() doc comment below for
    // why).
    struct ResolvedTextureArray {
        VkImageView view = VK_NULL_HANDLE;
    };

    // Per-layer sibling of ResolvedTextureArray above - view AND image (an
    // attachment needs both: view for VkRenderingAttachmentInfo, image for a
    // future manual barrier/debug dump).
    struct ResolvedTextureArrayLayer {
        VkImageView view = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
    };

    // render-pass-6 campaign, PHASE3 (item 2.7) - plain, non-owning pointers
    // into RenderGraph::ExecuteCompiledGraph()'s own stack-local physicalX
    // vectors, set exactly once by RenderGraph::BuildPassContext() (see
    // RenderGraph.cpp) right before a pass's `execute` callback runs. Never
    // null for a PassContext actually handed to a pass - only a
    // default-constructed PassContext (never handed to a real pass) leaves
    // these null, which is why every resolve method below defensively
    // null-checks before dereferencing (mirroring this codebase's general
    // "defensive, cheap check even when a real call site already guarantees
    // it won't happen" style seen throughout RenderGraph.cpp). Named to
    // match this struct's own sibling plain-data-struct convention elsewhere
    // in this file/RenderGraphTypes.h (ResourceUsage, ColorAttachmentDesc,
    // etc. - no `m_` prefix), rather than RenderGraph's own class-member
    // `m_`-prefixed convention, since PassContext is a plain value struct,
    // not a class with real behavior of its own to hide.
    const std::vector<RenderGraph::PhysicalTexture>* textures = nullptr;
    const std::vector<RenderGraph::PhysicalBuffer>* buffers = nullptr;
    const std::vector<RenderGraph::PhysicalVolumeTexture>* volumeTextures = nullptr;
    // better-render-pass-3 campaign, BLOCK5, Phase 3 - see textures/buffers/
    // volumeTextures above for the exact same shape/lifetime reasoning.
    const std::vector<RenderGraph::PhysicalTextureArray>* textureArrays = nullptr;

    // task_manager/better-render-pass-1 campaign, PHASE3
    // (PHASE3_ENGINE_COMMAND_BUFFER_AND_TYPE_SAFE_PUSH_CONSTANTS.md) - the
    // ONE new field this phase adds to PassContext (everything else on this
    // struct is completely untouched). Plain, non-owning, set exactly once
    // by RenderGraph::BuildPassContext() (mirrors textures/buffers/
    // volumeTextures above exactly) - RenderGraph already holds this same
    // non-owning Renderer* as its own m_renderer member (added by
    // editor-core-separation-26 campaign, PHASE6), so this is zero new
    // plumbing, just one more pointer forwarded into the PassContext this
    // method already builds. Never null for a PassContext actually handed to
    // a pass - only a default-constructed PassContext (never handed to a
    // real pass) leaves this null, which is why Cmd() below is only ever
    // meaningful when this is non-null (CommandBuffer itself defensively
    // asserts/no-ops on a null Renderer - see CommandBuffer.h).
    Renderer* renderer = nullptr;

    // Resolves a texture this pass declared as a READ (via
    // PassBuilder::ReadTexture()) into its already-live VkImageView/
    // VkSampler pair, wired up by RenderGraph::Execute() right before
    // invoking this pass's `execute` callback. Returns a null view/sampler
    // for a handle that never resolved to a physical texture this call
    // (including an imported resource, which carries no VkSampler of its
    // own - see RenderGraphBuilder::ImportTexture()'s own TextureImportInfo,
    // which has no sampler field).
    ResolvedTexture resolveReadTexture(TextureHandle handle) const noexcept;

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
    ResolvedTexture resolveTexture(TextureHandle handle) const noexcept { return resolveReadTexture(handle); }

    // The buffer sibling of resolveTexture() above - resolves a declared
    // BufferHandle (via ReadBuffer()/WriteBuffer()) into its CURRENT
    // physical VkBuffer, for the exact same "rewrite my own
    // ComputeDescriptorSet before dispatching" use case. Returns
    // VK_NULL_HANDLE for a handle that never resolved to a physical buffer
    // this call.
    VkBuffer resolveBuffer(BufferHandle handle) const noexcept;

    // Atmosphere Scattering campaign, Phase 2 - the volume-texture sibling
    // of resolveTexture()/resolveBuffer() above, same "resolve whatever was
    // already resolved" shape. A pass's `execute` callback uses this to
    // rewrite its own ComputeDescriptorSet (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    // VK_IMAGE_LAYOUT_GENERAL) against the CURRENT physical image view
    // behind a declared handle.
    ResolvedVolumeTexture resolveVolumeTexture(VolumeTextureHandle handle) const noexcept;

    // better-render-pass-3 campaign, BLOCK5, Phase 3 - the TextureArray
    // sibling of resolveVolumeTexture() above, same "resolve whatever was
    // already resolved" shape. Mirrors ResolvedVolumeTexture's own
    // minimalism - "view only", no sampler field (TextureArray2D exposes
    // its own Sampler() directly via PhysicalTextureArray if a future
    // consumer needs it; do not speculatively add a field here until a
    // real call site proves it needs more).
    ResolvedTextureArray resolveTextureArray(TextureArrayHandle handle) const noexcept;

    // Resolves a layer this pass declared via WriteArrayLayer() into its
    // already-live {view, image} pair - safe-null for anything not resolved
    // this frame (mirrors resolveReadTexture()'s own discipline). Only an
    // index bounds check beyond that is needed - the view comes straight
    // from TextureArrayTarget::LayerView().
    ResolvedTextureArrayLayer resolveArrayLayer(TextureArrayHandle handle, std::uint32_t layerIndex) const noexcept;

    // render-pass-6 campaign, PHASE3 (item 2.7) - recordDraw/
    // recordIndirectDraw deliberately stay small, non-owning CALLABLE
    // STRUCT fields rather than becoming bare ordinary member functions
    // (unlike the four resolve*() methods above) - a full grep audit (this
    // phase's own Step 3.4) found real production call sites a bare member
    // function cannot satisfy:
    //   - ~20 sites do `m_renderer.BeginGraphPassRecording(ctx.cmd,
    //     ctx.recordDraw);` - passing the field as a plain VALUE into
    //     Renderer's own `std::function<void(bool, std::uint32_t,
    //     std::uint32_t)>`-typed parameter, never calling it with parens.
    //   - Application.cpp does `if (ctx.recordIndirectDraw) {
    //     ctx.recordIndirectDraw(); }` - a truthiness check before calling.
    // A bare non-static member function name cannot be used, unqualified,
    // as a value or in a boolean context (only called with `()`, or
    // address-of'd via `&PassContext::recordDraw` and bound to an object) -
    // so converting these two to plain member functions would be a compile
    // error at every one of those call sites, directly contradicting this
    // phase's own harder "zero call-site change, whole engine compiles
    // unmodified" requirement. A small function-object struct (implicitly
    // convertible to `std::function<...>` via its own `operator()`, exactly
    // like a lambda would be) keeps every one of those call sites compiling
    // completely unmodified while still holding only a plain, non-owning
    // `DrawStats*` - no lambda capture, no PassContext-owned std::function,
    // no heap allocation of its own. Confirmed via `ask_questions` during
    // this phase's own implementation (see PHASE3_COMPLETION_REPORT.md).
    struct RecordDrawFn {
        DrawStats* drawStats = nullptr;

        // Called by a pass's `execute` callback immediately alongside
        // issuing a real vkCmdDraw/vkCmdDrawIndexed, so this pass's own
        // DrawStats tally stays fused to the exact call site that actually
        // issued the draw - mirroring Renderer::Submit()'s existing shape/
        // semantics (see AGENTS.md's "Profiling" section, AccumulateDrawStats()'s
        // own correctness rule) but scoped per-PASS here instead of
        // per-frame-queue.
        void operator()(bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) const;
    };

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
    // - the indirect-draw sibling of RecordDrawFn above. Called by a pass's
    // `execute` callback immediately alongside issuing a real
    // Renderer::SubmitIndirect() call, so DrawStats::indirectDrawCount (see
    // DrawStats.h's own doc comment - a COUNT OF INDIRECT DRAW CALLS ISSUED,
    // never an object/triangle count) is real, per-pass data. Deliberately a
    // SEPARATE type from RecordDrawFn (never a shared one with an extra bool
    // parameter) - the two update genuinely DIFFERENT DrawStats fields
    // (indirectDrawCount vs. drawCallCount/triangleCount), and must never be
    // confused with each other, per DrawStats.h's own explicit rule. Also
    // carries an explicit `operator bool()` so `if (ctx.recordIndirectDraw)`
    // (Application.cpp's existing call site) keeps compiling and behaving
    // identically - true whenever this PassContext was built by
    // RenderGraph::BuildPassContext() (i.e. `drawStats != nullptr`), exactly
    // mirroring what a default-constructed `std::function`'s own
    // `operator bool()` used to report before this phase.
    struct RecordIndirectDrawFn {
        DrawStats* drawStats = nullptr;

        void operator()() const;

        explicit operator bool() const noexcept { return drawStats != nullptr; }
    };

    RecordDrawFn recordDraw;
    RecordIndirectDrawFn recordIndirectDraw;

    // task_manager/better-render-pass-1 campaign, PHASE3
    // (PHASE3_ENGINE_COMMAND_BUFFER_AND_TYPE_SAFE_PUSH_CONSTANTS.md) - builds
    // a fresh gte::rg::CommandBuffer (CommandBuffer.h) from this
    // PassContext's own cmd/renderer/recordDraw.drawStats fields, every time
    // it's called - never cached/stored by PassContext itself. The returned
    // CommandBuffer must not outlive the `execute` callback that obtained it,
    // mirroring PassContext's own lifetime discipline (see this struct's own
    // doc comment above). Safe to call even on a default-constructed
    // PassContext (renderer/recordDraw.drawStats simply stay null - every
    // CommandBuffer method defensively asserts/no-ops on that, see
    // CommandBuffer.h).
    CommandBuffer Cmd() const noexcept { return CommandBuffer(cmd, renderer, recordDraw.drawStats); }
};

} // namespace gte::rg
