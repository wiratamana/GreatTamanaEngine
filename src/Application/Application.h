#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "../Core/EngineContext.h"
// editor-core-separation-1 campaign, PHASE12 - gte::Core, the new gte_core
// public facade Application now constructs and delegates its own
// Renderer/RenderGraph/Game/EngineContext-Time ownership to (see this
// class's own m_core/m_renderer/m_game/m_engineContext member comments
// below). Core.h itself already includes EngineContext.h/Renderer.h/
// RenderGraph.h/Game.h transitively - the explicit includes below/above are
// kept anyway for this file's own existing "state exactly what this file
// needs" discipline, unaffected by this addition.
#include "../Core/Core.h"
#include "../Core/EditorCapabilities.h"
#include "../Editor/EditorLayer.h"
#include "../Game/Game.h"
#include "../Network/NetworkServer.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
// render-pass-3 campaign, PHASE2 (PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md)
// - the new, generic pass-DECLARATION layer (PHASE1's own
// RenderPipeline.h). Application owns the ONE m_offscreenRenderPipeline
// instance registered with this phase's first two real providers -
// "GpuSkinning" and "RenderOpaque" - see this class's own member comment
// below and Application.cpp's RegisterOffscreenRenderPipelineProviders().
#include "../Renderer/RenderGraph/RenderPipeline.h"
// render-pass-3 campaign, PHASE3 (PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md)
// - the Application-layer "what is a view" data (RenderPassViewData) plus
// TranslateLegacyViewScope() - see that header's own doc comment for why
// this stays a separate, Application-layer header rather than living inside
// RenderPipeline.h itself.
#include "RenderPassViewData.h"
#include "../Renderer/VolumeTexturePreviewRenderer.h"
#include "../Window/Window.h"
#include "AssetImportCommandBridge.h"
#include "EngineCommandBridge.h"
#include "EditorUiCommandBridge.h"
#include "FrameCaptureBridge.h"
#include "FrameDebuggerCommandBridge.h"

namespace gte {

// The only layer that knows about SDL directly. Owns SDL's lifetime plus the
// main loop, and wires the Window/Renderer/Editor/Game abstractions
// together.
//
// Application is also the composition root for the optional Editor layer:
// it is the only place that asks "where should Game render this frame?"
// (IEditorLayer::GameViewTarget() - an off-screen RenderTexture in an
// Editor build, or nullptr meaning straight to the swapchain in a release
// build) and wires the answer into Renderer::RenderOffscreen()/Present().
// Game itself never knows the Editor exists either way.
class Application {
public:
    Application(const std::string& title, int width, int height);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Runs the main loop until the window is closed. Returns a process exit code.
    int Run();

    // editor-core-separation-1 campaign, PHASE5
    // (PHASE5_EDITOR_CAPABILITY_INTERFACES_DESIGN.md) - registers the real
    // Editor-side ISceneIOCapability implementation (Core/EditorCapabilities.h)
    // that CORE-destined code (EngineCommandDispatch.cpp, PHASE6) will consult
    // at runtime instead of a compile-time `#if GTE_ENABLE_EDITOR`. Defaults
    // to nullptr (no capability registered, matching a future Player host
    // that never calls this) - PHASE5 itself never calls this setter; PHASE6
    // is what constructs a real EditorSceneIOCapability and wires it here,
    // from Application's own constructor. This exact nullable-pointer/setter
    // shape is the TEMPORARY home Application (today's composition root)
    // provides - PHASE12/PHASE15 relocate ownership into Core/EditorHost
    // without needing to redesign ISceneIOCapability itself (see this
    // header's own EditorCapabilities.h include comment).
    void SetSceneIOCapability(ISceneIOCapability* capability) noexcept { m_sceneIOCapability = capability; }

private:
    // render-pass-3 campaign, PHASE2/PHASE3 - registers every remaining
    // OFFSCREEN-regime provider (Atmosphere ×3 provider wrappers, GPU
    // Skinning, Opaque, Sky Background, Transparent, Atmosphere Composite)
    // onto m_offscreenRenderPipeline (below). Called once, from the
    // constructor body - see Application.cpp.
    void RegisterOffscreenRenderPipelineProviders();

    // render-pass-3 campaign, PHASE3 (Step 3.5) - registers the ONE
    // "Present" provider onto m_presentRenderPipeline (below). Called once,
    // from the constructor body - see Application.cpp.
    void RegisterPresentRenderPipelineProvider();

