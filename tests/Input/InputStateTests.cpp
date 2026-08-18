// Unit tests for InputState (src/Input/InputState.h) - covers the
// continuous "is this held" query state built up purely from translated
// gte::Event values (see Application/EventTranslatorTests.cpp for the
// SDL_Event -> gte::Event side of the pipeline). No SDL/Vulkan/window
// involved at all: InputState only ever consumes gte::Event, by design (see
// Event.h) - every test below builds Event values by hand.

#include "Input/InputState.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

Event MakeKeyEvent(EventType type, KeyCode code, bool repeat = false)
{
    Event event;
    event.type = type;
    event.data = KeyEventData{ code, repeat };
    return event;
}

Event MakeMouseMoveEvent(float x, float y, float dx, float dy)
{
    Event event;
    event.type = EventType::MouseMoved;
    event.data = MouseMoveEventData{ x, y, dx, dy };
    return event;
}

Event MakeMouseButtonEvent(EventType type, MouseButton button, float x = 0.0f, float y = 0.0f)
{
    Event event;
    event.type = type;
    event.data = MouseButtonEventData{ button, x, y };
    return event;
}

Event MakeWheelEvent(float dx, float dy)
{
    Event event;
    event.type = EventType::MouseWheel;
    event.data = MouseWheelEventData{ dx, dy };
    return event;
}

TEST(InputStateTest, FreshState_NothingIsDownOrPressedOrReleased)
{
    InputState input;

    EXPECT_FALSE(input.IsKeyDown(KeyCode::W));
    EXPECT_FALSE(input.WasKeyPressed(KeyCode::W));
    EXPECT_FALSE(input.WasKeyReleased(KeyCode::W));
    EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    EXPECT_EQ(input.MouseX(), 0.0f);
    EXPECT_EQ(input.MouseY(), 0.0f);
    EXPECT_EQ(input.MouseDeltaX(), 0.0f);
    EXPECT_EQ(input.MouseWheelDeltaY(), 0.0f);
}

TEST(InputStateTest, KeyDown_SetsDownAndJustPressedThisFrame)
{
    InputState input;
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::W));

    EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
    EXPECT_TRUE(input.WasKeyPressed(KeyCode::W));
    EXPECT_FALSE(input.WasKeyReleased(KeyCode::W));
}

TEST(InputStateTest, KeyDown_RepeatFlagNeverSetsJustPressed_EvenOnFirstEvent)
{
    InputState input;
    // Models an (unusual, but possible) OS auto-repeat event arriving before
    // any non-repeat KeyDown was ever seen for this key - must still never
    // look like a fresh "just pressed" - see InputState::Apply()'s
    // `!data.repeat` check.
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::W, /*repeat=*/true));

    EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
    EXPECT_FALSE(input.WasKeyPressed(KeyCode::W));
}

TEST(InputStateTest, KeyDown_HeldAcrossFrames_OnlyPressedOnTheFirstFrame)
{
    InputState input;
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::W));
    ASSERT_TRUE(input.WasKeyPressed(KeyCode::W));

    input.BeginFrame();
    // No new KeyDown this frame - the key is still logically held.
    EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
    EXPECT_FALSE(input.WasKeyPressed(KeyCode::W));

    // An auto-repeat KeyDown for an already-held key must not resurrect
    // WasKeyPressed().
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::W, /*repeat=*/true));
    EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
    EXPECT_FALSE(input.WasKeyPressed(KeyCode::W));
}

TEST(InputStateTest, KeyUp_ClearsDownAndSetsJustReleased)
{
    InputState input;
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::W));
    input.BeginFrame();

    input.Apply(MakeKeyEvent(EventType::KeyUp, KeyCode::W));

    EXPECT_FALSE(input.IsKeyDown(KeyCode::W));
    EXPECT_TRUE(input.WasKeyReleased(KeyCode::W));
}

TEST(InputStateTest, BeginFrame_ClearsTransientPressedReleased_ButNotDownState)
{
    InputState input;
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::Space));
    ASSERT_TRUE(input.WasKeyPressed(KeyCode::Space));

    input.BeginFrame();

    EXPECT_FALSE(input.WasKeyPressed(KeyCode::Space));
    EXPECT_FALSE(input.WasKeyReleased(KeyCode::Space));
    EXPECT_TRUE(input.IsKeyDown(KeyCode::Space)); // still held - BeginFrame() must not clear this.
}

