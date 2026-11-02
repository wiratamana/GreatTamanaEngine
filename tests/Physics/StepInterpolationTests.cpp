// Unit tests for InterpolateToAnchor (src/Physics/StepInterpolation.h).

#include "Physics/StepInterpolation.h"

#include "Math/Quat.h"
#include <gtest/gtest.h>

#include <limits>

namespace gte {

TEST(StepInterpolationTests, AlphaSelectsEndpointsAndMidpoint)
{
    const Mat4 anchor = Mat4::Translation(Vec3(10.0f, 0.0f, 0.0f));
    const Vec3 prev(0.0f, 0.0f, 0.0f);
    const Vec3 curr(2.0f, 4.0f, 0.0f);

    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, 0.0f), Vec3(10.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, 1.0f), Vec3(12.0f, 4.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, 0.5f), Vec3(11.0f, 2.0f, 0.0f)));
}

TEST(StepInterpolationTests, AlphaIsClamped)
{
    const Mat4 anchor = Mat4::Identity();
    const Vec3 prev(0.0f, 0.0f, 0.0f);
    const Vec3 curr(2.0f, 0.0f, 0.0f);

    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, -1.0f), prev));
    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, 2.0f), curr));
    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, std::numeric_limits<float>::quiet_NaN()), prev));
}

TEST(StepInterpolationTests, RotatedAndTranslatedAnchorTransformsTheResult)
{
    // The result must follow the full anchor matrix, not only its translation.
    const Mat4 anchor = Mat4::TRS(Vec3(1.0f, 2.0f, 3.0f), Quat::FromEulerDegrees(0.0f, 90.0f, 0.0f), Vec3::One());
    const Vec3 prev(1.0f, 0.0f, 0.0f);
    const Vec3 curr(1.0f, 0.0f, 0.0f);

    const Vec3 expected = anchor.TransformPoint(Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(InterpolateToAnchor(anchor, prev, curr, 0.3f), expected, 1e-5f));
    EXPECT_FALSE(ApproximatelyEqual(expected, Vec3(2.0f, 2.0f, 3.0f), 1e-3f));
}

TEST(StepInterpolationTests, BlendAnchorFramesIsExactForTranslationsAndClamped)
{
    const Mat4 from = Mat4::Translation(Vec3(0.0f, 0.0f, 0.0f));
    const Mat4 to = Mat4::Translation(Vec3(4.0f, 2.0f, 0.0f));

    EXPECT_TRUE(ApproximatelyEqual(BlendAnchorFrames(from, to, 0.25f).TransformPoint(Vec3::Zero()), Vec3(1.0f, 0.5f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(BlendAnchorFrames(from, to, -1.0f), from));
    EXPECT_TRUE(ApproximatelyEqual(BlendAnchorFrames(from, to, 2.0f), to));
    EXPECT_TRUE(ApproximatelyEqual(BlendAnchorFrames(from, to, std::numeric_limits<float>::quiet_NaN()), from));
}

TEST(StepInterpolationTests, BlendAnchorFramesOfASmallRotationStaysRigid)
{
    const Mat4 from = Mat4::TRS(Vec3::Zero(), Quat::FromEulerDegrees(0.0f, 0.0f, 0.0f), Vec3::One());
    const Mat4 to = Mat4::TRS(Vec3(1.0f, 0.0f, 0.0f), Quat::FromEulerDegrees(0.0f, 4.0f, 0.0f), Vec3::One());
    const Mat4 half = BlendAnchorFrames(from, to, 0.5f);

    // A unit vector keeps its length to better than 0.1 percent.
    EXPECT_NEAR(Length(half.TransformVector(Vec3(0.0f, 0.0f, 1.0f))), 1.0f, 1e-3f);
    Mat4 inverse;
    ASSERT_TRUE(half.TryInverse(inverse));
    EXPECT_TRUE(ApproximatelyEqual(inverse.TransformPoint(half.TransformPoint(Vec3(3.0f, 2.0f, 1.0f))), Vec3(3.0f, 2.0f, 1.0f), 1e-4f));
}

} // namespace gte
