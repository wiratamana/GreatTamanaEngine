#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "../Core/EngineContext.h"
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
    Renderer m_renderer;
    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
    // the ONE shared RenderGraph instance Game view/Scene view/Present are
    // all recorded through (two Execute() calls per frame - see Run() and
    // RenderPasses.h). Declared right after m_renderer (constructed with a
    // reference to it) so it's already fully constructed by the time
    // m_editorLayer/m_game below might indirectly need it.
    rg::RenderGraph m_renderGraph;

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
    Game m_game;

    // frame-debugger-1 campaign (task_manager/frame-debugger-1/
    // PHASE0_MASTER_STRATEGY.md) - the ONE EngineContext instance for the
    // whole process, advanced exactly once per Run() loop iteration
    // (m_engineContext.time.Advance(...)) and passed by const reference into
    // Game::Update(). See EngineContext.h's own doc comment for why this
    // stays deliberately minimal (just `time` for now).
    EngineContext m_engineContext;

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
};

} // namespace gte
