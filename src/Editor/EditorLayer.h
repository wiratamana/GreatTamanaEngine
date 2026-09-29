#pragma once

#include "../Math/Mat4.h"
#include "../Math/Vec3.h"
#include "../Renderer/Atmosphere/AtmosphereTypes.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/Culling/GpuDrivenBatchDebugInfo.h"
// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - a small, dependency-free
// gte_core-owned struct (see that header's own doc comment for why it lives
// there, co-located with RenderFeatureCompositor.h, instead of being defined
// inline here) - included directly, mirroring GpuDrivenBatchDebugInfo.h
// immediately above exactly.
#include "../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
// editor-core-separation-2 campaign, PHASE2 - the new gte_core-owned
// IFrameDebuggerCaptureRecorder interface (src/Core/
// FrameDebuggerCaptureRecorder.h) - a gte_core-owned, ImGui/SDL-free header,
// so EditorLayer.h (the one documented gte_core-visible exception file - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8) including it
// directly is completely safe and mirrors how this header already includes
// other gte_core headers it needs. MUST be included here, at file scope
// (NOT from inside `namespace gte { ... }` below) - this header opens its
// own `namespace gte { ... }` block, and including it from inside an
// already-open `namespace gte { ... }` here would create a bogus nested
// `gte::gte` namespace instead of extending the real `gte` namespace.
#include "../Core/FrameDebuggerCaptureRecorder.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward-declared so this header needs no SDL dependency at all (matches
// the Vulkan-handle forward-declare trick already used in Window.h) - only
// whichever concrete implementation's .cpp needs the full SDL_Event
// definition.
union SDL_Event;

