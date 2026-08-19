// Unit tests for Mat4 (src/Math/Mat4.h). Where possible these use exact,
// hand-verified expected values (see comments per test) rather than
// comparing against a second math library - see src/Math/MathTypes.h for
// why that was judged an acceptable risk tradeoff for this from-scratch
// implementation.

#include "Math/Mat4.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(Mat4Tests, IdentityLeavesPointsUnchanged)
{
    Vec3 p(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(ApproximatelyEqual(Mat4::Identity().TransformPoint(p), p));
}

TEST(Mat4Tests, TranslationMovesAPoint)
{
    Mat4 t = Mat4::Translation(Vec3(10.0f, -5.0f, 2.0f));
    Vec3 result = t.TransformPoint(Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3(11.0f, -4.0f, 3.0f)));
}

TEST(Mat4Tests, TranslationDoesNotAffectDirections)
{
    Mat4 t = Mat4::Translation(Vec3(10.0f, -5.0f, 2.0f));
    Vec3 result = t.TransformVector(Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3(1.0f, 0.0f, 0.0f)));
}

TEST(Mat4Tests, ScalingScalesEachAxisIndependently)
{
    Mat4 s = Mat4::Scaling(Vec3(2.0f, 3.0f, 4.0f));
    Vec3 result = s.TransformPoint(Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3(2.0f, 3.0f, 4.0f)));
}

TEST(Mat4Tests, MultiplicationComposesRightToLeft)
{
    // (A * B) * v must equal A * (B * v) - the defining property of this
    // engine's composition convention (see Mat4.h).
    Mat4 a = Mat4::Translation(Vec3(1.0f, 0.0f, 0.0f));
    Mat4 b = Mat4::Scaling(Vec3(2.0f, 2.0f, 2.0f));
    Vec3 v(3.0f, 3.0f, 3.0f);

    Vec3 composed = (a * b).TransformPoint(v);
    Vec3 sequential = a.TransformPoint(b.TransformPoint(v));

    EXPECT_TRUE(ApproximatelyEqual(composed, sequential));
    // Scale first (6,6,6), then translate +1 on X -> (7,6,6).
    EXPECT_TRUE(ApproximatelyEqual(composed, Vec3(7.0f, 6.0f, 6.0f)));
}

TEST(Mat4Tests, TransposedSwapsOffDiagonalElements)
{
    Mat4 t = Mat4::Translation(Vec3(7.0f, 8.0f, 9.0f));
    Mat4 transposed = t.Transposed();

    EXPECT_FLOAT_EQ(t(0, 3), 7.0f);
    EXPECT_FLOAT_EQ(transposed(3, 0), 7.0f);
    EXPECT_FLOAT_EQ(transposed(0, 3), 0.0f);
}

TEST(Mat4Tests, TransposedTwiceReturnsTheOriginal)
{
    Mat4 m = Mat4::TRS(Vec3(1.0f, 2.0f, 3.0f), Quat::FromAxisAngle(Vec3::Up(), DegToRad(37.0f)), Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(ApproximatelyEqual(m.Transposed().Transposed(), m));
}

TEST(Mat4Tests, InverseOfTranslationUndoesIt)
{
    Mat4 t = Mat4::Translation(Vec3(5.0f, -2.0f, 9.0f));
    Mat4 roundTrip = t * t.Inverse();
    EXPECT_TRUE(ApproximatelyEqual(roundTrip, Mat4::Identity(), 1e-4f));
}

TEST(Mat4Tests, InverseOfArbitraryTRSRoundTrips)
{
    Mat4 m = Mat4::TRS(
        Vec3(3.0f, -1.0f, 2.0f),
        Quat::FromAxisAngle(Normalize(Vec3(1.0f, 1.0f, 0.0f)), DegToRad(52.0f)),
        Vec3(2.0f, 0.5f, 1.5f));

    Mat4 inv;
    ASSERT_TRUE(m.TryInverse(inv));
    EXPECT_TRUE(ApproximatelyEqual(m * inv, Mat4::Identity(), 1e-3f));
}

TEST(Mat4Tests, SingularMatrixHasNoInverse)
{
    Mat4 singular = Mat4::Scaling(Vec3(1.0f, 0.0f, 1.0f)); // collapses Y to zero
    Mat4 inv;
    EXPECT_FALSE(singular.TryInverse(inv));
}

// Eye at the origin, looking straight down +Z (this engine's forward) with
// +Y up, is the one camera pose where nothing needs correcting - the view
// matrix must come out as Identity() exactly.
TEST(Mat4Tests, LookAtLH_AtOriginLookingForwardIsIdentity)
{
    Mat4 view = Mat4::LookAtLH(Vec3::Zero(), Vec3::Forward(), Vec3::Up());
    EXPECT_TRUE(ApproximatelyEqual(view, Mat4::Identity()));
}

TEST(Mat4Tests, LookAtLH_TranslatesEyeIntoOrigin)
{
    Mat4 view = Mat4::LookAtLH(Vec3(0.0f, 0.0f, -5.0f), Vec3(0.0f, 0.0f, -4.0f), Vec3::Up());
    // Eye is 5 units behind the world origin along -Z, looking further
    // down +Z - the origin should end up 5 units in front of the camera
    // (+Z in view space) after transforming.
    Vec3 originInView = view.TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(originInView, Vec3(0.0f, 0.0f, 5.0f)));
}

// fovY=90deg, aspect=1 gives a clean tan(45deg)=1, so the expected
// clip-space values below are exact, hand-verifiable numbers.
TEST(Mat4Tests, PerspectiveFovLH_ZO_MapsNearAndFarPlaneDepthsTo0And1)
{
    Mat4 proj = Mat4::PerspectiveFovLH_ZO(kHalfPi, 1.0f, 0.1f, 100.0f);

    Vec4 nearPoint = proj * Vec4(0.0f, 0.0f, 0.1f, 1.0f);
    EXPECT_NEAR(nearPoint.z / nearPoint.w, 0.0f, 1e-4f);

    Vec4 farPoint = proj * Vec4(0.0f, 0.0f, 100.0f, 1.0f);
    EXPECT_NEAR(farPoint.z / farPoint.w, 1.0f, 1e-4f);
}

TEST(Mat4Tests, PerspectiveFovLH_ZO_MapsFovEdgeToClipSpaceBoundary)
{
    Mat4 proj = Mat4::PerspectiveFovLH_ZO(kHalfPi, 1.0f, 0.1f, 100.0f);

    // At depth z=10 with a 90-degree vertical FOV and 1:1 aspect, the
    // visible half-width/half-height is exactly z * tan(45deg) == z.
    Vec4 edge = proj * Vec4(10.0f, 10.0f, 10.0f, 1.0f);
    EXPECT_NEAR(edge.x / edge.w, 1.0f, 1e-4f);
    EXPECT_NEAR(edge.y / edge.w, 1.0f, 1e-4f);
}

TEST(Mat4Tests, PerspectiveFlipYNegatesTheYAxisOnly)
{
    Mat4 proj = Mat4::PerspectiveFovLH_ZO(kHalfPi, 1.0f, 0.1f, 100.0f);
    Mat4 flipped = Mat4::PerspectiveFovLH_ZO(kHalfPi, 1.0f, 0.1f, 100.0f, /*flipY=*/true);

    EXPECT_FLOAT_EQ(flipped(1, 1), -proj(1, 1));
    EXPECT_FLOAT_EQ(flipped(0, 0), proj(0, 0));
}

} // namespace
} // namespace gte
