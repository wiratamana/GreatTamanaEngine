#pragma once

#include <cstdint>

namespace gte {

enum class MouseInjectAction : std::uint8_t {
    Move,    // Place the pointer at x, y.
    Down,    // Press `button` at the current pointer position.
    Up,      // Release `button`.
    Release, // Release every button and hand the pointer back to the real mouse.
};

enum class MouseInjectButton : std::uint8_t { Left, Right, Middle };

// One synthetic mouse event for the editor's ImGui input. x and y are screen pixels (ImGui space).
struct MouseInjection {
    MouseInjectAction action = MouseInjectAction::Move;
    MouseInjectButton button = MouseInjectButton::Left;
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace gte
