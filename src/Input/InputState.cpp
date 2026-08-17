#include "InputState.h"

#include <variant>

namespace gte {

void InputState::BeginFrame()
{
    m_keysPressed.fill(false);
    m_keysReleased.fill(false);
    m_mouseButtonsPressed.fill(false);
    m_mouseButtonsReleased.fill(false);

    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_mouseWheelDeltaX = 0.0f;
    m_mouseWheelDeltaY = 0.0f;
}

void InputState::Apply(const Event& event)
{
    switch (event.type) {
    case EventType::KeyDown: {
        const auto& data = std::get<KeyEventData>(event.data);
        const auto index = static_cast<std::size_t>(data.code);
        if (!data.repeat && !m_keysDown[index]) {
            m_keysPressed[index] = true;
        }
        m_keysDown[index] = true;
        break;
    }
    case EventType::KeyUp: {
        const auto& data = std::get<KeyEventData>(event.data);
        const auto index = static_cast<std::size_t>(data.code);
        m_keysDown[index] = false;
        m_keysReleased[index] = true;
        break;
    }
    case EventType::MouseMoved: {
        const auto& data = std::get<MouseMoveEventData>(event.data);
        m_mouseX = data.x;
        m_mouseY = data.y;
        m_mouseDeltaX += data.deltaX;
        m_mouseDeltaY += data.deltaY;
        break;
    }
    case EventType::MouseButtonDown: {
        const auto& data = std::get<MouseButtonEventData>(event.data);
        const auto index = static_cast<std::size_t>(data.button);
        if (!m_mouseButtonsDown[index]) {
            m_mouseButtonsPressed[index] = true;
        }
        m_mouseButtonsDown[index] = true;
        break;
    }
    case EventType::MouseButtonUp: {
        const auto& data = std::get<MouseButtonEventData>(event.data);
        const auto index = static_cast<std::size_t>(data.button);
        m_mouseButtonsDown[index] = false;
        m_mouseButtonsReleased[index] = true;
        break;
    }
    case EventType::MouseWheel: {
        const auto& data = std::get<MouseWheelEventData>(event.data);
        m_mouseWheelDeltaX += data.deltaX;
        m_mouseWheelDeltaY += data.deltaY;
        break;
    }
    case EventType::Quit:
    case EventType::WindowResized:
    default:
        // Not tracked as continuous input state - handled elsewhere
        // (Game::OnEvent) since these are discrete/one-shot in nature.
        break;
    }
}

bool InputState::IsKeyDown(KeyCode code) const
{
    return m_keysDown[static_cast<std::size_t>(code)];
}

bool InputState::WasKeyPressed(KeyCode code) const
{
    return m_keysPressed[static_cast<std::size_t>(code)];
}

bool InputState::WasKeyReleased(KeyCode code) const
{
    return m_keysReleased[static_cast<std::size_t>(code)];
}

bool InputState::IsMouseButtonDown(MouseButton button) const
{
    return m_mouseButtonsDown[static_cast<std::size_t>(button)];
}

bool InputState::WasMouseButtonPressed(MouseButton button) const
{
    return m_mouseButtonsPressed[static_cast<std::size_t>(button)];
}

bool InputState::WasMouseButtonReleased(MouseButton button) const
{
    return m_mouseButtonsReleased[static_cast<std::size_t>(button)];
}

} // namespace gte