namespace gte {

class Window;
class Renderer;
class Game;
class AtmosphereLutRenderer;

// Editor-only type (src/Editor/FrameDebuggerCapture.h) - forward-declared
// ONLY (never #included here), since EditorLayer.h is a CORE, always-
// compiled file (see task_manager/frame-debugger-3/
// PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md's own Step 3.1b, applied here
// exactly like src/Game/RenderSystem.h already does) that must still
// compile in a build where NullEditorLayer.cpp (rather than
// ImGuiEditorLayer.cpp) is what got linked in - a build where this type does
// NOT exist at all in that binary. A bare forward declaration of a pointee is always legal
// even when the type is never defined in this translation unit, since
// PrepareFrameDebuggerCaptureContext() below only ever needs a POINTER to
// it.
//
// editor-core-separation-2 campaign, PHASE2 - FrameDebuggerCaptureContext is
// still an Editor-only type, but PrepareFrameDebuggerCaptureContext() below
// now returns a POINTER TO THE ABSTRACT INTERFACE it implements,
// IFrameDebuggerCaptureRecorder (#included above, at file scope) - this
// closes the exact undefined-reference hazard that a bare
// `class FrameDebuggerCaptureContext;` forward declaration alone could never
// cause here (this header never dereferences the pointer), but which every
// DOWNSTREAM gte_core-tier consumer of this return value (Core.cpp) used to
// hit once it called a gte_editor-only free function on it by name.

namespace rg {
class RenderGraph;
class RenderGraphBuilder;
class RenderPassToggleRegistry; // editor-core-separation-8 campaign, PHASE1/PHASE3.
} // namespace rg

// editor-core-separation-8 campaign, PHASE2/PHASE3 - forward-declared only,
// mirrors "class Core;"'s own forward-declare-only precedent elsewhere in
// this codebase - the REAL header (src/Core/Plugins/RenderFeatureCompositor.h)
// is heavy (pulls in ComputeDescriptorSet.h/ComputePipeline.h/RenderTexture.h/
// volk.h) and is only ever #included by the .cpp files that actually CALL a
// method on this pointer (ImGuiEditorLayer.cpp, RenderGraphPanel.cpp) - this
// header itself only ever passes the pointer through, never dereferences it.
class RenderFeatureCompositor;

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE4 - forward-declared only, mirrors
// "class RenderFeatureCompositor;" immediately above: this header only
// ever stores/passes a POINTER to it, never dereferences one itself.
class IProjectLifecycleCapability;

// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4), PHASE3 - forward-declared only, mirrors
// "class IProjectLifecycleCapability;" immediately above: this header only
// ever stores/passes a POINTER to it, never dereferences one itself.
class IAssetScaffoldingCapability;

// editor-core-separation-19 campaign (On-Engine Project Workflow plan,
// BIG-STEP 5), PHASE1 - forward-declared only, mirrors
// "class IAssetScaffoldingCapability;" immediately above: this header only
// ever stores/passes a POINTER to it, never dereferences one itself.
class IHotReloadDebugCapability;

// Abstraction boundary between engine-core (Application/Renderer/Game) and
// the optional Editor/Debug UI. Dear ImGui-backed in real builds, but
// nothing outside src/Editor/ (specifically: nothing outside whichever
// files there only ever compile as part of the Editor's own build - see
// AGENTS.md, "Editor Module Structure") ever includes an ImGui header -
// Application only ever talks to this interface.
//
// Game never depends on this at all, in either direction: Game has no idea
// the Editor exists, and the Editor only ever *observes*/edits Game/Renderer
// through their existing public accessors (never the other way around) -
// e.g. Game::GetRegistry() is what lets the Hierarchy/Inspector panels below
// see and edit the ECS world without Game gaining any Editor awareness.
// That is what makes linking a Player host against NullEditorLayer.cpp
// alone, without ever seeing ImGuiEditorLayer.cpp's own source, a genuinely
// zero-touch operation for gameplay code.
//
// Two implementations exist, selected entirely by which .cpp got compiled
// (see CMakeLists.txt) - never both at once, so there's no #ifdef soup
// anywhere that calls into this interface:
//   - ImGuiEditorLayer (src/Editor/ImGuiEditorLayer.cpp) - the real thing:
//     owns the ImGui context (docking branch), the SDL3 + Vulkan backends,
//     TWO RenderTextures Game's camera renders into - one for the "Game"
//     panel, one for the "Scene" panel, each tracking that panel's own
//     content-region size/aspect independently (see GameViewTarget()/
//     SceneViewTarget() below) - and the Unity-style docked layout
//     (Hierarchy left, Inspector right, Scene/Game tabbed center, top menu
//     bar). What this repo's own executable always links.
//   - NullEditorLayer (src/Editor/NullEditorLayer.cpp) - every method is a
//     no-op and GameViewTarget()/SceneViewTarget() always return nullptr,
//     meaning "render straight to the swapchain" - i.e. a release build
//     behaves exactly as if no Editor/ImGui ever existed. What a future
//     Player host (linking gte_core alone, never seeing gte_editor's
//     source) links instead, with zero ImGui code or linkage anywhere in
//     that binary.
// Result of ActivateTab() below - deliberately a SEPARATE, tiny,
// dependency-free type from Application/EditorUiCommandBridge.h's own
// ActivateTabOutcome (network-impl-7 campaign) - EditorLayer.h must never
// depend on anything under src/Application/ (Application depends on
// Editor, never the reverse - see this file's own class comment). Only
// Application::Run() (Phase 3) ever converts one of these into the OTHER
// type, one field at a time, at the one call site that legitimately
// depends on both.
struct TabActivationResult {
    bool tabExists = false;
};

// task_manager/stl-parser-2, PHASE1 - result of ImportExternalAssetIntoProject()
// below. Deliberately a SEPARATE, tiny, dependency-free type from
// src/Application/AssetImportCommandBridge.h's own ImportExternalFileOutcome
// (network-impl-7's own TabActivationResult/ActivateTabOutcome split
// precedent, applied here) - EditorLayer.h must never depend on anything
// under src/Application/. Application::Run() (Phase 3.7 below) is the one
// place that converts one of these into the OTHER type, one field at a
// time.
struct ProjectAssetImportResult {
    bool projectAvailable = true;
    bool success = false;
    std::string message;
    std::string finalRelativePath;
    std::string finalAbsolutePath;
    std::string guid;
    bool convertedToMeshAsset = false;
    std::string meshSourceFormat;
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;
    std::uint64_t meshVertexCount = 0;
    std::uint64_t meshTriangleCount = 0;
};

// task_manager/mrt-1 campaign (Multi-Render-Target / G-Buffer support),
// PHASE4 (PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md) - result of
// AddGBufferValidationPass() below. Deliberately a SEPARATE, tiny,
// dependency-free struct (mirrors TabActivationResult's own precedent
// above) - EditorLayer.h must never depend on src/Editor/GBufferValidation.h
// (an Editor-only file a Player host linking gte_core alone never even sees).
// The CALLER (Application::Run()) must add every one of these three
// handles to that call's own finalOutputs root set, or PHASE1-3's
// existing RenderGraphCompiler culling would silently drop whichever one
// never reaches a root.
struct GBufferValidationHandles {
    rg::TextureHandle albedo{};
    rg::TextureHandle normal{};
    rg::TextureHandle visualized{};
};

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - the Frame
// Debugger's own read-only state snapshot, mirroring TabActivationResult's
// own "tiny, dependency-free, Editor-owned result type" precedent
// immediately above - Application::Run() is the one place that copies this
// into src/Application/FrameDebuggerCommandBridge.h's own, separate
// FrameDebuggerStateOutcome, one field at a time (this header must never
// depend on anything under src/Application/, same rule TabActivationResult
// already follows).
struct FrameDebuggerStateSnapshotView {
    bool enabled = false;
    bool windowOpen = false;
    // task_manager/frame-debugger-7 campaign, PHASE1
    // (PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md) - REPLACES
    // `historyCount`/`historyCursor` (the old 8-slot ring buffer's own
    // "how many"/"which one" fields) - there is only ever ONE captured
    // frame now, so the only meaningful question is "is there one".
    bool hasCapturedFrame = false;
    int totalEventCount = 0;
    int selectedEventIndex = -1;
    std::string channel = "all";
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;
};

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
// - GpuDrivenBatchDebugInfo itself now lives in
// src/Renderer/Culling/GpuDrivenBatchDebugInfo.h (editor-core-separation-1
// campaign, PHASE13) - see that header's own doc comment for why (a plain,
// dependency-free data type gte_core's own Core::BuildFrame() produces every
// frame, only ever CONSUMED here via IEditorLayer::BuildUI()'s own trailing
// parameter below).

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 - result of SpawnGpuDrivenTestBatch() below. Deliberately a plain,
// dependency-free struct (mirrors ProjectAssetImportResult's own
// "editorAvailable"-style convention immediately above) - EditorLayer.h must
// never depend on src/Editor/GpuDrivenBatchTestSpawner.h (an Editor-only
// file's own real implementation), and must never depend on
// src/Application/EditorUiCommandBridge.h's own separate outcome type
// either (Application::Run() is the one place that converts one of these
// into the other, one field at a time - see every other *Outcome/*Result
// pair in this file for the identical precedent).
struct GpuDrivenTestBatchSpawnResult {
    bool success = false;
    // editor-core-separation-1 campaign, PHASE8 - a real, dedicated field
    // (mirrors ProjectAssetImportResult's own "editorAvailable"-style
    // convention immediately above) so a caller never needs to sniff
    // `errorMessage`'s own TEXT for a literal substring to distinguish
    // "structurally unavailable in this build" from "a caller mistake".
    bool editorAvailable = true;
    std::string errorMessage;
    std::uint32_t instanceCount = 0;
};

class IEditorLayer {
public:
    virtual ~IEditorLayer() = default;

    // Feeds one raw SDL event to the editor's own input handling (real
    // impl only - Null impl no-ops). Application is the only layer that
    // already touches SDL_Event, so it is the one that calls this - same
    // boundary EventTranslator sits on.
    virtual void ProcessEvent(const SDL_Event& event) = 0;

