// Unit tests for the double-precision matrix (src/Math/Mat4d.h).

#include "Math/Mat4d.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

Mat4 FarCameraViewProjection()
{
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    return Mat4::PerspectiveFovLH_ZO(1.0471976f, 1600.0f / 867.0f, 0.1f, 1000.0f, true)
        * Mat4::LookAtLH(eye, eye + Normalize(Vec3(0.3f, -0.1f, 1.0f)), Vec3::Up());
}

TEST(Mat4dTest, ConversionRoundTripKeepsEveryFloat)
{
    const Mat4 a = FarCameraViewProjection();
    const Mat4 b = Mat4d::FromMat4(a).ToMat4();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_EQ(a(r, c), b(r, c));
        }
    }
}

TEST(Mat4dTest, InverseTimesMatrixIsIdentityForACameraFarFromTheOrigin)
{
    const Mat4d a = Mat4d::FromMat4(FarCameraViewProjection());
    Mat4d inverse;
    ASSERT_TRUE(a.TryInverse(inverse));
    const Mat4d product = a * inverse;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(product.m[c * 4 + r], r == c ? 1.0 : 0.0, 1e-9) << r << "," << c;
        }
    }
}

TEST(Mat4dTest, SingularAndNonFiniteMatricesAreRejected)
{
    Mat4d out;
    EXPECT_FALSE(Mat4d{}.TryInverse(out));

    Mat4d nan = Mat4d::Identity();
    nan.m[5] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(nan.TryInverse(out));
    EXPECT_FALSE(nan.IsFinite());
}

TEST(Mat4dTest, TranslationsComposeAndMoveAPoint)
{
    const Mat4d t = Mat4d::Translation(1.0, 2.0, 3.0) * Mat4d::Translation(10.0, 20.0, 30.0);
    const double point[4] = { 1.0, 1.0, 1.0, 1.0 };
    double moved[4] = {};
    t.Transform(point, moved);
    EXPECT_DOUBLE_EQ(moved[0], 12.0);
    EXPECT_DOUBLE_EQ(moved[1], 23.0);
    EXPECT_DOUBLE_EQ(moved[2], 34.0);
    EXPECT_DOUBLE_EQ(moved[3], 1.0);
}

// Same convention as Mat4: `a * b` applies b first.
TEST(Mat4dTest, ProductMatchesMat4ForSmallNumbers)
{
    const Mat4 a = Mat4::PerspectiveFovLH_ZO(1.0f, 1.5f, 0.1f, 50.0f);
    const Mat4 b = Mat4::LookAtLH(Vec3(1.0f, 2.0f, -3.0f), Vec3::Zero(), Vec3::Up());
    const Mat4 expected = a * b;
    const Mat4 actual = (Mat4d::FromMat4(a) * Mat4d::FromMat4(b)).ToMat4();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(actual(r, c), expected(r, c), 1e-5f * (1.0f + std::abs(expected(r, c))));
        }
    }
}

} // namespace
} // namespace gte
