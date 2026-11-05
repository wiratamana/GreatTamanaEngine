// The SSE2 Mat4 products must equal the scalar column formula bit for bit.

#include "Math/Mat4.h"

#include <gtest/gtest.h>

#include <cstring>
#include <random>

namespace gte {
namespace {

Vec4 ScalarMul(const Mat4& m, const Vec4& v)
{
    return Vec4(
        m.columns[0].x * v.x + m.columns[1].x * v.y + m.columns[2].x * v.z + m.columns[3].x * v.w,
        m.columns[0].y * v.x + m.columns[1].y * v.y + m.columns[2].y * v.z + m.columns[3].y * v.w,
        m.columns[0].z * v.x + m.columns[1].z * v.y + m.columns[2].z * v.z + m.columns[3].z * v.w,
        m.columns[0].w * v.x + m.columns[1].w * v.y + m.columns[2].w * v.z + m.columns[3].w * v.w);
}

Mat4 RandomMatrix(std::mt19937& rng)
{
    std::uniform_real_distribution<float> u(-100.0f, 100.0f);
    Mat4 m;
    for (int c = 0; c < 4; ++c) {
        // Sprinkle exact zeros of both signs: the hard case for sign-of-zero.
        const auto pick = [&]() {
            const unsigned k = rng() % 8;
            return k == 0 ? 0.0f : (k == 1 ? -0.0f : u(rng));
        };
        m.columns[c] = Vec4(pick(), pick(), pick(), pick());
    }
    return m;
}

} // namespace

TEST(Mat4SimdParityTests, MatrixTimesVectorMatchesTheScalarFormulaBitForBit)
{
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        const Mat4 m = RandomMatrix(rng);
        const Mat4 vm = RandomMatrix(rng);
        const Vec4 v = vm.columns[0];
        const Vec4 simd = m * v;
        const Vec4 scalar = ScalarMul(m, v);
        ASSERT_EQ(std::memcmp(&simd, &scalar, sizeof(Vec4)), 0);
    }
}

TEST(Mat4SimdParityTests, MatrixTimesMatrixMatchesTheScalarFormulaBitForBit)
{
    std::mt19937 rng(12);
    for (int i = 0; i < 10000; ++i) {
        const Mat4 a = RandomMatrix(rng);
        const Mat4 b = RandomMatrix(rng);
        const Mat4 simd = a * b;
        const Mat4 scalar(ScalarMul(a, b.columns[0]), ScalarMul(a, b.columns[1]), ScalarMul(a, b.columns[2]), ScalarMul(a, b.columns[3]));
        ASSERT_EQ(std::memcmp(&simd, &scalar, sizeof(Mat4)), 0);
    }
}

TEST(Mat4SimdParityTests, TransformPointAndVectorKeepTheirWeights)
{
    const Mat4 t = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(ApproximatelyEqual(t.TransformPoint(Vec3(1.0f, 1.0f, 1.0f)), Vec3(2.0f, 3.0f, 4.0f)));
    EXPECT_TRUE(ApproximatelyEqual(t.TransformVector(Vec3(1.0f, 1.0f, 1.0f)), Vec3(1.0f, 1.0f, 1.0f)));
}

} // namespace gte
