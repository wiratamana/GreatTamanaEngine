// Unit tests for Vec3 (src/Math/Vec3.h) - basic arithmetic/geometric
// operations, using exact hand-computed expected values. CrossOfRightAndUp*
// below double as the defining sanity check for this engine's left-handed,
// Y-up/Z-forward/X-right coordinate system (see Vec3.h).

#include "Math/Vec3.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(Vec3Tests, DotProductOfPerpendicularAxesIsZero)
{
    EXPECT_FLOAT_EQ(Dot(Vec3::Right(), Vec3::Up()), 0.0f);
    EXPECT_FLOAT_EQ(Dot(Vec3::Right(), Vec3::Forward()), 0.0f);
}

TEST(Vec3Tests, DotProductOfParallelUnitAxesIsOne)
{
    EXPECT_FLOAT_EQ(Dot(Vec3::Up(), Vec3::Up()), 1.0f);
}

// Cross(Right, Up) == Forward matches Unity's
// Vector3.Cross(Vector3.right, Vector3.up) == Vector3.forward exactly.
TEST(Vec3Tests, CrossOfRightAndUpIsForward)
{
    Vec3 result = Cross(Vec3::Right(), Vec3::Up());
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3::Forward()));
}

TEST(Vec3Tests, CrossOfUpAndForwardIsRight)
{
    Vec3 result = Cross(Vec3::Up(), Vec3::Forward());
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3::Right()));
}

TEST(Vec3Tests, LengthOfUnitAxisIsOne)
{
    EXPECT_FLOAT_EQ(Length(Vec3::Right()), 1.0f);
}

TEST(Vec3Tests, LengthMatchesPythagorean345Triangle)
{
    Vec3 v(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(Length(v), 5.0f);
}

TEST(Vec3Tests, NormalizeProducesUnitLength)
{
    Vec3 v(3.0f, 4.0f, 0.0f);
    Vec3 n = Normalize(v);
    EXPECT_NEAR(Length(n), 1.0f, 1e-5f);
    EXPECT_TRUE(ApproximatelyEqual(n, Vec3(0.6f, 0.8f, 0.0f)));
}

TEST(Vec3Tests, NormalizeOfZeroVectorIsSafeAndReturnsZero)
{
    Vec3 n = Normalize(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(n, Vec3::Zero()));
}

TEST(Vec3Tests, LerpAtZeroAndOneReturnsEndpoints)
{
    Vec3 a(0.0f, 0.0f, 0.0f);
    Vec3 b(10.0f, 20.0f, 30.0f);
    EXPECT_TRUE(ApproximatelyEqual(Lerp(a, b, 0.0f), a));
    EXPECT_TRUE(ApproximatelyEqual(Lerp(a, b, 1.0f), b));
    EXPECT_TRUE(ApproximatelyEqual(Lerp(a, b, 0.5f), Vec3(5.0f, 10.0f, 15.0f)));
}

TEST(Vec3Tests, OperatorArithmetic)
{
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    EXPECT_TRUE(ApproximatelyEqual(a + b, Vec3(5.0f, 7.0f, 9.0f)));
    EXPECT_TRUE(ApproximatelyEqual(b - a, Vec3(3.0f, 3.0f, 3.0f)));
    EXPECT_TRUE(ApproximatelyEqual(a * 2.0f, Vec3(2.0f, 4.0f, 6.0f)));
    EXPECT_TRUE(ApproximatelyEqual(-a, Vec3(-1.0f, -2.0f, -3.0f)));
}

} // namespace
} // namespace gte