    // Called when the OS window is resized (Application forwards this
    // straight from the same WindowResized event Renderer::OnResize()
    // reacts to). The real implementation no-ops: the Game-view/Scene-view
    // RenderTextures track their own ImGui panel's content-region size
    // instead of the OS window's size (Unity-style "Free Aspect" - see
    // GameViewTarget()/SceneViewTarget()/BuildUI() in ImGuiEditorLayer), so
    // an OS window resize by itself is not a reason to resize either of
    // them. Kept in the interface only in case a future implementation
    // needs it - Null impl no-ops too.
    virtual void OnWindowResized(int width, int height) = 0;

    // Starts a new UI frame. Call once per frame, before Game's
    // Update()/Render().
    virtual void NewFrame() = 0;

    // The render target Game's gameplay camera should draw into THIS
    // frame for the "Game" panel: a RenderTexture, or nullptr meaning
    // "don't bother rendering a Game view this frame" - either because
    // there's no Editor at all (render straight to the swapchain,
    // fullscreen instead - what the Null implementation always returns), or
    // because the real implementation's "Game" panel is not currently
    // visible (e.g. it's an inactive tab behind "Scene" - see
    // Panels/GamePanel.cpp/EditorContext::gameViewVisible) - skipping a
    // RenderOffscreen() pass nobody would ever see. The real implementation
    // also resizes this texture here (if the "Game" panel's content-region
    // size changed since last frame's BuildUI()) before returning it - the
    // last safe/needed point to do so, since Game is about to render into
    // it. See ImGuiEditorLayer's class comment.
    virtual RenderTexture* GameViewTarget() = 0;

    // The Scene-view equivalent of GameViewTarget() above - its own,
    // separate RenderTexture (never the same one as the Game view - each
    // panel can be a different size/aspect, e.g. split side-by-side), or
    // nullptr under the exact same two circumstances: no Editor at all, or
    // the real implementation's "Scene" panel isn't currently visible (see
    // Panels/ScenePanel.cpp/EditorContext::sceneViewVisible). Application
    // calls this and GameViewTarget() independently each frame and renders
    // into whichever one(s) come back non-null - if "Scene" and "Game" are
    // tabbed together, exactly one is ever visible at a time (so only that
    // one gets rendered); if the user has split them apart, BOTH are
    // visible and BOTH get rendered.
    virtual RenderTexture* SceneViewTarget() = 0;

    // The combined projection * view matrix "Scene" should be rendered
    // with THIS frame, for a render target of the given aspect ratio -
    // Application passes this straight to Game::Render()'s
    // viewProjectionOverride parameter for the Scene view specifically,
    // in place of whatever ECS entity currently has the active Camera
    // component (which is what the Game view still uses - see
    // RenderSystem::ResolveActiveCameraViewProjection()). Backed by the
    // real implementation's own independently-orbitable EditorCamera (see
    // EditorCamera.h and Panels/ScenePanel.cpp for its Unity-style pan/
    // rotate/dolly mouse controls) - always Mat4::Identity() for
    // NullEditorLayer, though it is never actually consulted there in
    // practice, since SceneViewTarget() above always returns nullptr for
    // it (so Application never renders a Scene view at all in a release
    // build).
    virtual Mat4 SceneViewProjection(float aspectWidthOverHeight) const = 0;

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 7 - the
    // Scene view's own EditorCamera world-space eye position (its
    // Transform's position - see EditorCamera::GetTransform()), needed to
    // resolve the Scene View's own AtmosphereFrameUniforms (camera height
    // above the virtual planet's ground) the same way
    // RenderSystem::ResolveActiveCameraViewProjection()'s ECS Camera
    // equivalent already is for the Game View. Always Vec3::Zero() for
    // NullEditorLayer (never actually consulted there in practice, for the
    // same reason SceneViewProjection() above never is either).
    virtual Vec3 SceneViewCameraWorldPosition() const = 0;


    // Atmosphere Scattering + Aerial Perspective campaign, Phase 7
    // (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
    // - hands this implementation a stable RenderTexture* to display in the
    // "Game" panel INSTEAD of GameViewTarget()'s own m_gameView, now that
    // the atmosphere-composited output (a NEW, separate texture -
    // "GameViewComposited" - written by a post-process pass AFTER
    // Game::Render() finishes) is PERMANENTLY what gets displayed - never
    // optional/debug-only, unlike the existing "Show Compute Blur (debug)"
    // toggle. Called once per frame, AFTER Application::Run() has finished
    // recording this frame's atmosphere composite pass (and finalized it
    // for external sampling) - `texture` is nullptr on any frame the Game
    // view wasn't actually rendered at all (mirrors GameViewTarget()'s own
    // nullptr contract). A real implementation is expected to fall back to
    // its own original m_gameView whenever this is nullptr (e.g. the very
    // first frame, before anything has run yet) rather than showing nothing.
    // A no-op for NullEditorLayer (a release build has no "Game" panel to
    // update).
    virtual void SetGameViewCompositedTexture(RenderTexture* texture) = 0;

    // The Scene-view equivalent of SetGameViewCompositedTexture() above, for
    // "SceneViewComposited" / the "Scene" panel - see that method's own doc
    // comment for the full reasoning.
    virtual void SetSceneViewCompositedTexture(RenderTexture* texture) = 0;


