// Unit tests for EventTranslator (src/Application/EventTranslator.h) - the
// SDL_Event -> gte::Event boundary. Every test constructs a plain SDL_Event
// by hand (a zero-initialized union plus whichever member fields matter for
// that event type) - no SDL_Init()/window/video subsystem is needed, since
// SDL_Event is just a plain struct/union and
// EventTranslator::Translate()/TranslateKeyCode()/TranslateMouseButton() are
// pure functions of its fields (see EventTranslator.cpp) - nothing here ever
// touches the real SDL event queue.

#include "Application/EventTranslator.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(EventTranslatorTest, Quit_TranslatesToQuitEvent)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_QUIT;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::Quit);
}

TEST(EventTranslatorTest, KeyDown_TranslatesKnownKeyCodeAndRepeatFlag)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_KEY_DOWN;
    sdlEvent.key.key = SDLK_W;
    sdlEvent.key.repeat = true;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::KeyDown);
    const auto& data = std::get<KeyEventData>(event->data);
    EXPECT_EQ(data.code, KeyCode::W);
    EXPECT_TRUE(data.repeat);
}

TEST(EventTranslatorTest, KeyUp_TranslatesToKeyUpWithoutRepeat)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_KEY_UP;
    sdlEvent.key.key = SDLK_ESCAPE;
    sdlEvent.key.repeat = false;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::KeyUp);
    const auto& data = std::get<KeyEventData>(event->data);
    EXPECT_EQ(data.code, KeyCode::Escape);
    EXPECT_FALSE(data.repeat);
}

TEST(EventTranslatorTest, KeyDown_UnmappedSdlKeycodeBecomesUnknown_ButStillTranslates)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_KEY_DOWN;
    // SDLK_KP_0 (numpad 0) - a real SDL keycode this engine's KeyCode enum
    // has no dedicated mapping for (only the top-row SDLK_0.. SDLK_9 digits
    // are mapped - see EventTranslator::TranslateKeyCode's default case).
    sdlEvent.key.key = SDLK_KP_0;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(std::get<KeyEventData>(event->data).code, KeyCode::Unknown);
}

TEST(EventTranslatorTest, MouseMotion_TranslatesPositionAndDelta)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_MOUSE_MOTION;
    sdlEvent.motion.x = 100.0f;
    sdlEvent.motion.y = 200.0f;
    sdlEvent.motion.xrel = 5.0f;
    sdlEvent.motion.yrel = -3.0f;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::MouseMoved);
    const auto& data = std::get<MouseMoveEventData>(event->data);
    EXPECT_FLOAT_EQ(data.x, 100.0f);
    EXPECT_FLOAT_EQ(data.y, 200.0f);
    EXPECT_FLOAT_EQ(data.deltaX, 5.0f);
    EXPECT_FLOAT_EQ(data.deltaY, -3.0f);
}

TEST(EventTranslatorTest, MouseButtonDown_TranslatesKnownButtonAndPosition)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    sdlEvent.button.button = SDL_BUTTON_LEFT;
    sdlEvent.button.x = 42.0f;
    sdlEvent.button.y = 24.0f;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::MouseButtonDown);
    const auto& data = std::get<MouseButtonEventData>(event->data);
    EXPECT_EQ(data.button, MouseButton::Left);
    EXPECT_FLOAT_EQ(data.x, 42.0f);
    EXPECT_FLOAT_EQ(data.y, 24.0f);
}

TEST(EventTranslatorTest, MouseButtonUp_TranslatesToMouseButtonUp)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_MOUSE_BUTTON_UP;
    sdlEvent.button.button = SDL_BUTTON_RIGHT;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::MouseButtonUp);
    EXPECT_EQ(std::get<MouseButtonEventData>(event->data).button, MouseButton::Right);
}

TEST(EventTranslatorTest, MouseButton_AllFiveMappedButtonsTranslateCorrectly)
{
    const std::pair<Uint8, MouseButton> cases[] = {
        { SDL_BUTTON_LEFT, MouseButton::Left },
        { SDL_BUTTON_MIDDLE, MouseButton::Middle },
        { SDL_BUTTON_RIGHT, MouseButton::Right },
        { SDL_BUTTON_X1, MouseButton::X1 },
        { SDL_BUTTON_X2, MouseButton::X2 },
    };

    for (const auto& [sdlButton, expected] : cases) {
        SDL_Event sdlEvent{};
        sdlEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        sdlEvent.button.button = sdlButton;

        const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

        ASSERT_TRUE(event.has_value()) << "sdlButton=" << static_cast<int>(sdlButton);
        EXPECT_EQ(std::get<MouseButtonEventData>(event->data).button, expected)
            << "sdlButton=" << static_cast<int>(sdlButton);
    }
}

TEST(EventTranslatorTest, MouseButton_UnmappedSdlButtonIsDroppedEntirely_NotAliasedToLeft)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    // An exotic button index this engine's MouseButton enum has no mapping
    // for - EventTranslator deliberately returns nullopt rather than
    // aliasing it to e.g. Left (see MouseButton::Unknown's comment in
    // Event.h).
    sdlEvent.button.button = 200;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    EXPECT_FALSE(event.has_value());
}

TEST(EventTranslatorTest, MouseWheel_TranslatesDelta)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_MOUSE_WHEEL;
    sdlEvent.wheel.x = 1.5f;
    sdlEvent.wheel.y = -2.5f;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::MouseWheel);
    const auto& data = std::get<MouseWheelEventData>(event->data);
    EXPECT_FLOAT_EQ(data.deltaX, 1.5f);
    EXPECT_FLOAT_EQ(data.deltaY, -2.5f);
}

TEST(EventTranslatorTest, WindowResized_TranslatesNewSize)
{
    SDL_Event sdlEvent{};
    sdlEvent.type = SDL_EVENT_WINDOW_RESIZED;
    sdlEvent.window.data1 = 1920;
    sdlEvent.window.data2 = 1080;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->type, EventType::WindowResized);
    const auto& data = std::get<WindowResizedEventData>(event->data);
    EXPECT_EQ(data.width, 1920);
    EXPECT_EQ(data.height, 1080);
}

TEST(EventTranslatorTest, UnhandledSdlEventType_TranslatesToNullopt)
{
    SDL_Event sdlEvent{};
    // Text input is deliberately not among the events this engine cares
    // about yet (see EventTranslator::Translate()'s default case).
    sdlEvent.type = SDL_EVENT_TEXT_INPUT;

    const std::optional<Event> event = EventTranslator::Translate(sdlEvent);

    EXPECT_FALSE(event.has_value());
}

} // namespace
} // namespace gte
