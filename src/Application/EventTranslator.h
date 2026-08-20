#pragma once

// This header (and its .cpp) is one of the few places outside main.cpp/
// Application itself that is allowed to know about SDL directly - it *is*
// the boundary the AGENTS.md architecture rule talks about. Everything past
// Translate()'s return value (InputState, Game, future subsystems) only ever
// sees gte::Event and never SDL_Event.
#include <SDL3/SDL.h>

#include <optional>

#include "../Event/Event.h"

namespace gte {

// Stateless translator: raw SDL_Event -> engine's own Event (see Event.h).
class EventTranslator {
public:
    // Returns std::nullopt for SDL events the engine doesn't (yet) care
    // about, so callers can just skip anything that comes back empty.
    //
    // mainWindowId identifies Application's own main game window (see
    // Window::Id()). Once Dear ImGui's multi-viewport feature is enabled
    // (ImGuiConfigFlags_ViewportsEnable - see ImGuiEditorLayer), more than
    // one real SDL window can exist at a time: any panel dragged outside the
    // main window becomes its own extra SDL window, entirely owned/managed
    // by imgui_impl_sdl3.cpp (already fed every raw SDL_Event via
    // IEditorLayer::ProcessEvent() before Translate() is ever called - see
    // Application::Run()). Every window/keyboard/mouse SDL_Event carries the
    // ID of the specific SDL window it actually happened on; any such event
    // whose window ID does NOT match mainWindowId is dropped here (returns
    // std::nullopt) rather than translated, since it happened on one of
    // ImGui's own extra platform windows, not on the game's own window - a
    // window resize there must not resize the game's swapchain, and a mouse
    // click/move there is not gameplay input. SDL_EVENT_QUIT carries no
    // window ID at all (closing ANY window - or the whole app - always ends
    // the whole process) and is therefore never filtered by this check.
    //
    // Defaults to 0 purely so EventTranslatorTests.cpp's hand-built,
    // zero-initialized SDL_Event{} fixtures (real SDL never assigns window
    // ID 0 to an actual window) keep compiling/passing unchanged; real
    // callers (Application::Run()) always pass their actual Window::Id().
    static std::optional<Event> Translate(const SDL_Event& sdlEvent, Uint32 mainWindowId = 0);

private:
    static KeyCode TranslateKeyCode(SDL_Keycode sdlKeyCode);
    static MouseButton TranslateMouseButton(Uint8 sdlButton);
};

} // namespace gte
