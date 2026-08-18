#pragma once

#include <variant>

namespace gte {

// Engine-owned key identifiers, decoupled from SDL's SDL_Keycode. Only
// EventTranslator (in the Application layer) knows how to map SDL's codes
// onto these - everything else in the engine works with KeyCode only and
// never has to know SDL exists.
enum class KeyCode {
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    Space, Enter, Escape, Backspace, Tab,

    Left, Right, Up, Down,

    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Count // Sentinel - keep last. Used to size lookup tables (see InputState).
};

// Engine-owned mouse button identifiers, decoupled from SDL's button indices.
enum class MouseButton {
    // Returned by EventTranslator for a button code it doesn't recognize.
    // Never actually delivered in an Event - EventTranslator::Translate()
    // filters an unrecognized mouse button out entirely (returns
    // std::nullopt) rather than letting it alias to a real button like
    // Left, so an unmapped/exotic mouse button can never masquerade as a
    // Left-button press/release.
    Unknown = 0,

    Left,
    Middle,
    Right,
    X1,
    X2,

    Count // Sentinel - keep last. Used to size lookup tables (see InputState).
};

// The kinds of events the engine knows about. Deliberately a small,
// hand-picked subset of what SDL can report - extend as the engine actually
// needs more (e.g. gamepad, text input).
enum class EventType {
    Quit,
    KeyDown,
    KeyUp,
    MouseMoved,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    WindowResized,
};

struct KeyEventData {
    KeyCode code = KeyCode::Unknown;
    bool repeat = false;
};

struct MouseMoveEventData {
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

struct MouseButtonEventData {
    MouseButton button = MouseButton::Unknown;
    float x = 0.0f;
    float y = 0.0f;
};

struct MouseWheelEventData {
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

struct WindowResizedEventData {
    int width = 0;
    int height = 0;
};

// The engine's own event representation. Produced exclusively by
// EventTranslator from raw SDL events - nothing past that point (InputState,
// Game, and any future subsystem) ever has to touch SDL_Event directly.
struct Event {
    EventType type;

    std::variant<
        std::monostate,
        KeyEventData,
        MouseMoveEventData,
        MouseButtonEventData,
        MouseWheelEventData,
        WindowResizedEventData
    > data;
};

} // namespace gte
