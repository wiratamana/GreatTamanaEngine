#pragma once

#include "EngineContext.h"
#include "IHostServices.h"
#include "InputFrame.h"
#include "ISurfaceProvider.h"
#include "../Application/RenderPassViewData.h"
#include "../ECS/Entity.h"
#include "../Game/Game.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/Culling/GpuDrivenBatchCache.h"
#include "../Renderer/Culling/GpuDrivenBatchDebugInfo.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderPipeline.h"
// editor-core-separation-2 campaign, PHASE2 - Core only ever holds/forwards
// a bare IFrameDebuggerCaptureRecorder* pointer (src/Core/
// FrameDebuggerCaptureRecorder.h, gte_core-owned), never
// FrameDebuggerCaptureContext directly - see that header's own doc comment
// and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1. MUST be
// included here, at file scope (NOT from inside `namespace gte { ... }`
// below) - this header opens its own `namespace gte { ... }` block, and
// including it from inside an already-open `namespace gte { ... }` here
// would create a bogus nested `gte::gte` namespace instead of extending the
// real `gte` namespace.
#include "FrameDebuggerCaptureRecorder.h"

// editor-core-separation-3 campaign, PHASE2
// (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md) - PluginHost is a
// concrete, gte_core-owned mechanism class (not a Bucket-B capability
// interface), so it is included here directly by name, mirroring
// FrameDebuggerCaptureRecorder.h's own "MUST be file-scope, not inside
// namespace gte { ... }" placement discipline immediately above (for the
// exact same reason - this header opens its own `namespace gte { ... }`
// block).
#include "Plugins/PluginHost.h"

#include <volk.h>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace gte {

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE documented
// exception to "gte_core never includes anything under src/Editor/":
// Core.h forward-declares IEditorLayer ONLY (this class holds nothing but a
// bare, nullable pointer to it) - Core.cpp is the one place that
// #includes the real src/Editor/EditorLayer.h header, mirroring
// src/Game/RenderSystem.h's own pre-existing FrameDebuggerCaptureContext*
// forward-declaration precedent exactly.
class IEditorLayer;

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - forward-declared only, mirroring IEditorLayer immediately above: Core
// only ever holds these behind std::unique_ptr in a std::vector (see
// m_capabilityOrchestrators below), never a concrete instance, so no
// #include of "Plugins/IPluginCapabilityOrchestrator.h" is needed here -
// Core.cpp is the one place that includes it for real.
class IPluginCapabilityOrchestrator;

// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - forward-declared only,
// mirroring IPluginCapabilityOrchestrator immediately above: Core only ever
// holds a raw, non-owning pointer into m_capabilityOrchestrators's own
// RenderFeatureCompositor entry (see m_renderFeatureCompositorPtr below), so
// no #include of "Plugins/RenderFeatureCompositor.h" is needed here -
// Core.cpp is the one place that #includes it for real, to construct the
// owned instance.
class RenderFeatureCompositor;

// Placeholder shape (editor-core-separation-1 campaign, PHASE12) - Core's own
// public contract (design doc Section 5.2) commits to exposing frame
// statistics via GetFrameStats(), but no phase in this 19-phase campaign
// actually wires real Profiling::FrameProfiler/DrawStats data into it -
// Application (and, later, EditorHost) keep talking to
// Profiling::FrameProfiler::Instance() directly, exactly as they do today.
// Left deliberately empty until a real, future need drives its actual
// shape, rather than guessing fields nothing populates or consumes yet.
struct FrameStats {
};

