#include "EventTranslator.h"

namespace gte {

std::optional<Event> EventTranslator::Translate(const SDL_Event& sdlEvent)
{
    switch (sdlEvent.type) {
    case SDL_EVENT_QUIT: {
        Event event;
        event.type = EventType::Quit;
        return event;
    }

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        Event event;
        event.type = (sdlEvent.type == SDL_EVENT_KEY_DOWN) ? EventType::KeyDown : EventType::KeyUp;

        KeyEventData data;
        data.code = TranslateKeyCode(sdlEvent.key.key);
        data.repeat = sdlEvent.key.repeat;
        event.data = data;
        return event;
    }

    case SDL_EVENT_MOUSE_MOTION: {
        Event event;
        event.type = EventType::MouseMoved;

        MouseMoveEventData data;
        data.x = sdlEvent.motion.x;
        data.y = sdlEvent.motion.y;
        data.deltaX = sdlEvent.motion.xrel;
        data.deltaY = sdlEvent.motion.yrel;
        event.data = data;
        return event;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        Event event;
        event.type = (sdlEvent.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            ? EventType::MouseButtonDown
            : EventType::MouseButtonUp;

        MouseButtonEventData data;
        data.button = TranslateMouseButton(sdlEvent.button.button);
        data.x = sdlEvent.button.x;
        data.y = sdlEvent.button.y;
        event.data = data;
        return event;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        Event event;
        event.type = EventType::MouseWheel;

        MouseWheelEventData data;
        data.deltaX = sdlEvent.wheel.x;
        data.deltaY = sdlEvent.wheel.y;
        event.data = data;
        return event;
    }

    case SDL_EVENT_WINDOW_RESIZED: {
        Event event;
        event.type = EventType::WindowResized;

        WindowResizedEventData data;
        data.width = sdlEvent.window.data1;
        data.height = sdlEvent.window.data2;
        event.data = data;
        return event;
    }

    default:
        return std::nullopt;
    }
}

KeyCode EventTranslator::TranslateKeyCode(SDL_Keycode sdlKeyCode)
{
    switch (sdlKeyCode) {
    case SDLK_A: return KeyCode::A;
    case SDLK_B: return KeyCode::B;
    case SDLK_C: return KeyCode::C;
    case SDLK_D: return KeyCode::D;
    case SDLK_E: return KeyCode::E;
    case SDLK_F: return KeyCode::F;
    case SDLK_G: return KeyCode::G;
    case SDLK_H: return KeyCode::H;
    case SDLK_I: return KeyCode::I;
    case SDLK_J: return KeyCode::J;
    case SDLK_K: return KeyCode::K;
    case SDLK_L: return KeyCode::L;
    case SDLK_M: return KeyCode::M;
    case SDLK_N: return KeyCode::N;
    case SDLK_O: return KeyCode::O;
    case SDLK_P: return KeyCode::P;
    case SDLK_Q: return KeyCode::Q;
    case SDLK_R: return KeyCode::R;
    case SDLK_S: return KeyCode::S;
    case SDLK_T: return KeyCode::T;
    case SDLK_U: return KeyCode::U;
    case SDLK_V: return KeyCode::V;
    case SDLK_W: return KeyCode::W;
    case SDLK_X: return KeyCode::X;
    case SDLK_Y: return KeyCode::Y;
    case SDLK_Z: return KeyCode::Z;

    case SDLK_0: return KeyCode::Num0;
    case SDLK_1: return KeyCode::Num1;
    case SDLK_2: return KeyCode::Num2;
    case SDLK_3: return KeyCode::Num3;
    case SDLK_4: return KeyCode::Num4;
    case SDLK_5: return KeyCode::Num5;
    case SDLK_6: return KeyCode::Num6;
    case SDLK_7: return KeyCode::Num7;
    case SDLK_8: return KeyCode::Num8;
    case SDLK_9: return KeyCode::Num9;

    case SDLK_SPACE: return KeyCode::Space;
    case SDLK_RETURN: return KeyCode::Enter;
    case SDLK_ESCAPE: return KeyCode::Escape;
    case SDLK_BACKSPACE: return KeyCode::Backspace;
    case SDLK_TAB: return KeyCode::Tab;

    case SDLK_LEFT: return KeyCode::Left;
    case SDLK_RIGHT: return KeyCode::Right;
    case SDLK_UP: return KeyCode::Up;
    case SDLK_DOWN: return KeyCode::Down;

    case SDLK_LSHIFT: return KeyCode::LeftShift;
    case SDLK_RSHIFT: return KeyCode::RightShift;
    case SDLK_LCTRL: return KeyCode::LeftCtrl;
    case SDLK_RCTRL: return KeyCode::RightCtrl;
    case SDLK_LALT: return KeyCode::LeftAlt;
    case SDLK_RALT: return KeyCode::RightAlt;

    case SDLK_F1: return KeyCode::F1;
    case SDLK_F2: return KeyCode::F2;
    case SDLK_F3: return KeyCode::F3;
    case SDLK_F4: return KeyCode::F4;
    case SDLK_F5: return KeyCode::F5;
    case SDLK_F6: return KeyCode::F6;
    case SDLK_F7: return KeyCode::F7;
    case SDLK_F8: return KeyCode::F8;
    case SDLK_F9: return KeyCode::F9;
    case SDLK_F10: return KeyCode::F10;
    case SDLK_F11: return KeyCode::F11;
    case SDLK_F12: return KeyCode::F12;

    default: return KeyCode::Unknown;
    }
}

MouseButton EventTranslator::TranslateMouseButton(Uint8 sdlButton)
{
    switch (sdlButton) {
    case SDL_BUTTON_LEFT: return MouseButton::Left;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    case SDL_BUTTON_RIGHT: return MouseButton::Right;
    case SDL_BUTTON_X1: return MouseButton::X1;
    case SDL_BUTTON_X2: return MouseButton::X2;
    default: return MouseButton::Left;
    }
}

} // namespace gte