    // Phase 7 (COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md) -
    // declares (if this implementation's own "Show Compute Blur (debug)"
    // toggle is on AND the "Scene" panel was visible last frame) a compute
    // box-blur validation pass into `builder`: reads `sceneViewHandle`
    // (this call's own already-imported, just-rendered Scene view
    // texture) as a plain `Texture`, and writes this implementation's own
    // persistent, storage-capable `blurredOutput` RenderTexture (an
    // `RWTexture`, imported fresh every call) sized to `sceneExtent`.
    // Returns the blurred output's TextureHandle - which the CALLER must
    // add to this call's own finalOutputs root set, or the pass's write
    // will be silently culled - or std::nullopt if the pass was not
    // declared at all this frame (see ComputeBlurValidation.h for the
    // real implementation this wraps; always std::nullopt for
    // NullEditorLayer, which never declares a pass at all). `renderer` is
    // the same Renderer Application already owns.
    virtual std::optional<rg::TextureHandle> AddBlurValidationPass(
        rg::RenderGraphBuilder& builder, Renderer& renderer, rg::TextureHandle sceneViewHandle, VkExtent2D sceneExtent) = 0;

    // Transitions the blurred-output texture (if AddBlurValidationPass()
    // above actually declared a pass this frame - a safe no-op otherwise)
    // from its compute-write state to a real ShaderRead state, ready for
    // this implementation's own ImGui::Image() display - mirrors
    // RenderPasses.h's FinalizeRenderTextureForExternalSampling() for the
    // Game/Scene views. Must be called against the SAME command buffer
    // the offscreen RenderGraph::Execute() call just recorded into, AFTER
    // that call returns and BEFORE that command buffer is ended/submitted
    // - see Application::Run(). A no-op for NullEditorLayer.
    virtual void FinalizeBlurValidationForSampling(VkCommandBuffer cmd) = 0;

    // task_manager/mrt-1 campaign, PHASE4
    // (PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md) - declares (if this
    // implementation's own "Show GBuffer Validation (debug)" toggle is on
    // AND the "Scene" panel was visible last frame, AND `sceneExtent` is
    // non-degenerate) this campaign's own first real MRT consumer: a
    // GRAPHICS pass writing two color attachments (albedo/normal) in one
    // draw, plus a small compute pass reading one of them back into a
    // third, independently-inspectable output. Unlike AddBlurValidationPass()
    // above, this pass reads NO Scene View texture at all - its two color
    // outputs are entirely self-contained, procedural test content (see
    // src/Editor/GBufferValidation.h's own header comment), so this method
    // needs no `sceneViewHandle` parameter. Returns every real output
    // handle this call declared - the CALLER must add all three to this
    // call's own finalOutputs root set, or PHASE1-3's existing
    // RenderGraphCompiler culling would silently drop whichever one never
    // reaches a root - or std::nullopt if no pass was declared at all this
    // frame (see GBufferValidation.h for the real implementation this
    // wraps; always std::nullopt for NullEditorLayer). `renderer` is the
    // same Renderer Application already owns.
    virtual std::optional<GBufferValidationHandles> AddGBufferValidationPass(
        rg::RenderGraphBuilder& builder, Renderer& renderer, VkExtent2D sceneExtent) = 0;

    // Transitions all three GBuffer Validation outputs (if
    // AddGBufferValidationPass() above actually declared a pass this frame
    // - a safe no-op otherwise) to a real ShaderRead state, ready for this
    // implementation's own ImGui::Image() display / GET /get_texture
    // capture - mirrors FinalizeBlurValidationForSampling() above (the
    // compute-written visualized output) plus RenderPasses.h's own
    // FinalizeRenderTextureForExternalSampling() (the graphics-written
    // albedo/normal outputs). Must be called against the SAME command
    // buffer the offscreen RenderGraph::Execute() call just recorded into,
    // AFTER that call returns and BEFORE that command buffer is ended/
    // submitted - see Application::Run(). A no-op for NullEditorLayer.
    virtual void FinalizeGBufferValidationForSampling(VkCommandBuffer cmd) = 0;

    // Records the Editor's "Scene" panel infinite ground grid (see
    // task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) directly
    // against `cmd` - called by Application::Run() from INSIDE
    // AddSceneViewPass()'s own execute callback (RenderPasses.cpp), i.e.
    // still inside that pass's open vkCmdBeginRendering/vkCmdEndRendering
    // bracket, immediately AFTER Game::Render() has already recorded the real
    // scene geometry for this frame - this exact ordering is what makes the
    // grid correctly depth-tested/occluded by scene objects already in front
    // of it (see SceneGridRenderer::Draw()'s own doc comment).
    // `sceneViewProjection` is the exact same combined view-projection matrix
    // Game::Render() was just called with for this same pass (see
    // IEditorLayer::SceneViewProjection()). Always a safe no-op for
    // NullEditorLayer (a release build has no "Scene" panel, and this is
    // simply never meaningfully reachable there either way, since
    // SceneViewTarget() already always returns nullptr for it).
    virtual void RenderSceneGrid(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection) = 0;

