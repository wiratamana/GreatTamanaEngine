#pragma once

#include "../Math/Mat4.h"
#include "../Renderer/RenderTexture.h"

#include <memory>

// Forward-declared so this header needs no SDL dependency at all (matches
// the Vulkan-handle forward-declare trick already used in Window.h) - only
// whichever concrete implementation's .cpp needs the full SDL_Event
// definition.
union SDL_Event;

namespace gte {

class Window;
class Renderer;
class Game;

namespace rg {
class RenderGraph;
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
    // above.
    virtual void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph) = 0;

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
};

// Constructs the real ImGui-backed editor layer, or the inert Null one,
// depending entirely on which .cpp got linked in (see GTE_ENABLE_EDITOR in
// CMakeLists.txt) - Application calls this once, at startup, and never
// needs to know or care which one it got.
std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer);

} // namespace gte
