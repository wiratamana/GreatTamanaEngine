// Unit tests for ScaleStepFraction and PmxDampingToStepFraction (src/Physics/TimeScaling.h).

#include "Physics/TimeScaling.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {

TEST(TimeScalingTests, ReferenceStepIsExact)
{
    EXPECT_EQ(ScaleStepFraction(0.02f, 1.0f / 60.0f), 0.02f);
}

TEST(TimeScalingTests, HalfStepGivesSquareRootFraction)
{
    EXPECT_NEAR(ScaleStepFraction(0.02f, 1.0f / 120.0f), 1.0f - std::sqrt(0.98f), 1e-6f);
    EXPECT_NEAR(ScaleStepFraction(0.02f, 1.0f / 120.0f), 0.0100505f, 1e-6f);
}

TEST(TimeScalingTests, MultiplierActsAsExponent)
{
    EXPECT_NEAR(ScaleStepFraction(0.02f, 1.0f / 60.0f, 2.0f), 1.0f - 0.98f * 0.98f, 1e-6f);
    EXPECT_EQ(ScaleStepFraction(0.4f, 1.0f / 60.0f, 0.0f), 0.0f);
}

TEST(TimeScalingTests, FullAndZeroFractionsAreFixed)
{
    EXPECT_EQ(ScaleStepFraction(1.0f, 1.0f / 120.0f), 1.0f);
    EXPECT_EQ(ScaleStepFraction(1.0f, 1.0f / 30.0f), 1.0f);
    EXPECT_EQ(ScaleStepFraction(2.0f, 1.0f / 60.0f), 1.0f);
    EXPECT_EQ(ScaleStepFraction(0.0f, 1.0f / 120.0f), 0.0f);
    EXPECT_EQ(ScaleStepFraction(-0.5f, 1.0f / 60.0f), 0.0f);
}

TEST(TimeScalingTests, NanAndZeroStepGiveZero)
{
    EXPECT_EQ(ScaleStepFraction(std::numeric_limits<float>::quiet_NaN(), 1.0f / 60.0f), 0.0f);
    EXPECT_EQ(ScaleStepFraction(0.3f, 0.0f), 0.0f);
    EXPECT_EQ(ScaleStepFraction(0.3f, std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST(TimeScalingTests, TwoHalfStepsEqualOneReferenceStep)
{
    for (const float f : { 0.02f, 0.4f, 0.9f }) {
        const float f120 = ScaleStepFraction(f, 1.0f / 120.0f);
        EXPECT_NEAR((1.0f - f120) * (1.0f - f120), 1.0f - f, 1e-6f) << "f=" << f;
    }
}

TEST(TimeScalingTests, PmxDampingMapsToRatePerSecond)
{
    EXPECT_NEAR(PmxDampingToStepFraction(1.0f, 1.0f), 0.016529f, 1e-5f);
    EXPECT_NEAR(PmxDampingToStepFraction(0.99f, 1.0f), 0.016365f, 1e-5f);
    EXPECT_EQ(PmxDampingToStepFraction(0.0f, 5.0f), 0.0f);
    EXPECT_EQ(PmxDampingToStepFraction(1.0f, 0.0f), 0.0f);
    EXPECT_EQ(PmxDampingToStepFraction(std::numeric_limits<float>::quiet_NaN(), 1.0f), 0.0f);
}

} // namespace gte