    // Builds every editor panel for this frame - top menu bar (File > Exit,
    // ...), Hierarchy (left), Inspector (right), Scene/Game (tabbed,
    // center), and Memory (bottom, a Unity-Memory-Profiler-style GPU memory
    // panel - see Panels/MemoryPanel.cpp) inside a full-viewport ImGui
    // docking DockSpace, so the user can freely rearrange/split them (e.g.
    // drag Scene and Game apart to view both at once). `game` is the same
    // Game Application owns - Hierarchy lists its ECS world's entities
    // (Game::GetRegistry()) and spawns new primitive entities via
    // Game::CreatePrimitiveEntity() (its "Create 3D Object" context menu -
    // see Panels/HierarchyPanel.cpp), Inspector edits the selected entity's
    // components. Taking `Game&` here (rather than pre-extracting just
    // Registry&, as before Create 3D Object existed) does not widen what the
    // Editor can see: every panel still only ever calls Game's own small,
    // deliberate public API (GetRegistry()/CreatePrimitiveEntity()), never
    // anything Game keeps private (RenderSystem, Mesh/Pipeline pools, ...) -
    // see Game.h's class comment. `renderer` is the same Renderer this
    // Editor was constructed with (see CreateEditorLayer() below) - Memory
    // reads its GetMemoryTotals()/GetMemoryResources() to show live GPU
    // memory usage, and CreatePrimitiveEntity() needs it to build/upload
    // that shape's GPU mesh the first time it's requested. Call after Game
    // has finished rendering into GameViewTarget()/SceneViewTarget()
    // (whichever came back non-null), so the "Game"/"Scene" panels have
    // fresh contents to display this frame.
    // `renderGraph` (Phase 8 -
    // RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md) is the SAME
    // gte::rg::RenderGraph Application drives every frame (two Execute()
    // calls - see Application::Run()) - the "Render Graph" panel
    // (Panels/RenderGraphPanel.h) reads its LastSnapshot() to show which
    // passes ran/were culled last time each regime executed. Never mutated
    // by the Editor - purely observed, same spirit as `game`/`renderer`
    // above. `atmosphereSettings` (Atmosphere Scattering + Aerial
    // Perspective campaign, Phase 8 -
    // ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) is the SAME
    // AtmosphereSettings Application owns (Application::m_atmosphereSettings) -
    // the new "Atmosphere" panel (Panels/AtmospherePanel.h) reads/writes it
    // directly by reference, the same way "Inspector" reads/writes a
    // selected entity's Camera component - Application's own per-frame
    // atmosphere pass-building code (AtmospherePassSequence.h/.cpp) reads
    // whatever this panel most recently wrote. `atmosphereLutRenderer`
    // (Phase 9 - ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md)
    // is the SAME AtmosphereLutRenderer Application owns
    // (Application::m_atmosphereLutRenderer) - the "Atmosphere" panel's new
    // "Validate Transmittance LUT" button
    // (src/Editor/AtmosphereTransmittanceLutValidation.h) reads back its
    // real, currently-computed output texture through this reference.
    // `gpuDrivenBatchDebugInfo` (GPU-Driven Frustum Culling + Indirect Draw
    // campaign, render-pass-5, PHASE6) is this frame's freshly-built
    // "instances culled this frame" readout, one entry per real, eligible
    // GPU-driven batch (Game View only - Locked Design Decision 11,
    // PHASE0_MASTER_STRATEGY.md) - the "Render Graph" panel displays it
    // alongside its own existing per-pass draw-call/triangle stats. Always
    // empty on a frame with no eligible batch (including every frame in a
    // release/non-Editor build, trivially, since this whole method is never
    // called there).
    // `renderFeatureEntries` (editor-core-separation-6 campaign, PHASE7 -
    // PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) is this frame's freshly-built
    // snapshot of every loaded `_v2` render-feature plugin's own REAL,
    // resolved ordering decision (Core::GetRenderFeatureCompositor()-
    // >DebugSnapshot(), or an empty vector when there is no compositor/no
    // loaded `_v2` plugin) - the "Render Graph" panel's new "Plugin Render
    // Features" section displays it, mirroring `gpuDrivenBatchDebugInfo`
    // immediately above exactly (always empty on a frame/build with nothing
    // to show). Placed LAST so every existing call site needs only one new
    // trailing argument, never a full argument-order rewrite.
    virtual void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
        AtmosphereSettings& atmosphereSettings, AtmosphereLutRenderer& atmosphereLutRenderer,
        const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
        const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
        // editor-core-separation-8 campaign, PHASE3 - NEVER null (Core owns
        // exactly one instance as a plain value member - see
        // Core::GetRenderPassToggleRegistryMutable()). The "Render Graph" panel
        // (PHASE4) reads/writes THROUGH this exact reference to draw and mutate
        // its own "Enabled" checkbox column - see PHASE0_MASTER_STRATEGY.md's
        // Step 2.6 for why this is passed as a plain mutable reference rather
        // than routed through a bridge: both this call and
        // RenderPipeline::DeclareOnePhase()'s own consult of the SAME registry
        // happen on the main thread only, so there is no data race to guard
        // against here (unlike the genuinely cross-thread HTTP path, PHASE5).
        rg::RenderPassToggleRegistry& renderPassToggleRegistry,
        // editor-core-separation-8 campaign, PHASE3 - NULLABLE, mirroring
        // Core::GetRenderFeatureCompositor()'s own existing nullability exactly
        // (null whenever no loaded _v2 plugin exists this session). The "Render
        // Graph" panel's own per-plugin-feature "Enabled" checkbox + priority
        // DragInt (PHASE4) call SetFeatureEnabled()/SetFeaturePriority()
        // directly through this pointer, always null-checked first.
        RenderFeatureCompositor* renderFeatureCompositor) = 0;

    // Records this frame's UI draw data into cmd. Called from inside
    // Renderer::Present()'s recordExtra hook - i.e. while the swapchain
    // image is already bound as the current dynamic-rendering color
    // attachment.
    virtual void Render(VkCommandBuffer cmd) = 0;

    // Updates/renders every extra OS window currently created because a
    // panel was dragged outside the main viewport (Dear ImGui "multi-
    // viewport"/"platform windows" - see the ImGuiConfigFlags_ViewportsEnable
    // discussion in ImGuiEditorLayer's class comment). Call once per frame,
    // AFTER the main swapchain has been fully presented (i.e. after
    // Renderer::Present() returns) - each such window owns its own,
    // completely independent Vulkan swapchain, so it has nothing to do with,
    // and does not need to happen before/inside, the main window's own
    // present. No-ops for NullEditorLayer (a release build has no Editor UI,
    // so nothing can ever have been dragged outside a main window that
    // doesn't even show one), and also safely no-ops on any frame where
    // Render() above never actually ran (e.g. the main OS window was
    // minimized that frame) - see ImGuiEditorLayer::RenderPlatformWindows().
    virtual void RenderPlatformWindows() = 0;

    // True the frame after the user picked File > Exit (or any other
    // programmatic "please close the application" UI action) - checked by
    // Application::Run() once per frame to end its main loop cleanly, the
    // same way a Quit event/closing the OS window does. Always false for
    // NullEditorLayer (a release build has no such menu to click).
    virtual bool WantsExit() const = 0;

