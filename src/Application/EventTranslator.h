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
    static std::optional<Event> Translate(const SDL_Event& sdlEvent);

private:
    static KeyCode TranslateKeyCode(SDL_Keycode sdlKeyCode);
    static MouseButton TranslateMouseButton(Uint8 sdlButton);
};

} // namespace gte
