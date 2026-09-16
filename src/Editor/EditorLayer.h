#pragma once

#include "../Math/Mat4.h"
#include "../Math/Vec3.h"
#include "../Renderer/Atmosphere/AtmosphereTypes.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

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
// compile with GTE_ENABLE_EDITOR=OFF, a build where this type does not
// exist at all. A bare forward declaration of a pointee is always legal
// even when the type is never defined in this translation unit, since
// PrepareFrameDebuggerCaptureContext() below only ever needs a POINTER to
// it.
class FrameDebuggerCaptureContext;

namespace rg {
class RenderGraph;
class RenderGraphBuilder;
} // namespace rg

// Abstraction boundary between engine-core (Application/Renderer/Game) and
// the optional Editor/Debug UI. Dear ImGui-backed in real builds, but
// nothing outside src/Editor/ (specifically: nothing outside whichever
// files there are only ever compiled under GTE_ENABLE_EDITOR - see
// AGENTS.md, "Editor Module Structure") ever includes an ImGui header -
// Application only ever talks to this interface.
//
// Game never depends on this at all, in either direction: Game has no idea
// the Editor exists, and the Editor only ever *observes*/edits Game/Renderer
// through their existing public accessors (never the other way around) -
// e.g. Game::GetRegistry() is what lets the Hierarchy/Inspector panels below
// see and edit the ECS world without Game gaining any Editor awareness.
// That is what makes turning the Editor off (GTE_ENABLE_EDITOR=OFF in
// CMakeLists.txt) a genuinely zero-touch operation for gameplay code.
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
//     bar). Only compiled when GTE_ENABLE_EDITOR is ON.
//   - NullEditorLayer (src/Editor/NullEditorLayer.cpp) - every method is a
//     no-op and GameViewTarget()/SceneViewTarget() always return nullptr,
//     meaning "render straight to the swapchain" - i.e. a release build
//     behaves exactly as if no Editor/ImGui ever existed. Compiled instead
//     when GTE_ENABLE_EDITOR is OFF, with zero ImGui code or linkage
//     anywhere in the binary.
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
    int historyCount = 0;
    int historyCursor = 0;
    int totalEventCount = 0;
    int selectedEventIndex = -1;
    std::string channel = "all";
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;
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
    virtual void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
        AtmosphereSettings& atmosphereSettings, AtmosphereLutRenderer& atmosphereLutRenderer) = 0;

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
    // against EditorPanelCatalog.h's known panel list by the CALLER
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
    virtual FrameDebuggerCaptureContext* PrepareFrameDebuggerCaptureContext() = 0;

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
    // exactly, including the false->true edge's "auto-engage Pause +
    // trigger the very first real capture" side effect (see
    // Panels/FrameDebuggerPanel.cpp's own BuildToolbarRow()/
    // ApplyEnabledEdge()). A no-op for NullEditorLayer.
    virtual void FrameDebuggerSetEnabled(bool enabled) = 0;

    // Forces PHASE3's TriggerCapture() this frame - returns false (a safe
    // no-op) if the Frame Debugger is not currently enabled (mirroring the
    // "Capture" button's own BeginDisabled(!m_enabled) guard), true
    // otherwise. Always false for NullEditorLayer.
    virtual bool FrameDebuggerCaptureNow() = 0;

    // Sets the currently-selected leaf event's global index (clamped via
    // ClampSelectedEventIndex() against the currently-viewed captured
    // frame's own totalEventCount, exactly like a tree-row click already
    // does). A no-op for NullEditorLayer.
    virtual void FrameDebuggerSelectEvent(int index) = 0;

    // Steps the Frame-History cursor (+1 next / -1 prev / any other delta -
    // see FrameDebuggerHistory::StepCursor()). A no-op for NullEditorLayer.
    virtual void FrameDebuggerStepHistory(int delta) = 0;

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
};

// Constructs the real ImGui-backed editor layer, or the inert Null one,
// depending entirely on which .cpp got linked in (see GTE_ENABLE_EDITOR in
// CMakeLists.txt) - Application calls this once, at startup, and never
// needs to know or care which one it got.
std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer);

} // namespace gte