    // True if the Editor UI currently wants exclusive use of mouse input
    // this frame (e.g. the cursor is over an ImGui panel/widget, dragging a
    // slider, resizing a dock border, ...). Application checks this before
    // forwarding a translated mouse Event to InputState::Apply()/
    // Game::OnEvent(), so clicking/dragging inside the Editor's own panels
    // never also registers as gameplay input underneath them - the classic
    // ImGui-in-a-game-engine "click-through" problem. Backed by
    // ImGuiIO::WantCaptureMouse in the real implementation. Always false
    // for NullEditorLayer - a release build has no Editor UI that could
    // ever want mouse input, so Game always sees every mouse event, exactly
    // as if no Editor existed.
    virtual bool WantsCaptureMouse() const = 0;

    // Same idea as WantsCaptureMouse(), but for keyboard input (e.g. a
    // future ImGui text field currently has keyboard focus). Backed by
    // ImGuiIO::WantCaptureKeyboard. Always false for NullEditorLayer.
    virtual bool WantsCaptureKeyboard() const = 0;

    // frame-debugger-1 campaign (task_manager/frame-debugger-1/
    // PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - true whenever the
    // user currently has gameplay simulation paused via the toolbar (see
    // PlaybackControls.h). Read once per frame by Application::Run(), BEFORE
    // NewFrame(), to decide this frame's EngineContext::Time::Advance() call
    // (see PHASE4) - this necessarily reflects whatever the user last clicked
    // as of the END of the PREVIOUS frame's BuildUI() call, exactly one frame
    // of lag, the same acceptable lag every other Editor<->engine feedback
    // loop in this codebase already has (see e.g. GameViewTarget()'s own doc
    // comment on resize lag). Always false for NullEditorLayer (a release
    // build has no toolbar to pause with at all).
    virtual bool IsPlaybackPaused() const = 0;

    // True, and CLEARS the pending request (read-and-clear, exactly once), if
    // the user clicked "Step" since the last time this was called - false on
    // every other call, including every call after the first one following a
    // given click. Called unconditionally once per frame by
    // Application::Run() (see PHASE4) so a stray/stale request set while NOT
    // actually paused (should never happen - the button is rendered disabled
    // then - but this is defensive) is still drained rather than left
    // dangling forever. Always false for NullEditorLayer.
    virtual bool TryConsumeStepRequest() = 0;

    // network-impl-7 campaign - brings the named Editor panel/tab to the
    // front (Dear ImGui's own SetWindowFocus(), which for a DOCKED window
    // selects it as its dock node's active tab - exactly like a user
    // clicking the tab). `panelName` is expected to already be validated
    // against Core/EditorPanelRegistry.h's known panel list by the CALLER
    // (Application::Run(), fed from EditorUiCommandBridge - see Phase 3) -
    // this method itself does no such validation; it just tries to find and
    // focus whatever exact name it's given. Returns tabExists == false, and
    // does nothing else, if no live ImGui window with that exact name
    // exists THIS FRAME (e.g. called before this panel's own first Begin()
    // call ever ran this session). Must only ever be called between
    // NewFrame() and BuildUI() in the SAME frame (see Application::Run(),
    // Phase 3) - calling it before NewFrame() or after Render() is
    // undefined with respect to which frame's tab-selection state it
    // affects. Always returns tabExists == false for NullEditorLayer (a
    // release build has no Editor UI/tabs to activate at all).
    virtual TabActivationResult ActivateTab(const std::string& panelName) = 0;

    // task_manager/stl-parser-2, PHASE1 - imports a single external file
    // (anywhere on disk, `sourceAbsolutePath`) into the Editor's "Project"
    // folder, at `destinationRelativeFolder` (a path relative to the Project
    // root - "" means the Project root itself; auto-created if missing) -
    // the programmatic equivalent of dragging a file onto the "Project" panel
    // (see Panels/ProjectPanel.h's own HandleExternalFileDrop()). Routes
    // through the EXACT SAME AssetImporter::ImportAssetFile() pipeline and the
    // SAME live AssetDatabase instance ProjectPanel already owns, so the
    // Project panel's own tree/selection reflect this import on its very next
    // rendered frame, with no separate rescan needed from the caller. Returns
    // projectAvailable == false (every other field meaningless) when this
    // build has no "Project" panel at all - always true for NullEditorLayer,
    // and for the real ImGuiEditorLayer impl only when GTE_ENABLE_PROJECT_PANEL
    // is OFF (a real Editor build with the Project panel compiled out).
    virtual ProjectAssetImportResult ImportExternalAssetIntoProject(
        const std::string& sourceAbsolutePath, const std::string& destinationRelativeFolder) = 0;

    // task_manager/frame-debugger-3 campaign, PHASE3
    // (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step 3.4) -
    // the ONE place that decides whether the Frame Debugger's real capture
    // context is ARMED for THIS frame's Game-View render: returns a real,
    // non-null pointer (already Reset() for this fresh frame) whenever the
    // Frame Debugger window is currently open AND its own "Enable" toggle
    // is on, or nullptr otherwise (the overwhelmingly common case - see
    // PHASE1's own "zero-overhead-when-disarmed" requirement). Call ONCE
    // per frame, BEFORE Game::Render()'s Game-View branch runs (see
    // Application::Run(), which threads the result straight into
    // RenderPasses.h's AddGameViewPass()) - this necessarily reflects
    // whatever the user's "Enable"/open state was as of the END of the
    // PREVIOUS frame's BuildUI() call, the exact same one-frame lag every
    // other Editor<->engine feedback loop in this codebase already has
    // (see e.g. IsPlaybackPaused()'s own doc comment). Always nullptr for
    // NullEditorLayer (a release build has no Frame Debugger to arm).
    virtual IFrameDebuggerCaptureRecorder* PrepareFrameDebuggerCaptureContext() = 0;

