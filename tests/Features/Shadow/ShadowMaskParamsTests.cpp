// Layout and conversion tests for the shadow mask GPU parameter block.

#include "Features/Shadow/ShadowMath.h"
#include "Features/Shadow/ShadowTypes.h"

#include <gtest/gtest.h>

#include <cstddef>

namespace gte {
namespace {

TEST(ShadowMaskParamsTest, LayoutMatchesStd430Block)
{
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, invViewProjection), 0u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, lightViewProjection), 64u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, depthBias), 128u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, unused), 132u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, shadowTexelSize), 136u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, pad), 140u);
    EXPECT_EQ(sizeof(ShadowMaskParamsGpu), 144u);
}

TEST(ShadowMaskParamsTest, ZeroResolutionGivesZeroTexelSize)
{
    EXPECT_EQ(MakeShadowMaskParams(Mat4::Identity(), Mat4::Identity(), 0.0f, 0).shadowTexelSize, 0.0f);
}

TEST(ShadowMaskParamsTest, TexelSizeIsReciprocalOfResolution)
{
    EXPECT_FLOAT_EQ(MakeShadowMaskParams(Mat4::Identity(), Mat4::Identity(), 0.0f, 2048).shadowTexelSize, 1.0f / 2048.0f);
}

TEST(ShadowMaskParamsTest, CopiesMatricesAndBias)
{
    const Mat4 inverse = Mat4::Translation(Vec3{ 1.0f, 2.0f, 3.0f });
    const Mat4 light = Mat4::Scaling(Vec3{ 2.0f, 3.0f, 4.0f });

    const ShadowMaskParamsGpu params = MakeShadowMaskParams(inverse, light, 0.005f, 1024);

    EXPECT_TRUE(ApproximatelyEqual(params.invViewProjection, inverse));
    EXPECT_TRUE(ApproximatelyEqual(params.lightViewProjection, light));
    EXPECT_FLOAT_EQ(params.depthBias, 0.005f);
    EXPECT_EQ(params.unused, 0.0f);
    EXPECT_EQ(params.pad, 0.0f);
}

} // namespace
} // namespace gte