// gte_core's own public facade (design doc, Section 5; PHASE0's Locked
// Design Decision #8's own EXTENDED contract). Constructed by injecting an
// ISurfaceProvider& (the host's own window/surface abstraction) and an
// IHostServices& (the host's own logging/diagnostics hook) - Core itself
// never sees SDL, ImGui, or any concrete Editor type by name; the ONE
// exception is the nullable IEditorLayer* hook above, consulted only
// through a forward-declared pointer, never a concrete Editor include.
//
// editor-core-separation-1 campaign, PHASE13
// (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - Update()/BuildFrame()/
// Present() now contain the REAL per-frame orchestration logic that used to
// live inside Application::Run()'s own body: offscreen regime (Game
// View + Scene View, Atmosphere passes, GPU-skinning dispatch requests,
// GPU-driven batch culling readback), present regime (the swapchain-present
// pass), and every render-graph-frame-building IEditorLayer call site
// (Locked Design Decision #8's first bucket - GameViewTarget()/
// SceneViewTarget()/SceneViewProjection()/SceneViewCameraWorldPosition()/
// RenderSceneGrid()/AddBlurValidationPass()/FinalizeBlurValidationForSampling()/
// AddGBufferValidationPass()/FinalizeGBufferValidationForSampling()/
// SetGameViewCompositedTexture()/SetSceneViewCompositedTexture()/
// PrepareFrameDebuggerCaptureContext()/ConsumePendingFrameDebuggerReplayRequest()),
// reached ONLY through the null-checked m_editorLayer hook below. See
// PHASE13_COMPLETION_REPORT.md for the full, itemized accounting of every
// one of Application::Run()'s ~30 IEditorLayer call sites' new home.
//
// A handful of small, ADDITIVE accessors beyond Core's own originally-frozen
// minimum (design doc Section 5.3: "Exposed OUT... At minimum:... whatever
// draw-stats/profiling data the Editor UI displays") were added this phase,
// each documented at its own declaration below, so host-level code
// (Application::Run() today, EditorHost later) can keep reaching data that
// physically moved into Core without Core ever calling back into a host-level
// automation bridge/IEditorLayer method itself (design doc Section 6.1: "Core
// stays a pure engine facade: no HTTP server, no automation-bridge knowledge,
// ever").
class Core {
public:
    Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices);

    // editor-core-separation-6 campaign, PHASE2
    // (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
    // - declared here, DEFINED (as `= default`) in Core.cpp, NOT inline here.
    // m_capabilityOrchestrators holds std::unique_ptr<IPluginCapabilityOrchestrator>,
    // and IPluginCapabilityOrchestrator is only ever FORWARD-declared in this
    // header (see that forward declaration's own doc comment above) - an
    // implicitly-generated destructor needs the complete type at the point it
    // is generated, so a plain `class Core { ... };` with no explicit
    // destructor would fail to compile at every OTHER translation unit that
    // destroys a Core (e.g. `std::unique_ptr<Core>` in
    // tests/Core/CoreHeadlessConstructionTests.cpp, or EditorHost's own
    // `std::unique_ptr<Core> m_core`), long before Core.cpp itself is even
    // reached - the classic incomplete-type-behind-unique_ptr pitfall.
    // Defining it out-of-line in Core.cpp (which DOES #include the real
    // "Plugins/IPluginCapabilityOrchestrator.h") fixes this with zero other
    // behavior change.
    ~Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    // Advances Time (respecting Pause/Resume/Step - see InputFrame.h's own
    // doc comment on why the already-resolved playbackPaused/stepRequested
    // booleans travel inside `input` rather than growing this method's own
    // frozen 2-parameter signature) and dispatches Game::Update() - a safe
    // no-op if `input.inputState` is null.
    void Update(const InputFrame& input, float deltaTime);

    // The real per-frame Render Graph BUILD-AND-EXECUTE step for the
    // SYNCHRONOUS offscreen regime (Game View + Scene View together) - see
    // this class's own doc comment above for the full accounting of what
    // moved here.
    void BuildFrame();

    // The real per-frame Render Graph BUILD-AND-EXECUTE step for the
    // PIPELINED swapchain-present regime.
    void Present();

    Renderer& GetRenderer() noexcept { return m_renderer; }
    Registry& GetRegistry() noexcept { return m_game.GetRegistry(); }
    Game& GetGame() noexcept { return m_game; }
    rg::RenderGraph& GetRenderGraph() noexcept { return m_renderGraph; }
    EngineContext& GetEngineContext() noexcept { return m_engineContext; }
    Time& GetTime() noexcept { return m_engineContext.time; }
    const FrameStats& GetFrameStats() const noexcept { return m_frameStats; }

    // PHASE13 - this frame's freshly-built "instances culled this frame"
    // readout (GPU-Driven Frustum Culling + Indirect Draw campaign,
    // render-pass-5, PHASE6), one entry per real, eligible batch - populated
    // by BuildFrame() every frame. Application::Run()'s own
    // IEditorLayer::BuildUI() call (a host-level concern that stays directly
    // on Application, never moving into Core - see Locked Design Decision #8)
    // needs this value, which now physically lives inside Core - a small,
    // additive accessor, exactly the kind design doc Section 5.3 anticipates
    // ("whatever draw-stats/profiling data the Editor UI displays").
    const std::vector<GpuDrivenBatchDebugInfo>& GetGpuDrivenBatchDebugInfo() const noexcept
    {
        return m_gpuDrivenBatchDebugInfoLastFrame;
    }

    // editor-core-separation-8 campaign, PHASE1 - the ONE registry instance
    // shared by BOTH m_offscreenRenderPipeline and m_presentRenderPipeline (see
    // the constructor-time wiring, Core.cpp). Non-const, non-null (a plain owned
    // member, never a pointer) - the "Render Graph" panel (PHASE4) and
    // RenderGraphControlCommandBridge's pump (PHASE5) both mutate THROUGH this
    // exact reference, on the main thread only (see
    // RenderPassToggleRegistry.h's own header comment for why no mutex is
    // needed).
    rg::RenderPassToggleRegistry& GetRenderPassToggleRegistryMutable() noexcept
    {
        return m_renderPassToggleRegistry;
    }

    // PHASE13 - the SAME AtmosphereSettings/AtmosphereLutRenderer instances
    // Core's own per-frame Atmosphere pass-building code (BuildFrame())
    // reads/writes, now exposed so Application::Run()'s own
    // IEditorLayer::BuildUI() call (host-level, unmoved - the "Atmosphere"
    // panel edits atmosphereSettings live, and reads back
    // atmosphereLutRenderer's real output textures for its own validation
    // buttons) can keep reaching them by reference, exactly as it already
    // does for Renderer/Game/RenderGraph (see Application.h's own
    // m_renderer/m_game/m_renderGraph reference-member precedent, PHASE12).
    AtmosphereSettings& GetAtmosphereSettings() noexcept { return m_atmosphereSettings; }
    AtmosphereLutRenderer& GetAtmosphereLutRenderer() noexcept { return m_atmosphereLutRenderer; }

    // PHASE13 - this frame's already-resolved Game View render target (the
    // SAME value BuildFrame() itself just used internally this frame,
    // IEditorLayer::GameViewTarget()'s own real, current answer) - nullptr on
    // any frame the "Game" panel isn't visible (or there is no Editor at
    // all). Exposed so Application::Run()'s own FrameCaptureBridge servicing
    // (GET /get_game_view's fast-fail branch + success-path capture) - a
    // HOST-LEVEL AUTOMATION concern per design doc Section 6.1 ("Core stays a
    // pure engine facade: no HTTP server, no automation-bridge knowledge,
    // ever") - can react to it without a second, duplicate
    // IEditorLayer::GameViewTarget() call site (Locked Design Decision #8's
    // own Definition of Done: every render-graph-frame-building IEditorLayer
    // call site "now lives inside Core::BuildFrame()... never directly on
    // Application anymore"). Safe to read any time after BuildFrame() has
    // returned this frame.
    RenderTexture* GetGameViewTargetThisFrame() const noexcept { return m_gameTargetThisFrame; }

    // PHASE13 - Application/EditorHost supplies the ONE callback that
    // actually calls IEditorLayer::Render(cmd) - explicitly a HOST-LEVEL
    // IEditorLayer method (Locked Design Decision #8's second bucket:
    // "called directly against the same IEditorLayer instance... Core never
    // calls these"). This is NOT Core calling Render() itself - the closure's
    // own body (defined in Application.cpp/EditorHost.cpp, capturing the
    // HOST's own m_editorLayer, never Core's) is simply handed to Core as
    // plain data, for Core's own "Present" RenderPipeline provider to invoke
    // at the correct point inside the swapchain-present pass - mirrors
    // exactly what Application::Run()'s own m_recordImGuiThisFrame lambda
    // already did before this phase (see RenderPasses.h's AddPresentPass()).
    // Expected to be called exactly ONCE, at host construction time (the
    // callback's own behavior never varies frame-to-frame) - see
    // Application::Application()'s own constructor body.
    void SetPresentImGuiRecorder(std::function<void(VkCommandBuffer)> recorder)
    {
        m_presentImGuiRecorder = std::move(recorder);
    }

    // PHASE13 - keeps Present()'s own "no Game/Scene panel visible, render
    // Game directly to the swapchain" fallback aspect-ratio computation
    // correct across a live OS window resize, mirroring Application's own
    // former m_windowWidth/m_windowHeight cache exactly (Window::Width()/
    // Height() only ever reflect CONSTRUCTION size, never a later resize -
    // see Window.h's own doc comment) - called by Application::Run()'s SDL
    // polling loop (a host-level concern, unmoved) alongside its existing
    // m_renderer.OnResize()/m_editorLayer->OnWindowResized() calls, on every
    // real WindowResized event.
    void NotifyWindowResized(int width, int height) noexcept
    {
        m_windowWidth = width;
        m_windowHeight = height;
    }

    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE
    // nullable, "big" opaque hook mirroring FrameDebuggerCaptureContext*'s
    // own already-proven small-scale opaque-pointer pattern (never a new
    // Bucket-B-style micro interface, and never split into smaller pieces -
    // see that Locked Design Decision's full text). EditorHost (gte_editor)
    // supplies the real ImGuiEditorLayer instance after constructing it
    // (PHASE15); a Player host never calls this, leaving it nullptr
    // forever. Core::BuildFrame() (PHASE13) calls through this pointer,
    // ALWAYS null-checked, for ONLY the render-graph-frame-building subset
    // of IEditorLayer's methods PHASE0 enumerates - never for UI-building/
    // input-routing methods, which stay a host-level
    // (Application/EditorHost) concern and never reach Core at all.
    void SetEditorLayerHook(IEditorLayer* editorLayer) noexcept { m_editorLayer = editorLayer; }

    // editor-core-separation-3 campaign, PHASE2
    // (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md) -
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9: plugins load
    // once, at host-construction time, never re-scanned per frame. Mirrors
    // SetEditorLayerHook()'s own "small, explicitly host-called method, not a
    // constructor parameter" precedent exactly - EditorHost's constructor
    // (gte_editor) calls this exactly once, gated behind
    // `#if GTE_ENABLE_PLUGINS` at THAT call site (this pass-through method
    // itself always compiles - see PluginHost.h's own doc comment for why
    // the class it forwards to is capability-agnostic and mechanical).
    void LoadPlugins(const std::filesystem::path& pluginsDirectory);

    // Read accessor for PHASE3 (render-feature capability lookup) and
    // PHASE4 (editor-panel capability lookup) - both look up capabilities
    // via AllLoadedModules(), never re-scanning the plugins/ folder
    // themselves (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 -
    // exactly ONE PluginHost instance/scan per process).
    const PluginHost& GetPluginHost() const noexcept { return m_pluginHost; }

    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - a plain, zero-cost,
    // non-owning pointer into m_capabilityOrchestrators's own
    // RenderFeatureCompositor entry (never a dynamic_cast/RTTI lookup - this
    // codebase uses no RTTI anywhere, confirmed via search_in_dir for
    // "dynamic_cast" across the whole src/ tree during this phase's own
    // review). Set once, at construction time, by
    // RegisterBuiltinCapabilityOrchestrators() (the SAME statement that
    // pushes the owning std::unique_ptr into m_capabilityOrchestrators - see
    // Core.cpp), so this is never null after construction completes. The
    // "Render Graph" panel (RenderGraphPanel::Build(), reached via
    // IEditorLayer::BuildUI()'s own trailing parameter) calls
    // ->DebugSnapshot() through this pointer, guarded by a null-check at the
    // call site anyway - exactly mirroring every other nullable hook this
    // class already exposes (see m_editorLayer's own doc comment above).
    const RenderFeatureCompositor* GetRenderFeatureCompositor() const noexcept
    {
        return m_renderFeatureCompositorPtr;
    }

    // editor-core-separation-6 campaign, PHASE2
    // (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
    // Step 3.3) - a PUBLIC NESTED type of Core itself (never a free-standing
    // namespace gte struct), so both LegacyRenderFeatureOrchestrator (this
    // phase) and RenderFeatureCompositor (PHASE4, which additionally needs
    // `.extent`) can resolve "this view's current plugin-facing target"
    // through ONE shared accessor instead of independently re-deriving
    // isGameView/compositedKey/pluginTarget inline themselves.
    struct PluginRenderFeatureTargetInfo {
        rg::TextureHandle target;
        VkExtent2D extent{};
        // editor-core-separation-6 campaign, PHASE4
        // (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) -
        // RenderFeatureCompositor (unlike LegacyRenderFeatureOrchestrator,
        // which never reads this field) needs a REAL VkSampler for `target`
        // to seed its own blend chain by sampling the view's CURRENT
        // composited image - an imported TextureHandle's own
        // PassContext::resolveTexture() never carries a sampler (see
        // RenderGraph.cpp). See FindPluginRenderFeatureTarget()'s own
        // updated doc comment (Core.cpp) for exactly how this is resolved.
        VkSampler sampler = VK_NULL_HANDLE;
    };

    // Returns std::nullopt when `frame.currentView` has no known
    // RenderPassViewData this frame (mirrors FindViewData()'s own nullptr
    // return, translated into optional form for this public accessor).
    // Calls the private FindViewData() internally - callers outside Core.cpp
    // never need FindViewData() directly, and never need a
    // `friend class LegacyRenderFeatureOrchestrator;` declaration either.
    //
    // editor-core-separation-6 campaign, PHASE4 - NOT `const` (a real,
    // confirmed adjustment from PHASE2's own original signature): resolving
    // `.sampler` above needs AtmosphereLutRenderer::CompositedOutput(),
    // which is itself a non-const method (it returns a non-const
    // RenderTexture*, matching every other AtmosphereLutRenderer accessor -
    // see that class's own header) - calling it from a `const Core*` would
    // require either a `mutable` AtmosphereLutRenderer member or a parallel
    // const overload on AtmosphereLutRenderer itself, both a larger,
    // less-honest change than simply dropping `const` here. Confirmed via
    // search_in_dir: the only real call site (LegacyRenderFeatureOrchestrator)
    // already holds a non-const `Core&`, so this is a safe, zero-impact
    // widening.
    std::optional<PluginRenderFeatureTargetInfo> FindPluginRenderFeatureTarget(
        const rg::RenderPassFrameContext& frame);