    // The Frame Debugger's own Step-triggered capture (PHASE3's Step 3.2,
    // call site 2) - called by Application::Run() right where
    // TryConsumeStepRequest() above is already checked, i.e. BEFORE
    // Game::Render() even runs this frame, whenever that call returned
    // true. Merely records "a Step happened this frame" - the REAL capture
    // (which needs this frame's now-FINAL RenderGraphSnapshot/
    // FrameDebuggerCaptureContext/Game View pixels) is actually performed
    // later the SAME frame, from inside BuildUI() (see
    // Panels/FrameDebuggerPanel.cpp's own TriggerCapture()), once the
    // Game-View render this Step just drove has genuinely finished. A
    // no-op for NullEditorLayer.
    virtual void NotifyFrameDebuggerStepConsumed() = 0;

    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.0) - the EARLY half of the two-bool pending/serviced handshake
    // this phase introduces, generalizing Phase 2's single-bool deferred-
    // Enable-edge-capture mechanism to ALL THREE capture triggers (Enable
    // edge / Step / Capture button). Called ONCE per frame by
    // Application::Run(), from inside the offscreen `build` lambda, BEFORE
    // this frame's "GameView" pass (and therefore
    // AddFrameDebuggerReplayPasses(), if this returns true) is declared -
    // read-and-clear, true for exactly the one frame following a real
    // capture trigger. Forwards straight into
    // FrameDebuggerPanel::ConsumePendingReplayRequest() - see that
    // method's own doc comment (Panels/FrameDebuggerPanel.h) for the full
    // two-bool handshake this is one half of. Always false for
    // NullEditorLayer (a release build has no Frame Debugger to arm).
    virtual bool ConsumePendingFrameDebuggerReplayRequest() = 0;

    // task_manager/frame-debugger-3 campaign, PHASE7
    // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - the
    // HTTP-automation surface for the Frame Debugger window
    // (`/frame_debugger/*` routes - see NetworkRoutes.h/NetworkServer.cpp
    // and the new src/Application/FrameDebuggerCommandBridge.h). Each
    // method below mirrors exactly what its corresponding piece of
    // hand-driven UI already does (see Panels/FrameDebuggerPanel.cpp's own
    // matching method for the real implementation) - Application::Run()'s
    // own FrameDebuggerCommandBridge pump (mirroring EditorUiCommandBridge's
    // own pump, ActivateTab() above) is what maps one bridge request's
    // `kind` to exactly ONE of these calls.

    // Opens the Frame Debugger window (idempotent - a no-op if already
    // open) and arranges for it to be PINNED to the main ImGui viewport for
    // that one opening only (see Panels/FrameDebuggerPanel.h's own
    // m_pinToMainViewportNextOpen bool) - this is what guarantees
    // GET /get_swapchain can see it on the very first frame it opens.
    // Never touched by, and never regresses, ordinary manual "Window >
    // Frame Debugger" use (DockLayout.cpp's own checkable menu item flips
    // ctx.frameDebuggerWindowOpen directly and has no reference to this
    // method at all). A no-op for NullEditorLayer.
    virtual void FrameDebuggerOpenWindow() = 0;

    // Mirrors the "Enable" checkbox's own false->true/true->false edges
    // exactly, including the false->true edge's "auto-engage Pause" side
    // effect. task_manager/frame-debugger-7 campaign, PHASE2
    // (PHASE2_DEFERRED_CAPTURE_TRIGGER.md) - the false->true edge no longer
    // triggers a real capture synchronously here; it only arms a deferred
    // one-shot flag consumed at the START of the very next Build() call
    // (fixes Bug 1 - see Panels/FrameDebuggerPanel.cpp's own
    // BuildToolbarRow()/ApplyEnabledEdge() doc comments for the full "why").
    // A no-op for NullEditorLayer.
    virtual void FrameDebuggerSetEnabled(bool enabled) = 0;

    // editor-core-separation-8 campaign, PHASE3 - HTTP automation entry point
    // (GET /render_graph/set_blur_enabled, PHASE5) for exactly the SAME state
    // ScenePanel.cpp's own "Show Compute Blur (debug)" checkbox already flips
    // directly (EditorContext::showBlurredSceneOutput) - and, per
    // PHASE0_MASTER_STRATEGY.md's Locked Product Decision #10, also what the
    // "Render Graph" panel's own NEW matching checkbox (PHASE4) calls. Mirrors
    // FrameDebuggerSetEnabled(bool)'s exact shape - a plain, always-succeeding
    // setter (no return value; there is no failure mode for flipping a bool).
    virtual void SetShowBlurredSceneOutput(bool enabled) = 0;

    // editor-core-separation-8 campaign, PHASE3 - the GBuffer Validation
    // equivalent of SetShowBlurredSceneOutput() immediately above - same
    // contract, same reasoning, mirrors EditorContext::showGBufferValidationOutput.
    virtual void SetShowGBufferValidationOutput(bool enabled) = 0;

    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - hands the real ImGui implementation a live
    // IProjectLifecycleCapability* (Core/EditorCapabilities.h) so its own
    // "New Project..." floating window (NewProjectWindow.h) can call
    // CreateNewProjectAssembly() directly - the exact SAME method
    // POST /project_assembly/create_project calls (LDD-PW5's "one function,
    // two callers" rule). Called exactly ONCE, from EditorHost's own
    // constructor body, immediately after Core::SetEditorLayerHook() -
    // never per-frame, unlike BuildUI()'s own trailing parameters, since
    // this pointer's value never changes for the life of the process
    // (mirrors how m_sceneIOCapability/the hot-reload-debug-capability
    // static are each wired exactly once too). Always a safe no-op for
    // NullEditorLayer (a release build has no "New Project..." window to
    // give a capability to at all).
    virtual void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) = 0;

    // editor-core-separation-18 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 4), PHASE3 - hands the real ImGui implementation a live
    // IAssetScaffoldingCapability* (Core/EditorCapabilities.h) so its own
    // "Create -> Render Pass/Compute Shader/Shader Pair" floating window
    // (CreateAssetWindow.h) can call CreateAssetScaffold() directly - the exact
    // SAME method POST /project_assembly/create_asset calls (LDD-PW5's "one
    // function, two callers" rule, mirroring SetProjectLifecycleCapability's
    // own precedent immediately above). Called exactly ONCE, from
    // EditorHost's own constructor body. Always a safe no-op for
    // NullEditorLayer.
    virtual void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* capability) = 0;

    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1 - hands the real ImGui implementation a live
    // IHotReloadDebugCapability* (Core/EditorCapabilities.h) so
    // DockLayout.cpp's own "Project > Compile" menu item can call
    // TriggerCompileOnly()/IsCompileInFlight() directly - the exact SAME
    // methods POST /project_assembly/debug/compile_only already calls
    // (LDD-PW5's "one function, two callers" rule, mirroring
    // SetProjectLifecycleCapability's own precedent above). Called exactly
    // ONCE, from EditorHost's own constructor body. Always a safe no-op for
    // NullEditorLayer (a release build has no menu bar to wire this into at
    // all).
    virtual void SetHotReloadDebugCapability(IHotReloadDebugCapability* capability) = 0;

    // Requests a real capture - returns false (a safe no-op) if the Frame
    // Debugger is not currently enabled (mirroring the "Capture" button's
    // own BeginDisabled(!m_enabled) guard), true otherwise.
    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.0) - no longer forces a synchronous TriggerCapture() this frame;
    // only arms the same deferred m_pendingCaptureTrigger flag the
    // hand-driven "Capture" button now sets, so this frame's worth of
    // replay passes can be declared before Game::Render() runs on the
    // NEXT frame - see FrameDebuggerPanel::CaptureNowFromCommand()'s own
    // doc comment. Always false for NullEditorLayer.
    virtual bool FrameDebuggerCaptureNow() = 0;

    // Sets the currently-selected leaf event's global index (clamped via
    // ClampSelectedEventIndex() against the currently-viewed captured
    // frame's own totalEventCount, exactly like a tree-row click already
    // does). A no-op for NullEditorLayer.
    virtual void FrameDebuggerSelectEvent(int index) = 0;

    // Sets the Channels row's active channel from "all"/"r"/"g"/"b"/"a"
    // (case-sensitive, lowercase-only - mirrors NetworkRoutes.cpp's own
    // existing exact-lowercase-matching convention for every other query
    // parameter in that file). Returns false (a safe no-op) for any other
    // string - defense in depth only, since NetworkRoutes.cpp's own query
    // parser already rejects an invalid value with its own 400 response
    // before this is ever called. Always false for NullEditorLayer.
    virtual bool FrameDebuggerSetChannel(const std::string& channel) = 0;

    // Sets the Levels black/white points directly (same defensive
    // "white > black + 0.001f" clamp the DragFloatRange2 UI control itself
    // already applies). A no-op for NullEditorLayer.
    virtual void FrameDebuggerSetLevels(float black, float white) = 0;

    // Read-only status snapshot for GET /frame_debugger/state - always
    // meaningful (never throws/asserts), reflecting whatever the Frame
    // Debugger's real state currently is. All-default for NullEditorLayer
    // (enabled == false, windowOpen == false, channel == "all", ...).
    virtual FrameDebuggerStateSnapshotView FrameDebuggerGetState() const = 0;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
    // - spawns `instanceCount` new entities sharing one hand-authored,
    // untextured, indexed, VertexLayout::PositionNormal Mesh+Pipeline pair
    // (see src/Editor/GpuDrivenBatchTestSpawner.h) - the real, repeatable
    // way to get at least one live GPU-driven-eligible batch
    // (>= kMinInstancesForGpuDrivenBatch) into the scene, confirmed via
    // ask_questions since no existing demo-scene/asset content already
    // qualifies. `game`/`renderer` are the SAME Game/Renderer Application
    // owns - mirrors CreatePrimitiveEntity()'s own "Editor hands Game/
    // Renderer through, the real spawn logic lives elsewhere" shape. Always
    // returns success == false, editorAvailable == false for NullEditorLayer
    // (a release build has no Editor-only validation tooling to spawn
    // through at all) - mirrors ImportExternalAssetIntoProject()'s own
    // "editorAvailable"-style graceful-unavailability precedent (editor-
    // core-separation-1 campaign, PHASE8 - see GpuDrivenTestBatchSpawnResult's
    // own doc comment above).
    virtual GpuDrivenTestBatchSpawnResult SpawnGpuDrivenTestBatch(Game& game, Renderer& renderer, std::uint32_t instanceCount) = 0;
};

// Constructs the real ImGui-backed editor layer. editor-core-separation-1
// campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md, Locked Design Decision
// #9) - this is now the ONLY declaration of gte::CreateEditorLayer() in the
// entire repository with exactly ONE definition (src/Editor/ImGuiEditorLayer.cpp,
// gte_editor-only) - EditorHost/Application always call this one,
// unambiguously.
std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer);

// Always-available, zero-ImGui/zero-SDL-dependency fallback living inside
// gte_core itself (src/Editor/NullEditorLayer.cpp) - the ONE thing a future
// Player host (linking gte_core alone, never seeing gte_editor's source)
// can call to get a working, no-op IEditorLayer. Never called anywhere in
// THIS repo - EditorHost/Application always call the real CreateEditorLayer()
// above instead, which resolves unambiguously to gte_editor's own
// ImGuiEditorLayer.cpp - there is no longer a second definition of THAT name
// anywhere in the link (the ODR/link-order hazard this rename fixes).
std::unique_ptr<IEditorLayer> CreateNullEditorLayer(Window& window, Renderer& renderer);

} // namespace gte
