// Unit tests for ComputeWindAcceleration (src/Physics/WindField.h) - the
// procedural, deterministic global wind field. No ECS/GPU/Renderer/RNG
// involved.

#include "Physics/WindField.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(WindFieldTests, SameInputsAlwaysProduceBitIdenticalOutput)
{
    WindSettings settings;
    settings.direction = Vec3::Right();
    settings.baseStrength = 2.0f;
    settings.gustStrength = 1.0f;
    settings.gustFrequency = 0.75f;
    settings.seedOffset = 0.3f;

    Vec3 worldPosition(1.0f, 2.0f, 3.0f);
    float timeSeconds = 4.2f;

    Vec3 a = ComputeWindAcceleration(settings, worldPosition, timeSeconds);
    Vec3 b = ComputeWindAcceleration(settings, worldPosition, timeSeconds);

    EXPECT_EQ(a.x, b.x);
    EXPECT_EQ(a.y, b.y);
    EXPECT_EQ(a.z, b.z);
}

TEST(WindFieldTests, BaseStrengthAloneWithZeroGustProducesConstantVectorRegardlessOfTime)
{
    WindSettings settings;
    settings.direction = Vec3::Forward();
    settings.baseStrength = 3.0f;
    settings.gustStrength = 0.0f; // zero amplitude - time/phase can't matter

    Vec3 worldPosition = Vec3::Zero();

    Vec3 atT0 = ComputeWindAcceleration(settings, worldPosition, 0.0f);
    Vec3 atT10 = ComputeWindAcceleration(settings, worldPosition, 10.0f);
    Vec3 atT123 = ComputeWindAcceleration(settings, worldPosition, 123.456f);

    EXPECT_TRUE(ApproximatelyEqual(atT0, Vec3::Forward() * 3.0f));
    EXPECT_TRUE(ApproximatelyEqual(atT10, Vec3::Forward() * 3.0f));
    EXPECT_TRUE(ApproximatelyEqual(atT123, Vec3::Forward() * 3.0f));
}

TEST(WindFieldTests, DifferentWorldPositionsWithGustProduceMeasurablyDifferentResults)
{
    WindSettings settings;
    settings.direction = Vec3::Right();
    settings.baseStrength = 0.0f;
    settings.gustStrength = 1.0f;
    settings.gustFrequency = 1.0f;

    Vec3 resultA = ComputeWindAcceleration(settings, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    Vec3 resultB = ComputeWindAcceleration(settings, Vec3(100.0f, 50.0f, 25.0f), 1.0f);

    // The spatial-hash term must actually shift the phase - proves it's
    // wired in, not a dead parameter.
    EXPECT_FALSE(ApproximatelyEqual(resultA, resultB));
}

} // namespace
} // namespace gte
