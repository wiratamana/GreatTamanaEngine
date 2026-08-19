#pragma once

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

// Abstraction boundary between engine-core (Application/Renderer/Game) and
// the optional Editor/Debug UI. Dear ImGui-backed in real builds, but
// nothing outside src/Editor/ImGuiEditorLayer.cpp ever includes an ImGui
// header - Application only ever talks to this interface.
//
// Game never depends on this at all, in either direction: Game has no idea
// the Editor exists, and the Editor only ever *observes* Game/Renderer
// through their existing public accessors (never the other way around).
// That is what makes turning the Editor off (GTE_ENABLE_EDITOR=OFF in
// CMakeLists.txt) a genuinely zero-touch operation for gameplay code.
//
// Two implementations exist, selected entirely by which .cpp got compiled
// (see CMakeLists.txt) - never both at once, so there's no #ifdef soup
// anywhere that calls into this interface:
//   - ImGuiEditorLayer (src/Editor/ImGuiEditorLayer.cpp) - the real thing:
//     owns the ImGui context, the SDL3 + Vulkan backends, and a
//     RenderTexture Game's camera renders into for the "Game" panel. Only
//     compiled when GTE_ENABLE_EDITOR is ON.
//   - NullEditorLayer (src/Editor/NullEditorLayer.cpp) - every method is a
//     no-op and GameViewTarget() always returns nullptr, meaning "render
//     straight to the swapchain" - i.e. a release build behaves exactly as
//     if no Editor/ImGui ever existed. Compiled instead when
//     GTE_ENABLE_EDITOR is OFF, with zero ImGui code or linkage anywhere
//     in the binary.
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
    // reacts to). The real implementation no-ops: the Game-view
    // RenderTexture tracks the "Game" panel's own content-region size
    // instead of the OS window's size (Unity-style "Free Aspect" - see
    // GameViewTarget()/BuildUI() in ImGuiEditorLayer), so an OS window
    // resize by itself is not a reason to resize it. Kept in the interface
    // only in case a future implementation needs it - Null impl no-ops too.
    virtual void OnWindowResized(int width, int height) = 0;

    // Starts a new UI frame. Call once per frame, before Game's
    // Update()/Render().
    virtual void NewFrame() = 0;

    // The render target Game's gameplay camera should draw into THIS
    // frame: a RenderTexture the editor wants to display inside a "Game"
    // panel, or nullptr meaning "render straight to the swapchain,
    // fullscreen" (what the Null implementation always returns). The real
    // implementation also resizes this texture here (if the "Game" panel's
    // content-region size changed since last frame's BuildUI()) before
    // returning it - the last safe/needed point to do so, since Game is
    // about to render into it. See ImGuiEditorLayer's class comment.
    virtual RenderTexture* GameViewTarget() = 0;

    // Builds every editor panel for this frame (currently just "Game";
    // Hierarchy/Inspector/Scene follow once there's a scene to inspect).
    // Call after Game has finished rendering into GameViewTarget() (if
    // any), so the "Game" panel has fresh contents to display this frame.
    virtual void BuildUI() = 0;

    // Records this frame's UI draw data into cmd. Called from inside
    // Renderer::Present()'s recordExtra hook - i.e. while the swapchain
    // image is already bound as the current dynamic-rendering color
    // attachment.
    virtual void Render(VkCommandBuffer cmd) = 0;
};

// Constructs the real ImGui-backed editor layer, or the inert Null one,
// depending entirely on which .cpp got linked in (see GTE_ENABLE_EDITOR in
// CMakeLists.txt) - Application calls this once, at startup, and never
// needs to know or care which one it got.
std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer);

} // namespace gte