    // render-pass-3 campaign, PHASE3 (Step 3.1) - looks up THIS frame's own
    // RenderPassViewData for `view` out of m_currentViewDataThisFrame
    // (below) - returns nullptr if `view` has no matching entry this frame
    // (should never happen for a view actually present in
    // frame.activeViews, but a provider must never assume without checking).
    const RenderPassViewData* FindViewData(rg::RenderViewId view) const noexcept;

    // RAII guard for SDL_Init()/SDL_Quit(). Declared FIRST so it is
    // constructed before, and destroyed after, every other SDL-owning member
    // below it (Window, Renderer, ...) - this keeps init/shutdown ordering
    // correct automatically instead of relying on manual cleanup code.
    struct SdlContext {
        SdlContext();
        ~SdlContext();

        SdlContext(const SdlContext&) = delete;
        SdlContext& operator=(const SdlContext&) = delete;
    };

    SdlContext m_sdlContext;
    Window m_window;

    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md) - a trivial,
    // TEMPORARY IHostServices implementation, needed only because Core's
    // constructor requires a real IHostServices& (Core's own frozen public
    // contract, design doc Section 5.2). Routes into the SAME global
    // log-sink mechanism GTE_LOG_* itself already uses (Core/LogSink.h) -
    // this is NOT a second, competing logging path, just a thin adapter
    // satisfying Core's constructor signature. PHASE16 (EditorHost Main
    // Loop and Automation Bridges) replaces this with EditorHost's own,
    // permanent IHostServices implementation once EditorHost exists; this
    // one is not meant to outlive Application itself (retired in PHASE17).
    struct ApplicationHostServices : IHostServices {
        void Log(LogLevel level, std::string_view message) override
        {
            LogToActiveSink(level, "Core", message);
        }
    };