TEST(InputStateTest, DifferentKeysAndButtons_AreTrackedIndependently)
{
    InputState input;
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::A));
    input.Apply(MakeMouseButtonEvent(EventType::MouseButtonDown, MouseButton::Left));

    EXPECT_TRUE(input.IsKeyDown(KeyCode::A));
    EXPECT_FALSE(input.IsKeyDown(KeyCode::B));
    EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Right));
}

TEST(InputStateTest, MouseMove_AbsolutePositionIsTheLatestEventThisFrame_DeltaAccumulates)
{
    InputState input;
    input.Apply(MakeMouseMoveEvent(10.0f, 20.0f, 1.0f, 2.0f));
    input.Apply(MakeMouseMoveEvent(15.0f, 25.0f, 3.0f, 4.0f));

    // Absolute position reflects only the LAST event this frame...
    EXPECT_EQ(input.MouseX(), 15.0f);
    EXPECT_EQ(input.MouseY(), 25.0f);
    // ...but the deltas accumulate across every event seen this frame.
    EXPECT_EQ(input.MouseDeltaX(), 4.0f);
    EXPECT_EQ(input.MouseDeltaY(), 6.0f);
}

TEST(InputStateTest, MouseMove_DeltaResetsOnBeginFrame_ButAbsolutePositionPersists)
{
    InputState input;
    input.Apply(MakeMouseMoveEvent(15.0f, 25.0f, 3.0f, 4.0f));

    input.BeginFrame();

    EXPECT_EQ(input.MouseDeltaX(), 0.0f);
    EXPECT_EQ(input.MouseDeltaY(), 0.0f);
    EXPECT_EQ(input.MouseX(), 15.0f); // last known position - not reset by BeginFrame().
    EXPECT_EQ(input.MouseY(), 25.0f);
}

TEST(InputStateTest, MouseButtonDown_SetsDownAndJustPressed)
{
    InputState input;
    input.Apply(MakeMouseButtonEvent(EventType::MouseButtonDown, MouseButton::Right));

    EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Right));
    EXPECT_TRUE(input.WasMouseButtonPressed(MouseButton::Right));
}

TEST(InputStateTest, MouseButtonUp_ClearsDownAndSetsJustReleased)
{
    InputState input;
    input.Apply(MakeMouseButtonEvent(EventType::MouseButtonDown, MouseButton::Left));
    input.BeginFrame();

    input.Apply(MakeMouseButtonEvent(EventType::MouseButtonUp, MouseButton::Left));

    EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    EXPECT_TRUE(input.WasMouseButtonReleased(MouseButton::Left));
}

TEST(InputStateTest, MouseWheel_AccumulatesAcrossEvents_AndResetsOnBeginFrame)
{
    InputState input;
    input.Apply(MakeWheelEvent(1.0f, 2.0f));
    input.Apply(MakeWheelEvent(0.5f, -1.0f));

    EXPECT_FLOAT_EQ(input.MouseWheelDeltaX(), 1.5f);
    EXPECT_FLOAT_EQ(input.MouseWheelDeltaY(), 1.0f);

    input.BeginFrame();
    EXPECT_EQ(input.MouseWheelDeltaX(), 0.0f);
    EXPECT_EQ(input.MouseWheelDeltaY(), 0.0f);
}

TEST(InputStateTest, QuitAndWindowResizedEvents_AreIgnoredWithoutDisturbingOtherState)
{
    InputState input;
    input.Apply(MakeKeyEvent(EventType::KeyDown, KeyCode::W)); // establish some state first.

    Event quit;
    quit.type = EventType::Quit;
    input.Apply(quit); // must not crash or alter tracked key/mouse state.

    Event resized;
    resized.type = EventType::WindowResized;
    resized.data = WindowResizedEventData{ 1920, 1080 };
    input.Apply(resized); // same - discrete events are handled elsewhere (Game::OnEvent), not here.

    EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
}

} // namespace
} // namespace gte
