// Unit tests for gte::Time (src/Core/Time.h/.cpp) - pure Tier-1 logic, no
// ECS/GPU/SDL/ImGui involved at all. See
// task_manager/frame-debugger-1/PHASE1_CORE_TIME_CLASS_AND_ENGINE_CONTEXT.md,
// "3.5 New test file" for the exact case list these tests implement.

#include "Core/Time.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

constexpr double kFixedStep = 1.0 / 60.0;

TEST(TimeTest, DefaultConstructedTimeReadsAllZeroFalseDefaults)
{
    const Time time;

    EXPECT_DOUBLE_EQ(time.DeltaTime(), 0.0);
    EXPECT_DOUBLE_EQ(time.UnscaledDeltaTime(), 0.0);
    EXPECT_FALSE(time.IsPaused());
    EXPECT_FALSE(time.IsFrozenThisFrame());
    EXPECT_FALSE(time.IsSteppedThisFrame());
    EXPECT_DOUBLE_EQ(time.TimeSinceStartupSeconds(), 0.0);
    EXPECT_DOUBLE_EQ(time.SimulatedTimeSeconds(), 0.0);
    EXPECT_EQ(time.FrameCount(), 0u);
}

TEST(TimeTest, NormalRunningFrameUsesRealDelta)
{
    Time time;

    time.Advance(kFixedStep, /*isPaused=*/false, /*isSteppedThisFrame=*/false, kFixedStep);

    EXPECT_DOUBLE_EQ(time.DeltaTime(), kFixedStep);
    EXPECT_DOUBLE_EQ(time.UnscaledDeltaTime(), kFixedStep);
    EXPECT_FALSE(time.IsPaused());
    EXPECT_FALSE(time.IsFrozenThisFrame());
    EXPECT_FALSE(time.IsSteppedThisFrame());
}

TEST(TimeTest, PausedNotSteppingFreezesDeltaButKeepsUnscaledReal)
{
    Time time;
    time.Advance(kFixedStep, false, false, kFixedStep);

    time.Advance(kFixedStep, /*isPaused=*/true, /*isSteppedThisFrame=*/false, kFixedStep);

    EXPECT_DOUBLE_EQ(time.DeltaTime(), 0.0);
    EXPECT_DOUBLE_EQ(time.UnscaledDeltaTime(), kFixedStep);
    EXPECT_TRUE(time.IsPaused());
    EXPECT_TRUE(time.IsFrozenThisFrame());
    EXPECT_FALSE(time.IsSteppedThisFrame());
}

TEST(TimeTest, PausedAndSteppedUsesFixedStepRegardlessOfRealDelta)
{
    Time time;

    // Deliberately pass a real delta wildly different from the fixed step
    // to prove Step is fully deterministic, never derived from wall-clock
    // time (Locked Design Decision #4).
    time.Advance(/*realDeltaSeconds=*/2.5, /*isPaused=*/true, /*isSteppedThisFrame=*/true, kFixedStep);

    EXPECT_DOUBLE_EQ(time.DeltaTime(), kFixedStep);
    EXPECT_TRUE(time.IsPaused());
    EXPECT_FALSE(time.IsFrozenThisFrame());
    EXPECT_TRUE(time.IsSteppedThisFrame());
}

TEST(TimeTest, SteppedWhileNotPausedIsHarmlessAndBehavesLikeNormalFrame)
{
    Time time;
    constexpr double kRealDelta = 0.1234;

    time.Advance(kRealDelta, /*isPaused=*/false, /*isSteppedThisFrame=*/true, kFixedStep);

    EXPECT_DOUBLE_EQ(time.DeltaTime(), kRealDelta);
    EXPECT_FALSE(time.IsSteppedThisFrame());
    EXPECT_FALSE(time.IsFrozenThisFrame());
    EXPECT_FALSE(time.IsPaused());
}

TEST(TimeTest, ResumeAfterLongPauseClampsExactlyOneFrameToFixedStep)
{
    Time time;

    // One normal frame.
    time.Advance(kFixedStep, false, false, kFixedStep);
    // A long simulated pause.
    time.Advance(5.0, true, false, kFixedStep);
    // Resume: 5 real seconds elapsed since the last check (e.g. the user
    // took a while to click Resume) - this frame must clamp to the fixed
    // step, NOT replay the full 5 seconds as one giant catch-up step
    // (Locked Design Decision #11).
    time.Advance(5.0, false, false, kFixedStep);

    EXPECT_DOUBLE_EQ(time.DeltaTime(), kFixedStep);
    EXPECT_FALSE(time.IsPaused());

    // The very next ordinary frame after resuming must go back to using
    // the real delta verbatim - the clamp applies to exactly one frame.
    time.Advance(kFixedStep, false, false, kFixedStep);

    EXPECT_DOUBLE_EQ(time.DeltaTime(), kFixedStep);
}

TEST(TimeTest, TimeSinceStartupNeverFreezesRegardlessOfPauseState)
{
    Time time;

    time.Advance(1.0, false, false, kFixedStep);       // running
    time.Advance(2.0, true, false, kFixedStep);        // paused, not stepping
    time.Advance(3.0, true, true, kFixedStep);         // paused + stepped
    time.Advance(4.0, false, false, kFixedStep);        // resume frame

    EXPECT_DOUBLE_EQ(time.TimeSinceStartupSeconds(), 1.0 + 2.0 + 3.0 + 4.0);
}

TEST(TimeTest, SimulatedTimeFreezesOnlyWhilePausedAndNotStepping)
{
    Time time;

    time.Advance(1.0, false, false, kFixedStep);   // running: deltaTime == 1.0
    double expectedSimulated = 1.0;
    EXPECT_DOUBLE_EQ(time.SimulatedTimeSeconds(), expectedSimulated);

    time.Advance(2.0, true, false, kFixedStep);    // frozen: deltaTime == 0.0
    EXPECT_DOUBLE_EQ(time.SimulatedTimeSeconds(), expectedSimulated);

    time.Advance(3.0, true, true, kFixedStep);     // stepped: deltaTime == kFixedStep
    expectedSimulated += kFixedStep;
    EXPECT_DOUBLE_EQ(time.SimulatedTimeSeconds(), expectedSimulated);

    time.Advance(4.0, false, false, kFixedStep);   // resume frame: clamped to kFixedStep
    expectedSimulated += kFixedStep;
    EXPECT_DOUBLE_EQ(time.SimulatedTimeSeconds(), expectedSimulated);
}

TEST(TimeTest, FrameCountAlwaysIncrementsEvenWhileFrozen)
{
    Time time;

    time.Advance(1.0, false, false, kFixedStep);
    EXPECT_EQ(time.FrameCount(), 1u);

    time.Advance(2.0, true, false, kFixedStep);
    EXPECT_EQ(time.FrameCount(), 2u);

    time.Advance(3.0, true, true, kFixedStep);
    EXPECT_EQ(time.FrameCount(), 3u);

    time.Advance(4.0, false, false, kFixedStep);
    EXPECT_EQ(time.FrameCount(), 4u);
}

} // namespace
} // namespace gte
