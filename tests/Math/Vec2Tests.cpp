// Unit tests for Vec2 (src/Math/Vec2.h) - basic arithmetic/geometric
// operations, using exact hand-computed expected values.

#include "Math/Vec2.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(Vec2Tests, DotProductOfPerpendicularUnitVectorsIsZero)
{
    EXPECT_FLOAT_EQ(Dot(Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f)), 0.0f);
}

TEST(Vec2Tests, LengthMatchesPythagorean345Triangle)
{
    EXPECT_FLOAT_EQ(Length(Vec2(3.0f, 4.0f)), 5.0f);
}

TEST(Vec2Tests, NormalizeProducesUnitLength)
{
    Vec2 n = Normalize(Vec2(3.0f, 4.0f));
    EXPECT_TRUE(ApproximatelyEqual(n, Vec2(0.6f, 0.8f)));
}

TEST(Vec2Tests, NormalizeOfZeroVectorIsSafeAndReturnsZero)
{
    EXPECT_TRUE(ApproximatelyEqual(Normalize(Vec2::Zero()), Vec2::Zero()));
}

TEST(Vec2Tests, LerpAtHalfwayAveragesComponents)
{
    Vec2 result = Lerp(Vec2(0.0f, 0.0f), Vec2(10.0f, 20.0f), 0.5f);
    EXPECT_TRUE(ApproximatelyEqual(result, Vec2(5.0f, 10.0f)));
}

TEST(Vec2Tests, OperatorArithmetic)
{
    Vec2 a(1.0f, 2.0f);
    Vec2 b(3.0f, 4.0f);
    EXPECT_TRUE(ApproximatelyEqual(a + b, Vec2(4.0f, 6.0f)));
    EXPECT_TRUE(ApproximatelyEqual(b - a, Vec2(2.0f, 2.0f)));
    EXPECT_TRUE(ApproximatelyEqual(a * 2.0f, Vec2(2.0f, 4.0f)));
}

} // namespace
} // namespace gte
