#pragma once

#include <array>
#include <cstddef>

#include "../Event/Event.h"

namespace gte {

// Tracks continuous, queryable input state (what's held down right now,
// current mouse position, etc). Fed exclusively by translated gte::Event
// values via Apply() - has no idea SDL exists.
//
// This complements (rather than replaces) discrete event handling: use
// InputState for "is this key held" style polling in Game::Update, and
// Game::OnEvent for one-shot/discrete reactions (quit, resize, a single
// key press you only want to act on once).
class InputState {
public:
    InputState() = default;

    // Call once per frame, before polling/translating this frame's SDL
    // events. Clears the transient "just pressed/released this frame" flags
    // and the per-frame mouse/wheel deltas so they only ever reflect the
    // current frame.
    void BeginFrame();

    // Feeds one translated engine event into the running state. Call this
    // for every Event produced by EventTranslator during the frame.
    void Apply(const Event& event);

    bool IsKeyDown(KeyCode code) const;
    bool WasKeyPressed(KeyCode code) const;  // true only on the frame the key went down
    bool WasKeyReleased(KeyCode code) const; // true only on the frame the key went up

    bool IsMouseButtonDown(MouseButton button) const;
    bool WasMouseButtonPressed(MouseButton button) const;
    bool WasMouseButtonReleased(MouseButton button) const;

    float MouseX() const noexcept { return m_mouseX; }
    float MouseY() const noexcept { return m_mouseY; }
    float MouseDeltaX() const noexcept { return m_mouseDeltaX; }
    float MouseDeltaY() const noexcept { return m_mouseDeltaY; }

    float MouseWheelDeltaX() const noexcept { return m_mouseWheelDeltaX; }
    float MouseWheelDeltaY() const noexcept { return m_mouseWheelDeltaY; }

private:
    static constexpr std::size_t kKeyCodeCount = static_cast<std::size_t>(KeyCode::Count);
    static constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

    std::array<bool, kKeyCodeCount> m_keysDown{};
    std::array<bool, kKeyCodeCount> m_keysPressed{};
    std::array<bool, kKeyCodeCount> m_keysReleased{};

    std::array<bool, kMouseButtonCount> m_mouseButtonsDown{};
    std::array<bool, kMouseButtonCount> m_mouseButtonsPressed{};
    std::array<bool, kMouseButtonCount> m_mouseButtonsReleased{};

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;

    float m_mouseWheelDeltaX = 0.0f;
    float m_mouseWheelDeltaY = 0.0f;
};

} // namespace gte