private:
    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE5 - one eligible batch's own THIS-FRAME render data, ready for the
    // "GpuDrivenBatches" provider (registered in
    // RegisterOffscreenRenderPipelineProviders(), between "RenderOpaque"'s
    // own Register() call and "DrawSkyBackground"'s own Register() call -
    // see that function's own comment) to declare its three passes against.
    // Relocated verbatim from Application.h (editor-core-separation-1
    // campaign, PHASE13) - see that file's own former doc comment (now here)
    // for the full reasoning, unchanged.
    struct GpuDrivenBatchRenderData {
        MeshHandle mesh;
        PipelineHandle originalPipeline;
        std::size_t instanceCount = 0;
        rg::BufferHandle inputHandle;
        rg::BufferHandle indirectHandle;
        rg::BufferHandle countHandle;
        VkBuffer indirectBufferNative = VK_NULL_HANDLE;
        VkBuffer countBufferNative = VK_NULL_HANDLE;
        VkDescriptorSet cullingDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet instanceBufferDescriptorSet = VK_NULL_HANDLE;
        const char* resetPassName = nullptr;
        const char* cullingPassName = nullptr;
        const char* indirectDrawPassName = nullptr;
        const char* displayName = nullptr;
    };

    // editor-core-separation-6 campaign, PHASE2
    // (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
    // Step 3.4) - populates m_capabilityOrchestrators (below), called once,
    // from the constructor, BEFORE RegisterOffscreenRenderPipelineProviders()
    // (a clearer, more readable convention - not itself load-bearing, since
    // the "PluginRenderFeatures" provider's lambda captures `this` and reads
    // m_capabilityOrchestrators at CALL time every frame, not at
    // registration time).
    void RegisterBuiltinCapabilityOrchestrators();

    // render-pass-3 campaign, PHASE2/PHASE3 - registers every remaining
    // production pass onto m_offscreenRenderPipeline. See Core.cpp for the
    // real body (relocated verbatim from
    // Application::RegisterOffscreenRenderPipelineProviders(), PHASE13).
    void RegisterOffscreenRenderPipelineProviders();

    // render-pass-3 campaign, PHASE3 (Step 3.5) - registers the ONE
    // "Present" provider onto m_presentRenderPipeline. See Core.cpp for the
    // real body (relocated verbatim from
    // Application::RegisterPresentRenderPipelineProvider(), PHASE13).
    void RegisterPresentRenderPipelineProvider();

    // render-pass-3 campaign, PHASE3 (Step 3.1) - looks up THIS frame's own
    // RenderPassViewData for `view` out of m_currentViewDataThisFrame
    // (below). Relocated verbatim from Application::FindViewData(), PHASE13.
    const RenderPassViewData* FindViewData(rg::RenderViewId view) const noexcept;

    // Declared first - independent of every other member below, and not
    // itself part of the ctor initializer-list ordering concern the real
    // owned members are (it uses its own default member initializer,
    // nullptr).
    IEditorLayer* m_editorLayer = nullptr;

    // Ownership relocated here from Application (editor-core-separation-1
    // campaign, PHASE12) - see design doc Section 2.1's ownership graph.
    // Declaration order matters (matches real constructor-initializer-list
    // order): m_renderer needs `surfaceProvider` (the constructor
    // parameter) only; m_renderGraph needs m_renderer already constructed;
    // m_game/m_engineContext have no dependency on either.
    Renderer m_renderer;
    rg::RenderGraph m_renderGraph;
    Game m_game;
    EngineContext m_engineContext;

    // See FrameStats's own doc comment above - a placeholder, never
    // written to by anything in this campaign.
    FrameStats m_frameStats;

    // PHASE13 - see NotifyWindowResized()'s own doc comment above. Seeded
    // from `surfaceProvider`'s own CONSTRUCTION-time size in Core's
    // constructor (Core.cpp) - correct because, at that exact moment, no
    // resize has happened yet.
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    // render-pass-3 campaign, PHASE2/PHASE3 - the new, generic pass-
    // DECLARATION layer sitting strictly ABOVE m_renderGraph/RenderGraphBuilder.
    // Relocated here (from Application, editor-core-separation-1 campaign,
    // PHASE13) together with their only real consumer, the per-frame
    // orchestration logic itself - see PHASE12_COMPLETION_REPORT.md's own
    // "Genuine ambiguity found" section for why this move was deferred to
    // this exact phase.
    rg::RenderPipeline m_offscreenRenderPipeline;
    rg::RenderPipeline m_presentRenderPipeline;

    // editor-core-separation-8 campaign, PHASE1 - the ONE registry instance
    // shared by BOTH m_offscreenRenderPipeline and m_presentRenderPipeline
    // (see the constructor-time wiring, Core.cpp) - see
    // GetRenderPassToggleRegistryMutable()'s own doc comment above. A plain
    // owned value member (no Vulkan/heavy dependency, needs no lazy
    // construction).
    rg::RenderPassToggleRegistry m_renderPassToggleRegistry;

    // Populated fresh, every frame, by BuildFrame() itself, immediately
    // before calling m_offscreenRenderPipeline.DeclareInto() - read ONLY by
    // the "GpuSkinning" provider (registered in the constructor, capturing
    // `this`) - never written to by anything except BuildFrame().
    std::vector<AnimationSystem::GpuSkinningDispatchRequest> m_gpuSkinningRequestsThisFrame;
    std::vector<rg::BufferHandle> m_gpuSkinningHandlesThisFrame;

    // render-pass-3 campaign, PHASE3 (Step 3.1) - THIS frame's per-view data
    // (Game View and/or Scene View, whichever are actually visible this
    // frame) - populated fresh, every frame, by BuildFrame() itself,
    // immediately before calling m_offscreenRenderPipeline.DeclareInto().
    std::vector<RenderPassViewData> m_currentViewDataThisFrame;

    // render-pass-3 campaign, PHASE2 - Game-View-only (see RenderPasses.h's
    // own AddRenderOpaquePass() doc comment on why a real, non-null capture
    // pointer is NEVER handed to Scene View/Present).
    IFrameDebuggerCaptureRecorder* m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE5 - the ONE persistent, per-batch GPU resource cache (PHASE4),
    // constructed once, reused every frame.
    GpuDrivenBatchCache m_gpuDrivenBatchCache;

    // Populated fresh, every frame, by BuildFrame() itself, immediately
    // before calling m_offscreenRenderPipeline.DeclareInto(). Computed and
    // consumed Game-View-only (Locked Design Decision 11) - left empty on
    // any frame the Game View isn't actually visible this frame.
    std::vector<GpuDrivenBatchRenderData> m_gpuDrivenBatchesThisFrame;

    // The exact set of entities that successfully got a batch's worth of
    // buffers imported THIS frame.
    std::unordered_set<Entity> m_gpuDrivenBatchedEntitiesThisFrame;

    // The Game View's own view-projection matrix this frame.
    Mat4 m_gpuDrivenGameViewProjectionThisFrame;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 - this frame's own "instances culled this frame" readout - see
    // GetGpuDrivenBatchDebugInfo()'s own doc comment above.
    std::vector<GpuDrivenBatchDebugInfo> m_gpuDrivenBatchDebugInfoLastFrame;

    // render-pass-3 campaign, PHASE3 (Step 3.5) - populated fresh, every
    // frame, by Present() itself, immediately before calling
    // m_presentRenderPipeline.DeclareInto() - read ONLY by the "Present"
    // provider (registered in the constructor, capturing `this`).
    bool m_needsDirectGameRenderThisFrame = false;
    std::optional<float> m_directGameRenderAspectThisFrame;
    rg::TextureHandle m_swapchainImageThisFrame;
    std::function<void(VkCommandBuffer)> m_recordImGuiThisFrame;

    // PHASE13 - see SetPresentImGuiRecorder()'s own doc comment above.
    std::function<void(VkCommandBuffer)> m_presentImGuiRecorder;

    // Atmosphere Scattering + Aerial Perspective campaign - owns every
    // atmosphere LUT/pass's ComputePipeline/descriptor set/output texture
    // across frames. Relocated here (from Application, editor-core-
    // separation-1 campaign, PHASE13) together with its only real per-frame
    // consumer.
    AtmosphereLutRenderer m_atmosphereLutRenderer;

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 8 - the
    // small set of tunable, non-spatial atmosphere knobs edited live via the
    // Editor's "Atmosphere" panel (host-level, unmoved) - see
    // GetAtmosphereSettings()'s own doc comment above for why this stays
    // reachable from the host.
    AtmosphereSettings m_atmosphereSettings;

    // editor-core-separation-3 campaign, PHASE2 - see LoadPlugins()/
    // GetPluginHost()'s own doc comments above. No constructor dependency on
    // any other Core member, so appended near the end of the private member
    // list, immediately before m_gameTargetThisFrame/m_sceneTargetThisFrame,
    // which similarly have no cross-member dependency.
    PluginHost m_pluginHost;

    // editor-core-separation-6 campaign, PHASE2
    // (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md,
    // Step 3.4) - populated ONCE, at construction time, by
    // RegisterBuiltinCapabilityOrchestrators() (declared above) - never
    // re-populated per frame. Declared after m_pluginHost (no constructor
    // dependency on it - orchestrators only ever read m_pluginHost lazily,
    // at call time, via m_core.GetPluginHost()).
    std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>> m_capabilityOrchestrators;

    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - see GetRenderFeatureCompositor()'s
    // own doc comment above for exactly why this exists and how it stays in
    // sync with m_capabilityOrchestrators (set at the SAME statement that
    // pushes the owning std::unique_ptr, inside
    // RegisterBuiltinCapabilityOrchestrators() - Core.cpp).
    RenderFeatureCompositor* m_renderFeatureCompositorPtr = nullptr;

    // PHASE13 - this frame's already-resolved Game/Scene View render targets
    // (IEditorLayer::GameViewTarget()/SceneViewTarget()'s own real answers,
    // called through this class's own m_editorLayer hook, Locked Design
    // Decision #8's first bucket) - see GetGameViewTargetThisFrame()'s own
    // doc comment above for why m_gameTargetThisFrame specifically is also
    // exposed publicly. m_sceneTargetThisFrame has no host-level consumer
    // today (kept private, internal-only, mirroring the "only add an
    // accessor when a real, confirmed need exists" discipline this whole
    // phase followed for every other new accessor).
    RenderTexture* m_gameTargetThisFrame = nullptr;
    RenderTexture* m_sceneTargetThisFrame = nullptr;
};

} // namespace gte
