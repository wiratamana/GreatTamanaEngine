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
// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// #include "Plugins/PluginRenderOperationRegistry.h" removed, along with
// the m_pluginRenderOperationRegistry member it backed -
// PluginRenderOperationRegistry.h/.cpp are deleted outright this phase
// (ABI-only; the one piece RenderFeatureCompositor still needed from it,
// the RenderFeatureBlend.comp pipeline, was already pulled fully in-house
// by PHASE1).
#include "../Renderer/Renderer.h"
#include "../Renderer/SceneServicesDescriptorSet.h"
// Block 4 (task_manager/better-render-pass-6), PHASE7
// (PHASE7_CORE_WIRING_RENDEROPAQUE_PROVIDER.md) - Core owns ONE
// SceneServicesDescriptorSet for its entire lifetime (m_sceneServicesDescriptorSet
// below); its constructor takes a Renderer&.
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

// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// #include "Plugins/PluginHost.h" removed, along with the m_pluginHost
// member/LoadPlugins()/GetPluginHost() it backed - PluginHost.h/.cpp are
// deleted outright this phase (ABI-only; nothing has called
// Core::LoadPlugins() since PHASE2 removed EditorHost.cpp's own call site).

// editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
// mirrors Plugins/PluginHost.h's own "concrete, gte_core-owned mechanism
// class, included by name" precedent immediately above exactly.
#include "Plugins/ProjectAssemblyHost.h"

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE1.
#include "../Assets/AssetDatabase.h"

// editor-core-separation-23 campaign, PHASE3
// (PHASE3_CORE_REGISTER_PROJECT_RENDER_FEATURE_API.md) - Core::RegisterProjectRenderFeature()/
// UnregisterProjectRenderFeature() below need both of these directly, by
// value/signature - ProjectRenderFeatureCallback.h is PHASE1's own new,
// free-standing, zero-Core-dependency header; RenderFeatureDescriptor.h has
// zero dependencies beyond <cstdint>/<cstddef> (confirmed, PHASE0 Step 2).
#include "Plugins/ProjectRenderFeatureCallback.h"
// better-render-pass-5 effort, BLOCK 3, PHASE3 - Core::AddPreOpaquePass()/
// RemovePreOpaquePass() below need ProjectPreOpaqueCallback by value/
// signature - mirrors ProjectRenderFeatureCallback.h's own free-standing,
// zero-Core-dependency precedent immediately above.
#include "Plugins/ProjectPreOpaqueCallback.h"
// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// relocated from "../../plugins/gte_plugin_abi/RenderFeatureDescriptor.h" into
// gte_core's own tree (Landmine A-style relocation, missed by the original
// campaign audit) - GtePluginRenderFeatureDescriptor is genuinely, permanently
// needed here, not ABI-only (see that header's own top-of-file comment).
#include "Plugins/RenderFeatureDescriptor.h"
// better-render-pass-3 campaign, BLOCK 2 (Arbitrary Render Views) - thin
// pass-throughs below (CreateRenderView()/FindRenderViewTarget()) need the
// real RenderViewRegistry class, held by value as a new m_renderViewRegistry
// member (see that member's own doc comment below).
#include "Plugins/RenderViewRegistry.h"

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

// editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
// mirrors IEditorLayer's own forward-declaration-only precedent immediately
// above: Core only ever holds a bare, nullable EditorHost* PARAMETER passed
// through LoadProjectAssemblies() (never a member), so no #include of the
// real src/Editor/EditorHost.h header is needed here at all.
class EditorHost;

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
    // Block 4 (task_manager/better-render-pass-6), PHASE8
    // (PHASE8_EXAMPLE_SHADER_AND_FULL_VERIFICATION.md) - lets a caller
    // building its own Pipeline against `set = 1` (e.g. this phase's own
    // proof-of-contract MeshWithShadow.frag Pipeline, or a future PreOpaque
    // feature) reach the ONE real owning instance's Layout() without ever
    // constructing a second, duplicate SceneServicesDescriptorSet.
    SceneServicesDescriptorSet& GetSceneServicesDescriptorSet() noexcept { return m_sceneServicesDescriptorSet; }
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

    // editor-core-separation-22 campaign, PHASE6
    // (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.3 item 2) - the
    // SAME `rg::RenderPassBlackboard` BuildFrame()'s own offscreen Execute()
    // callback declares every provider against this frame, now exposed
    // read-only so Application/EditorHost's own IEditorLayer::BuildUI() call
    // (and, through it, Panels/FrameDebuggerPanel.cpp's TriggerCapture(), the
    // new Clause C "disabled side effect still visible" detector) can ask
    // "was key X published THIS frame" via RenderPassBlackboard::
    // WasPublishedThisFrame() - see m_offscreenBlackboardThisFrame's own
    // member comment below for why this is safe to read any time after
    // BuildFrame() has returned (mirrors GetGpuDrivenBatchDebugInfo()'s own
    // "populated fresh every frame, safe to read afterward" contract
    // immediately above).
    const rg::RenderPassBlackboard& GetOffscreenBlackboardForFrameDebugger() const noexcept
    {
        return m_offscreenBlackboardThisFrame;
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

    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // LoadPlugins() removed outright - PluginHost (and the one call site
    // that ever invoked this method, EditorHost.cpp's own, removed by
    // PHASE2) are both gone; this mechanism is no longer reachable from
    // anywhere.

    // editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
    // thin pass-through, mirroring LoadPlugins() immediately above exactly.
    // `editorHost` is nullptr for a Player-shaped host that never
    // constructs one (a "*_Editor.dll" found in that case is skipped with a
    // loud, logged warning by ProjectAssemblyHost itself, never crashed on).
    // EditorHost's constructor (gte_editor) calls this exactly once, gated
    // behind `#if GTE_ENABLE_PROJECT_ASSEMBLIES` at THAT call site - its OWN,
    // separate flag, never GTE_ENABLE_PLUGINS (PHASE0_MASTER_STRATEGY.md,
    // Finding D) - this pass-through method itself always compiles.
    void LoadProjectAssemblies(const std::filesystem::path& outputDirectory, EditorHost* editorHost);

    // editor-core-separation-11 campaign (Project Assembly system), PHASE8
    // (Finding B). Core::RegisterOffscreenRenderPipelineProviders() itself
    // stays PRIVATE and unmodified - this is a NEW, separate, public thin
    // pass-through, mirroring LoadPlugins()'s own identical shape (a private
    // m_pluginHost member, a public one-line forwarding method). A Project
    // Assembly _Game.dll calls this directly, from its own GTE_RegisterProject
    // entry point, to contribute a real render-graph provider using the exact
    // same rg::RenderPipeline::Register() every internal engine pass already
    // goes through - no ABI wrapper, no curated operation registry (this
    // system has no ABI boundary to protect, unlike gte_plugin_abi's
    // IPluginRenderPassBuilder_v3). Forwards onto m_offscreenRenderPipeline
    // (confirmed correct target - see Core.cpp for the reasoning: this is the
    // pipeline every production Game-View/Scene-View pass registers onto;
    // m_presentRenderPipeline is the separate, narrower pipeline used ONLY
    // for the one "Present" swapchain-blit provider).
    void RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider);

    // editor-core-separation-13 campaign, PHASE3 - the teardown counterpart of
    // RegisterProjectRenderPassProvider() (immediately above), called ONLY by
    // ProjectAssemblyRegistrationLedger::UnregisterEverythingFor() - never by
    // any Project Assembly's own authored code directly.
    void UnregisterProjectRenderPassProvider(const char* debugName);

    // editor-core-separation-23 campaign, PHASE3
    // (PHASE3_CORE_REGISTER_PROJECT_RENDER_FEATURE_API.md) - a thin
    // pass-through into m_renderFeatureCompositorPtr's own
    // RegisterProjectFeature()/UnregisterProjectFeature() (PHASE2) - NOT
    // m_offscreenRenderPipeline (the target RegisterProjectRenderPassProvider()
    // above forwards to). RenderFeatureCompositor is a separate object, owned
    // by one of Core's m_capabilityOrchestrators entries, resolved through
    // the exact same m_renderFeatureCompositorPtr GetRenderFeatureCompositor()
    // already returns - never a second, freshly-constructed instance. Returns
    // false (logged, never crashes) if no RenderFeatureCompositor orchestrator
    // exists in this build, if debugName is null, if debugName's length
    // exceeds 63 bytes (GtePluginRenderFeatureDescriptor::name is a fixed
    // char[64] - this is the FIRST call site in this engine building that
    // string from free-form, un-length-checked input, so it REJECTS outright
    // rather than silently truncating - see Core.cpp for the full reasoning),
    // or if RenderFeatureCompositor::RegisterProjectFeature() itself refuses
    // (duplicate name, unwired stage, slot pool exhausted).
    bool RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
        RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback);

    // editor-core-separation-23 campaign, PHASE3 - the teardown counterpart
    // of RegisterProjectRenderFeature() immediately above, mirroring
    // UnregisterProjectRenderPassProvider()'s own shape. Null-safe; a safe
    // no-op if debugName was never successfully registered (an over-length
    // name could never have registered in the first place, so this simply
    // reports "not found," the same harmless outcome as any other unknown
    // name - no separate length check needed here).
    void UnregisterProjectRenderFeature(const char* debugName);

    // better-render-pass-5 effort, BLOCK 3, PHASE3 - a thin
    // pass-through into m_renderFeatureCompositorPtr's own
    // RegisterPreOpaqueFeature() (PHASE2) - mirrors
    // RegisterProjectRenderFeature()'s own shape exactly, including the
    // SAME null/length-check discipline, but reaches the PreOpaque-
    // specific entry point, never RegisterProjectFeature()/Entry (see
    // task_manager/better-render-pass-5/PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #1 for why these are deliberately separate
    // front doors). `priority` defaults to 0 - PreOpaque features
    // realistically number in the single digits per project (shadow
    // maps, GI passes), so an explicit default avoids forcing every
    // trivial caller to think about ordering relative to features it
    // does not know about yet; a caller with a genuine ordering need
    // against another PreOpaque feature still passes an explicit value.
    // Returns false (logged, never crashes) if no
    // RenderFeatureCompositor orchestrator exists in this build, if
    // debugName is null, if debugName's length exceeds 63 bytes, or if
    // RegisterPreOpaqueFeature() itself refuses (duplicate name).
    bool AddPreOpaquePass(const char* debugName, ProjectPreOpaqueCallback callback, std::int32_t priority = 0);

    // Teardown counterpart of AddPreOpaquePass() immediately above,
    // mirroring UnregisterProjectRenderFeature()'s own shape. Null-safe;
    // a safe no-op if debugName was never successfully registered.
    void RemovePreOpaquePass(const char* debugName);

    // better-render-pass-3 campaign, BLOCK 2 (Arbitrary Render Views) -
    // thin pass-throughs into m_renderViewRegistry (below). Mints (or
    // returns the already-existing) persistent, named render view -
    // mirrors RegisterProjectRenderPassProvider()'s own "thin wrapper +
    // orchestrator member" shape exactly, but is NOT itself a pass
    // registration: it only hands back an identity + a target. The pass
    // that WRITES into the returned target must still be registered
    // separately, via RegisterProjectRenderPassProvider() above - NEVER
    // AddScreenPostProcessPass() below, whose callback signature carries
    // no RenderPassFrameContext and therefore cannot perform the
    // mandatory `frame.finalTextureOutputs.push_back(handle)` root-set
    // push a brand-new view's writer pass needs (see
    // RenderPipeline.h's own finalTextureOutputs doc comment, and
    // RenderGraphCompiler::Compile()'s backward-reachability culling -
    // skipping this push means the pass silently never executes, with
    // no crash, no log, nothing visibly wrong). `depthOnly` (default
    // false) translates to RenderViewDesc{ hasColor = !depthOnly,
    // hasDepth = true } - a depth-only view always keeps its depth half
    // and never allocates a color image (see RenderTexture's own
    // createColorImage constructor parameter). `name` has no lifetime
    // requirement of its own here - RenderViewRegistry copies it into
    // its own std::string key immediately - but the SEPARATE name string
    // later passed to RenderGraphBuilder::ImportTexture() every frame (to
    // import this view's target) DOES need static/stable storage
    // duration; see RenderViewRegistry.h's own doc comments for the full
    // reasoning.
    rg::RenderViewId CreateRenderView(const char* name, std::uint32_t width, std::uint32_t height,
        bool depthOnly = false);
    RenderTexture* FindRenderViewTarget(rg::RenderViewId view) const noexcept;

    // better-render-pass-1 campaign, PHASE9 (Decision D3) - additive
    // convenience wrapper over RegisterProjectRenderFeature() immediately
    // above: fixes stage to RenderFeatureStage::PostComposite (the one,
    // real "draw over the final composited screen" hook point this concept
    // means), and auto-assigns a collision-tolerant priority at RUNTIME (a
    // simple, monotonically-incrementing counter - see Core.cpp) when the
    // caller does not supply one explicitly. Cuts the common case down to
    // ONE call: core.AddScreenPostProcessPass("Name",
    // [](rg::RenderGraphBuilder& builder, rg::TextureHandle target,
    // VkExtent2D extent) { ... }); - no stage/priority argument required at
    // all. RegisterProjectRenderFeature() itself is UNCHANGED and remains
    // available for any caller needing explicit stage/blend/priority
    // control (e.g. RenderFeatureStage::PreUI, or a specific hand-chosen
    // priority for managed cross-plugin compositing order).
    bool AddScreenPostProcessPass(const char* debugName, ProjectRenderFeatureCallback callback,
        RenderFeatureBlendMode blendMode = RenderFeatureBlendMode::AlphaOver,
        std::optional<std::int32_t> priority = std::nullopt);

    // editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 2), PHASE4 - mirrors GetRenderer()'s own existing precedent
    // exactly. Needed so EditorHotReloadDebugCapability (a gte_editor-tier
    // class, constructed as a namespace-scope static BEFORE any Core exists
    // - see that class's own SetProjectAssemblyHost() doc comment for the
    // full reasoning) can be handed a live ProjectAssemblyHost& once, from
    // EditorHost's own constructor BODY, strictly AFTER m_core already
    // exists.
    ProjectAssemblyHost& GetProjectAssemblyHost() noexcept { return m_projectAssemblyHost; }

    // editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 4), PHASE1 - a persistent, engine-owned AssetDatabase,
    // refreshed exactly ONCE per hot-reload cycle by
    // CaptureProjectAssemblyHotReloadState() (PHASE2), then reused AS-IS by
    // RestoreProjectAssemblyHotReloadState() (PHASE3) later in the SAME
    // cycle - mirrors Unity's own persistent Assets-folder database concept
    // (kept live, not rescanned from scratch on every single operation).
    // Deliberately a SEPARATE instance from Editor/Panels/ProjectPanel.h's
    // own separately-owned, separately-refreshed AssetDatabase (used by the
    // Project Browser panel), and separate again from the throwaway one-shot
    // instances Editor/SceneIO.cpp's SaveScene()/LoadScene() already build
    // fresh on every call - unifying all three is real, legitimate, OUT OF
    // SCOPE future work (PHASE0_MASTER_STRATEGY.md, LDD-HR7) - do not attempt
    // it as part of this phase.
    AssetDatabase& GetAssetDatabase() noexcept { return m_assetDatabase; }

    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // GetPluginHost() removed outright, along with PluginHost itself
    // (ABI-only) - its one real caller, LegacyRenderFeatureOrchestrator, is
    // deleted this same phase.

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
    // editor-core-separation-8 campaign, PHASE2 - return type widened from
    // `const RenderFeatureCompositor*` to `RenderFeatureCompositor*` (this
    // method itself stays const - it does not mutate Core; only what it
    // returns a pointer TO becomes mutable, so a caller can now reach the new
    // SetFeatureEnabled()/SetFeaturePriority() mutators). Safe/backward-
    // compatible: m_renderFeatureCompositorPtr was already a non-const
    // pointer internally, and every existing call site only ever assigns the
    // result into a const-pointer-typed local or calls const-qualified
    // methods on it (confirmed via search_in_dir - EditorHost.cpp is the
    // only real call site).
    RenderFeatureCompositor* GetRenderFeatureCompositor() const noexcept
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
        // editor-core-separation-22 campaign, PHASE3
        // (PHASE3_FIX_AUDIT_FINDINGS_SIDE_CHANNEL_LEAKS.md) - fixes
        // PHASE2_COMPLETION_REPORT.md's own finding #19: every original
        // DrawCommand::entity this batch replaces, carried alongside the
        // batch's own render data (instead of being inserted into
        // m_gpuDrivenBatchedEntitiesThisFrame unconditionally at COLLECTION
        // time, before this batch's own "<batch> IndirectDraw" pass toggle
        // state is even known) - see GpuDrivenBatchEntityExclusionLogic.h and
        // the "GpuDrivenBatches" provider's own body in Core.cpp for the real
        // fix: this list is only folded into the exclusion set once that
        // SPECIFIC pass is confirmed to have survived its own toggle check
        // this frame.
        std::vector<Entity> entities;
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

    // Block 4 (task_manager/better-render-pass-6), PHASE8
    // (PHASE8_EXAMPLE_SHADER_AND_FULL_VERIFICATION.md) - the one concrete,
    // permanent proof that `layout(set = 1, binding = 0)` works end-to-end
    // through the real Renderer::Submit()/"RenderOpaque" path (PHASE3-7):
    // builds a real Mesh + a real Pipeline from the new MeshWithShadow.frag
    // shader (passing GetSceneServicesDescriptorSet().Layout() as
    // Renderer::CreatePipeline()'s PHASE4 trailing sceneServicesSetLayout
    // parameter) and spawns ONE entity using it, directly through the
    // ordinary MeshRenderer::pipeline field - never RenderSystem::Draw()'s
    // separate pipelineOverride mechanism, which would force EVERY entity in
    // a call to share this one Pipeline instead of just this one proof
    // entity. Called exactly once, from the constructor, mirroring
    // GpuDrivenBatchTestSpawner.cpp's own "hand-authored indexed unit quad +
    // matching Pipeline, built once" shape (render-pass-5 campaign, PHASE6) -
    // see Core.cpp for the full reasoning and implementation.
    void SpawnSceneServicesExampleEntity();

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
    // Block 4 (task_manager/better-render-pass-6) - the ONE reserved "scene
    // services" descriptor set (set = 1) every Pipeline drawn through
    // Renderer::Submit() during the Editor's offscreen "RenderOpaque" pass
    // can optionally sample. Constructed AFTER m_renderer (declaration order
    // matters here) since its constructor takes a Renderer&. Never torn down
    // mid-process - lives for Core's entire lifetime, mirroring
    // RenderViewRegistry's own convention.
    SceneServicesDescriptorSet m_sceneServicesDescriptorSet;
    // better-render-pass-3 campaign, BLOCK 2 (Arbitrary Render Views) -
    // needs a live Renderer&, so it is declared (and initialized)
    // immediately after m_renderer, mirroring m_renderGraph's own identical
    // ordering requirement right below it.
    RenderViewRegistry m_renderViewRegistry;
    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // `PluginRenderOperationRegistry m_pluginRenderOperationRegistry` deleted
    // outright, along with the type itself (ABI-only; the one piece
    // RenderFeatureCompositor still needed from it was already pulled fully
    // in-house by PHASE1).
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

    // editor-core-separation-22 campaign, PHASE6
    // (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.3 item 2) -
    // PROMOTED from a lambda-local variable (BuildFrame()'s own offscreen
    // Execute() callback used to declare `rg::RenderPassBlackboard
    // blackboard;` as a plain stack local, destroyed the instant that
    // callback returned) to a real Core member, so it survives long enough
    // for GetOffscreenBlackboardForFrameDebugger() (above) to read it back
    // AFTER BuildFrame() has returned this same frame - the ONLY behavior
    // change this promotion causes is that the object's OWN LIFETIME is
    // longer; `.BeginFrame()` is still called exactly once per offscreen
    // Execute() call, at the exact same call site, so its CONTENTS are
    // still cleared and rebuilt fresh every single frame, byte-identical to
    // the old local-variable behavior. Game-View/Scene-View-shared, exactly
    // like the local it replaces (this is the OFFSCREEN regime's own
    // blackboard only - the separate PIPELINED "presentBlackboard" local a
    // few hundred lines later in Core.cpp is UNRELATED and untouched by
    // this phase, since nothing the Frame Debugger's Clause C detector
    // cares about is ever published there).
    rg::RenderPassBlackboard m_offscreenBlackboardThisFrame;

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

    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // `PluginHost m_pluginHost` (plus `LoadPlugins()`/`GetPluginHost()`,
    // removed above) deleted outright - `PluginHost` itself is deleted this
    // phase (ABI-only; nothing has invoked `Core::LoadPlugins()` since PHASE2
    // removed `EditorHost.cpp`'s own call site).

    // editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
    // a new, additive, parallel system, sibling to (but always separate from)
    // the ABI plugin system's own former PluginHost mechanism (deleted,
    // better-render-pass-2 campaign, PHASE3).
    // Mirrors m_pluginHost's own "no constructor dependency on any other
    // Core member" placement exactly.
    ProjectAssemblyHost m_projectAssemblyHost;

    // editor-core-separation-15 campaign, PHASE1 - see GetAssetDatabase()'s
    // own doc comment above. Default-constructed, empty, until the first hot
    // reload cycle calls RefreshFromDirectory() on it (PHASE2) - never
    // refreshed at engine startup by this phase, deliberately (nothing reads
    // it before PHASE2 exists).
    AssetDatabase m_assetDatabase;

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