    ApplicationHostServices m_hostServices;

    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md) - gte_core's own
    // public facade (src/Core/Core.h). Owns the real Renderer/RenderGraph/
    // Game/EngineContext-Time instances (see m_renderer's own doc comment
    // below for why Application still keeps same-named REFERENCE members
    // bound to these, rather than every call site being rewritten to
    // `m_core.GetX()` in this phase). Constructed injecting m_window as
    // ISurfaceProvider& (Window implements it - PHASE10) and m_hostServices
    // (above) as IHostServices&. `SetEditorLayerHook(m_editorLayer.get())`
    // is called once, from this class's own constructor BODY (never the
    // initializer list - m_editorLayer must already be fully constructed
    // first) - see Application.cpp. Declared right after m_window/
    // m_hostServices (its own two constructor dependencies) so both are
    // already fully constructed by the time this runs.
    Core m_core;
    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md) - Renderer/RenderGraph/
    // Game/EngineContext-Time are no longer owned VALUE members here: the
    // real instances now physically live inside m_core (below), per the
    // design doc's own Section 2.1 ownership graph. These four are kept as
    // same-named REFERENCE members bound to m_core's own accessors at
    // construction time - a documented, lower-risk DEVIATION from PHASE12's
    // own literal "rewrite every call site to go through
    // m_core.GetRenderer()/m_core.GetGame()/etc" instruction: Run() and the
    // two Register*Provider() methods below (none of which move into Core
    // until PHASE13) reference these members - and capture `this` in dozens
    // of lambdas - throughout ~2000+ lines; keeping the same names here lets
    // every one of those existing call sites keep compiling and behaving
    // BYTE-FOR-BYTE UNCHANGED this phase, with the literal "go through
    // m_core.GetX()" rewrite happening naturally in PHASE13 once that code
    // physically moves into Core's own methods (where it becomes ordinary,
    // direct member access again). See PHASE12_COMPLETION_REPORT.md for the
    // full reasoning (confirmed as an acceptable resolution to a genuine
    // ambiguity this phase's own strategy file left open).
    Renderer& m_renderer;
    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
    // the ONE shared RenderGraph instance Game view/Scene view/Present are
    // all recorded through (two Execute() calls per frame - see Run() and
    // RenderPasses.h). Declared right after m_renderer (constructed with a
    // reference to it) so it's already fully constructed by the time
    // m_editorLayer/m_game below might indirectly need it.
    rg::RenderGraph& m_renderGraph;

    // render-pass-3 campaign, PHASE2/PHASE3 - the new, generic pass-
    // DECLARATION layer sitting strictly ABOVE m_renderGraph/RenderGraphBuilder
    // (PHASE0_MASTER_STRATEGY.md's Locked Design Decision 6: "Two separate
    // RenderPipeline instances, not one"). m_offscreenRenderPipeline covers
    // every pass declared inside the SYNCHRONOUS offscreen Execute() call
    // (GPU Skinning, Atmosphere ×3 wrappers, Opaque, Sky Background,
    // Transparent - Game View AND Scene View alike, via ONE generic
    // per-view loop, PHASE3); m_presentRenderPipeline covers "Present"
    // alone, in the SEPARATE, PIPELINED swapchain Execute() call (PHASE3,
    // Step 3.5) - the two are NEVER shared, and never see each other's own
    // blackboard/frame context. Both have their own
    // SetLegacyViewScopeTranslator(&TranslateLegacyViewScope) wired once, at
    // construction time (see RegisterOffscreenRenderPipelineProviders()/
    // RegisterPresentRenderPipelineProvider()).
    rg::RenderPipeline m_offscreenRenderPipeline;
    rg::RenderPipeline m_presentRenderPipeline;

    // render-pass-3 campaign, PHASE2 - populated fresh, every frame, by
    // Run() itself, IMMEDIATELY BEFORE calling
    // m_offscreenRenderPipeline.DeclareInto() - a rg::RenderPassProvider
    // callback (PHASE1's own RenderPipeline.h) has no rg::RenderGraphBuilder&
    // access of its own to call ImportBuffer() (see this phase's own
    // completion report for the full "wrinkle" resolution this represents:
    // resolving BufferHandle values OUTSIDE any provider, by the caller,
    // rather than growing RenderGraphBuilder.h's own public surface). Read
    // ONLY by the "GpuSkinning" provider (registered in the constructor,
    // capturing `this`) - never written to by anything except Run().
    std::vector<AnimationSystem::GpuSkinningDispatchRequest> m_gpuSkinningRequestsThisFrame;
    std::vector<rg::BufferHandle> m_gpuSkinningHandlesThisFrame;

    // render-pass-3 campaign, PHASE3 (Step 3.1) - THIS frame's per-view data
    // (Game View and/or Scene View, whichever are actually visible this
    // frame) - populated fresh, every frame, by Run() itself, immediately
    // before calling m_offscreenRenderPipeline.DeclareInto(). Read by
    // FindViewData() above, which every per-view provider
    // ("RenderOpaque"/"DrawSkyBackground"/"RenderTransparent"/
    // "AtmosphereViewLut"/"AtmosphereComposite") calls to resolve
    // `frame.currentView`'s own color target/aspect/view-projection/eye
    // position/scene-overlay callback - see RenderPassViewData.h. REPLACES
    // PHASE2's own single-view-only
    // m_currentGameViewTargetForOffscreenPipeline/
    // m_currentGameViewAspectForOffscreenPipeline scalar members (removed
    // this phase - PHASE2's own completion report anticipated exactly this
    // generalization).
    std::vector<RenderPassViewData> m_currentViewDataThisFrame;

    // render-pass-3 campaign, PHASE2 - Game-View-only (see
    // RenderPasses.h's own AddRenderOpaquePass() doc comment on why a real,
    // non-null capture pointer is NEVER handed to Scene View/Present). Still
    // a scalar Application member (not part of RenderPassViewData) since
    // only ONE view can ever carry a real, non-null value here per frame.
    FrameDebuggerCaptureContext* m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE5 (task_manager/render-pass-5/
    // PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md) - the ONE
    // persistent, per-batch GPU resource cache (PHASE4), owned here exactly
    // like m_atmosphereLutRenderer/m_volumeTexturePreviewRenderer below are:
    // a stateful, Renderer-layer helper with no ECS/Game dependency of its
    // own (see GpuDrivenBatchCache.h's own "deliberately kept Renderer-
    // layer-clean" doc comment), constructed once, reused every frame -
    // RenderSystem::CollectGpuDrivenBatches() (called via
    // m_game.GetRenderSystem(), below) never owns this itself, so whoever
    // calls it every frame must.
    GpuDrivenBatchCache m_gpuDrivenBatchCache;

    // One eligible batch's own THIS-FRAME render data, ready for the new
    // "GpuDrivenBatches" provider (registered in
    // RegisterOffscreenRenderPipelineProviders(), between "RenderOpaque"'s
    // own Register() call and "DrawSkyBackground"'s own Register() call -
    // see that function's own comment) to declare its three passes against.
    // `mesh`/`originalPipeline` are RESOLVED FRESH inside each pass's own
    // deferred `execute` lambda (never a raw pointer captured here) -
    // ResourcePool<T,HandleT>'s own backing std::vector may reallocate if a
    // new Mesh/Pipeline is ever registered, so a pointer captured at
    // collection time could theoretically dangle by the time a deferred
    // pass's `execute` callback actually runs later this same Execute()
    // call - re-resolving by handle at execute time (a cheap pool lookup)
    // avoids this risk entirely.
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
        // Stable (whole-process-lifetime) pass/resource names - see
        // Application.cpp's own GpuDrivenBatchNamePool().
        const char* resetPassName = nullptr;
        const char* cullingPassName = nullptr;
        const char* indirectDrawPassName = nullptr;
        // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
        // PHASE6 - this batch's own stable DISPLAY name (e.g.
        // "GpuDrivenBatch0") - see GpuDrivenBatchNames's own doc comment
        // (Application.cpp).
        const char* displayName = nullptr;
    };

    // Populated fresh, every frame, by Run() itself (the offscreen regime's
    // own `build` lambda), immediately before calling
    // m_offscreenRenderPipeline.DeclareInto() - mirrors
    // m_gpuSkinningRequestsThisFrame/m_gpuSkinningHandlesThisFrame's own
    // exact "populate right before DeclareInto(), read inside the provider"
    // shape above. Computed and consumed Game-View-only (Locked Design
    // Decision 11) - left empty on any frame the Game View isn't actually
    // visible this frame.
    std::vector<GpuDrivenBatchRenderData> m_gpuDrivenBatchesThisFrame;

    // The exact set of entities that successfully got a batch's worth of
    // buffers imported THIS frame - built from the SAME result as
    // m_gpuDrivenBatchesThisFrame above (one source of truth - see
    // PHASE5_RENDERGRAPH_PASS_WIRING_AND_PRODUCTION_CUTOVER.md's own Section
    // 3.1). Passed into RenderSystem::Draw()'s new `batchedEntities`
    // parameter at EXACTLY ONE call site: the Game-View branch of the
    // "RenderOpaque" provider.
    std::unordered_set<Entity> m_gpuDrivenBatchedEntitiesThisFrame;

    // The Game View's own view-projection matrix this frame - captured here
    // (alongside `gameViewData.viewProjection`, computed in the same place)
    // so the "GpuDrivenBatches" provider's culling/indirect-draw passes
    // (which run LATER this same frame, inside DeclareInto()) can read it
    // without re-resolving the active ECS Camera a second time.
    Mat4 m_gpuDrivenGameViewProjectionThisFrame;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
    // - this frame's own "instances culled this frame" readout, one entry
    // per real, eligible batch (see m_gpuDrivenBatchesThisFrame above) -
    // populated by Run() itself, immediately after
    // Renderer::EndOffscreenRenderGraphRecording() returns (the exact point
    // every buffer this frame's culling dispatch wrote is already
    // fence-proven complete - see GpuDrivenBatchCache::
    // ReadLastKnownVisibleCount()'s own doc comment), then handed into
    // IEditorLayer::BuildUI()'s new `gpuDrivenBatchDebugInfo` parameter.
    // Cleared unconditionally every frame, alongside m_gpuDrivenBatchesThisFrame
    // above (never left stale from a previous frame Game View was active).
    std::vector<GpuDrivenBatchDebugInfo> m_gpuDrivenBatchDebugInfoLastFrame;

    // render-pass-3 campaign, PHASE3 (Step 3.5) - populated fresh, every
    // frame, by Run() itself, immediately before calling
    // m_presentRenderPipeline.DeclareInto() from inside the SEPARATE,
    // PIPELINED swapchain Execute() call's own `build` lambda - read ONLY by
    // the "Present" provider (registered in the constructor, capturing
    // `this`). Mirrors the exact same "populate right before DeclareInto(),
    // read inside the provider" shape already established above for the
    // offscreen regime.
    bool m_needsDirectGameRenderThisFrame = false;
    std::optional<float> m_directGameRenderAspectThisFrame;
    rg::TextureHandle m_swapchainImageThisFrame;
    std::function<void(VkCommandBuffer)> m_recordImGuiThisFrame;


    // Atmosphere Scattering + Aerial Perspective campaign, Phase 3
    // (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md)
    // - owns every atmosphere LUT/pass's ComputePipeline/descriptor set/
    // output texture across frames (see AtmosphereLutRenderer.h). Declared
    // right after m_renderGraph/before m_editorLayer for the same reason
    // m_renderGraph itself is: Run()'s offscreen-regime build lambda (see
    // src/Application/AtmospherePassSequence.h/Application.cpp) calls into
    // it every frame, so it must already be alive by the time that lambda
    // can possibly run, and must outlive it (destroyed only once the
    // Vulkan device it was built against is done being used). This is its
    // PERMANENT home as of Phase 7 (ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
    // - see that phase's own completion report.
    AtmosphereLutRenderer m_atmosphereLutRenderer;

    // network-impl-6 campaign, Phase 4
    // (task_manager/network-impl-6/PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md)
    // - the GET /get_texture volume-texture raymarch preview renderer (see
    // VolumeTexturePreviewRenderer.h). A genuinely lazy/on-demand class
    // (EnsureInitialized() does nothing until the first real RenderPreview()
    // call), so adding it unconditionally as a plain member here costs
    // nothing at startup - exactly like m_atmosphereLutRenderer above costs
    // nothing until its own first real pass runs. Declared right after it
    // for the same "constructed once, reused every request" ownership shape.
    VolumeTexturePreviewRenderer m_volumeTexturePreviewRenderer;

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 8
    // (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
    // - the small set of tunable, non-spatial atmosphere knobs edited live
    // via the Editor's new "Atmosphere" panel (Panels/AtmospherePanel.h) -
    // see AtmosphereTypes.h's own AtmosphereSettings doc comment for why
    // this lives on Application (not EditorContext, not an ECS component).
    // Threaded into the per-frame atmosphere pass-building code in Run()
    // (ground-albedo tint into AtmosphereParametersGpu before the shared
    // LUT passes, sky exposure into MakeRecordSkyBackgroundCallback(),
    // aerial-perspective strength into AddAtmosphereCompositePass()).
    AtmosphereSettings m_atmosphereSettings;

    // Declared after Renderer (and before Game) so it is destroyed before
    // Renderer's Vulkan device/instance go away, but its lifetime doesn't
    // need to relate to Game's at all.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    // editor-core-separation-1 campaign, PHASE12 - REFERENCE, not a real
    // owned instance anymore: the real Game instance now physically lives
    // inside m_core (Core::GetGame()) - see m_renderer's own doc comment
    // above for the full "why a reference, not m_core.GetGame() at every
    // call site" reasoning (identical here).
    Game& m_game;

    // frame-debugger-1 campaign (task_manager/frame-debugger-1/
    // PHASE0_MASTER_STRATEGY.md) - the ONE EngineContext instance for the
    // whole process, advanced exactly once per Run() loop iteration
    // (m_engineContext.time.Advance(...)) and passed by const reference into
    // Game::Update(). See EngineContext.h's own doc comment for why this
    // stays deliberately minimal (just `time` for now).
    // editor-core-separation-1 campaign, PHASE12 - REFERENCE, not a real
    // owned instance anymore: the real EngineContext/Time instance now
    // physically lives inside m_core (Core::GetEngineContext()) - see
    // m_core's own doc comment below. Kept as a same-named reference member
    // (a documented, lower-risk deviation from PHASE12's own literal "go
    // through m_core.GetEngineContext()" call-site-rewrite instruction) so
    // every one of Run()'s/the Register*Provider() methods' own existing
    // `m_engineContext.` call sites (still living in THIS file until
    // PHASE13 physically relocates that code into Core itself) keeps
    // compiling and behaving byte-for-byte unchanged this phase - see
    // PHASE12_COMPLETION_REPORT.md for the full reasoning.
    EngineContext& m_engineContext;

    // network-impl-2 campaign (task_manager/network-impl-2/) - the ONE
    // sanctioned cross-thread bridge a Network route handler is allowed to
    // touch (see AGENTS.md, "Networking", and FrameCaptureBridge.h's own
    // header comment). Declared BEFORE m_networkServer (constructed first,
    // destroyed last relative to it) so its address can be handed into
    // m_networkServer's own constructor below.
    FrameCaptureBridge m_captureBridge;

    // network-impl-3 campaign (task_manager/network-impl-3/) - the SECOND
    // sanctioned cross-thread bridge a Network route handler is allowed to
    // touch, this one for ECS-MUTATING commands (instantiate_primitive/
    // delete_entity - see AGENTS.md, "Networking", and
    // EngineCommandBridge.h's own header comment). Declared right after
    // m_captureBridge, for the exact same reason: BEFORE m_networkServer
    // (constructed first, destroyed last relative to it) so its address can
    // be handed into m_networkServer's own constructor below.
    EngineCommandBridge m_commandBridge;

    // network-impl-7 campaign - the THIRD sanctioned cross-thread bridge a
    // Network route handler is allowed to touch, this one for EDITOR-UI
    // commands (activate_tab - see AGENTS.md, "Networking", and
    // EditorUiCommandBridge.h's own header comment). Declared right after
    // m_commandBridge, for the exact same reason: BEFORE m_networkServer
    // (constructed first, destroyed last relative to it) so its address
    // can be handed into m_networkServer's own constructor below.
    EditorUiCommandBridge m_uiCommandBridge;

    // task_manager/frame-debugger-3 campaign, PHASE7
    // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - the
    // FOURTH sanctioned cross-thread bridge a Network route handler is
    // allowed to touch, this one for FRAME-DEBUGGER commands (open/enable/
    // capture/select_event/set_channel/set_levels/state - see
    // AGENTS.md, "Networking", and FrameDebuggerCommandBridge.h's own
    // header comment). Declared right after m_uiCommandBridge, for the
    // exact same reason: BEFORE m_networkServer (constructed first,
    // destroyed last relative to it) so its address can be handed into
    // m_networkServer's own constructor below.
    FrameDebuggerCommandBridge m_frameDebuggerCommandBridge;

    // task_manager/stl-parser-2 campaign, PHASE1 - the FIFTH sanctioned
    // cross-thread bridge a Network route handler is allowed to touch, this
    // one for the future POST /import_asset route (PHASE2) - see AGENTS.md,
    // "Networking", and AssetImportCommandBridge.h's own header comment.
    // Declared right after m_frameDebuggerCommandBridge, for the exact same
    // reason: BEFORE m_networkServer (constructed first, destroyed last
    // relative to it) so its address can be handed into m_networkServer's
    // own constructor below.
    AssetImportCommandBridge m_assetImportCommandBridge;

    // Networking campaign (task_manager/network-impl-1/) - an embedded,
    // loopback-only HTTP server (see AGENTS.md, "Networking"). Declared
    // LAST (after Game) so it is DESTROYED FIRST, before Game/the Editor/
    // Renderer/Window/SDL start tearing down - a defensive ordering choice,
    // not a strictly necessary one today (no route handler touches any
    // engine state at all yet - see NetworkRoutes.h), but it's what a
    // FUTURE endpoint that DOES need to bridge into engine state would
    // already want: the background thread is guaranteed fully stopped
    // before anything it might eventually reference starts being torn down.
    Network::NetworkServer m_networkServer;

    // Current OS window size, kept up to date by the same WindowResized
    // event Renderer::OnResize()/m_editorLayer->OnWindowResized() react to
    // (Window's own Width()/Height() only ever reflect its CONSTRUCTION
    // size, never a later resize) - used only as the aspect ratio for the
    // release-build ("no Editor, straight to the swapchain") rendering path
    // in Run(); the Editor build's Game/Scene views each use their own
    // RenderTexture's aspect ratio instead (see
    // IEditorLayer::GameViewTarget()/SceneViewTarget()).
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    // editor-core-separation-1 campaign, PHASE5
    // (PHASE5_EDITOR_CAPABILITY_INTERFACES_DESIGN.md) - the ONE Bucket B
    // nullable capability pointer this phase declares (Core/EditorCapabilities.h),
    // wired via SetSceneIOCapability() above. nullptr until PHASE6 constructs
    // a real EditorSceneIOCapability and registers it - PHASE5 itself never
    // reads or writes this field beyond its own default-member-initializer,
    // by design ("Files Touched"/"Out of Scope", PHASE5's own strategy file:
    // no real call site conversion yet).
    ISceneIOCapability* m_sceneIOCapability = nullptr;
};

} // namespace gte
