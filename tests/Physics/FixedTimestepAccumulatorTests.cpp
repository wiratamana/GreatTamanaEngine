#include "Physics/FixedTimestepAccumulator.h"

#include <gtest/gtest.h>

using namespace gte;

TEST(FixedTimestepAccumulatorTests, NormalSixtyFpsDeltaYieldsExactlyOneStep)
{
    float accumulated = 0.0f;
    const int steps = ComputeFixedStepCount(accumulated, 1.0f / 60.0f, 1.0f / 60.0f, 4);

    EXPECT_EQ(steps, 1);
    EXPECT_NEAR(accumulated, 0.0f, 0.0001f);
}

TEST(FixedTimestepAccumulatorTests, DeltaOfExactlyTwoTimestepsYieldsTwoStepsAndZeroLeftover)
{
    float accumulated = 0.0f;
    const int steps = ComputeFixedStepCount(accumulated, 2.0f / 60.0f, 1.0f / 60.0f, 4);

    EXPECT_EQ(steps, 2);
    EXPECT_NEAR(accumulated, 0.0f, 0.0001f);
}

TEST(FixedTimestepAccumulatorTests, HugeDeltaIsClampedToMaxStepsPerFrameAndLeftoverIsBounded)
{
    float accumulated = 0.0f;
    const int steps = ComputeFixedStepCount(accumulated, 5.0f, 1.0f / 60.0f, 4);

    EXPECT_EQ(steps, 4);
    // Spiral-of-death guard: leftover time must never exceed one fixed
    // timestep, no matter how large the input delta was.
    EXPECT_LE(accumulated, 1.0f / 60.0f);
    EXPECT_GE(accumulated, 0.0f);
}

TEST(FixedTimestepAccumulatorTests, DeltaSmallerThanTimestepYieldsZeroStepsAndAccumulates)
{
    float accumulated = 0.0f;
    const int steps = ComputeFixedStepCount(accumulated, 0.001f, 1.0f / 60.0f, 4);

    EXPECT_EQ(steps, 0);
    EXPECT_NEAR(accumulated, 0.001f, 0.0001f);
}

TEST(FixedTimestepAccumulatorTests, AccumulatesAcrossMultipleCallsUntilAStepFires)
{
    float accumulated = 0.0f;
    const float smallDelta = (1.0f / 60.0f) * 0.5f;

    EXPECT_EQ(ComputeFixedStepCount(accumulated, smallDelta, 1.0f / 60.0f, 4), 0);
    EXPECT_EQ(ComputeFixedStepCount(accumulated, smallDelta, 1.0f / 60.0f, 4), 1);
}

TEST(FixedTimestepAccumulatorTests, NonPositiveFixedTimestepReturnsZeroStepsAndNeverDividesByZero)
{
    float accumulated = 0.0f;
    const int steps = ComputeFixedStepCount(accumulated, 1.0f / 60.0f, 0.0f, 4);

    EXPECT_EQ(steps, 0);
}
