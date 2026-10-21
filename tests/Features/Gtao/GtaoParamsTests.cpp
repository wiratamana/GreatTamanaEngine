// Layout and conversion tests for the Gtao GPU parameter blocks.

#include "Features/Gtao/GtaoMath.h"
#include "Features/Gtao/GtaoTypes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace gte {
namespace {

fx::CameraBasis MakeBasis()
{
    fx::CameraBasis b;
    b.valid = true;
    b.eye = Vec3{ 1.0f, 2.0f, 3.0f };
    b.right = Vec3{ 1.0f, 0.0f, 0.0f };
    b.down = Vec3{ 0.0f, -1.0f, 0.0f };
    b.forward = Vec3{ 0.0f, 0.0f, 1.0f };
    b.pixelScale = 0.002f;
    return b;
}

TEST(GtaoParamsTest, MainBlockLayoutMatchesStd430)
{
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, invViewProjection), 0u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, eyeAndPixelScale), 64u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, rightAndRadius), 80u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, downAndFalloffMul), 96u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, forwardAndFalloffAdd), 112u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, tuning), 128u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, distanceFade), 144u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, counts), 160u);
    EXPECT_EQ(offsetof(GtaoMainParamsGpu, reserved), 176u);
    EXPECT_EQ(sizeof(GtaoMainParamsGpu), 192u);
}

TEST(GtaoParamsTest, CompositePushLayout)
{
    EXPECT_EQ(offsetof(GtaoCompositePush, eye), 64u);
    EXPECT_EQ(offsetof(GtaoCompositePush, strengthReservedSigma), 96u);
    EXPECT_EQ(offsetof(GtaoCompositePush, divisorDebug), 112u);
    EXPECT_EQ(sizeof(GtaoCompositePush), 128u);
}

TEST(GtaoParamsTest, MainParamsCarryCameraAndSettings)
{
    GtaoSettings cfg;
    cfg.radius = 1.5f;
    cfg.sliceCount = 3;
    cfg.stepCount = 5;
    cfg.finalPower = 2.0f;
    cfg.resolutionDivisor = 1;
    const GtaoSettings safe = SanitizeGtaoSettings(cfg);
    const Mat4 inverse = Mat4::Translation(Vec3{ 4.0f, 5.0f, 6.0f });

    const GtaoMainParamsGpu p = MakeGtaoMainParams(MakeBasis(), safe, inverse, 7);

    EXPECT_TRUE(ApproximatelyEqual(p.invViewProjection, inverse));
    EXPECT_FLOAT_EQ(p.eyeAndPixelScale[1], 2.0f);
    EXPECT_FLOAT_EQ(p.eyeAndPixelScale[3], 0.002f);
    EXPECT_FLOAT_EQ(p.rightAndRadius[0], 1.0f);
    EXPECT_FLOAT_EQ(p.rightAndRadius[3], 1.5f);
    EXPECT_FLOAT_EQ(p.downAndFalloffMul[1], -1.0f);
    EXPECT_FLOAT_EQ(p.forwardAndFalloffAdd[2], 1.0f);
    EXPECT_FLOAT_EQ(p.tuning[0], 2.0f);
    EXPECT_FLOAT_EQ(p.distanceFade[2], kGtaoMaxScreenRadiusPixels);
    EXPECT_EQ(p.counts[0], 3u);
    EXPECT_EQ(p.counts[1], 5u);
    EXPECT_EQ(p.counts[2], 7u);
    EXPECT_EQ(p.counts[3], 1u);
}

TEST(GtaoParamsTest, DistanceFadeKeepsDistinctEdgesForZeroFraction)
{
    GtaoSettings cfg;
    cfg.distanceFadeFraction = 0.0f;
    const GtaoMainParamsGpu p = MakeGtaoMainParams(MakeBasis(), SanitizeGtaoSettings(cfg), Mat4::Identity(), 0);
    EXPECT_LT(p.distanceFade[0], p.distanceFade[1]);
}

TEST(GtaoParamsTest, CompositePushCarriesStrengthModeAndDivisor)
{
    GtaoSettings cfg;
    cfg.strength = 0.4f;
    cfg.debugView = GtaoDebugView::AoOnly;

    const GtaoCompositePush p = MakeGtaoCompositePush(MakeBasis(), SanitizeGtaoSettings(cfg), Mat4::Identity(), 2);

    EXPECT_FLOAT_EQ(p.strengthReservedSigma[0], 0.4f);
    EXPECT_FLOAT_EQ(p.strengthReservedSigma[1], 0.0f);
    EXPECT_EQ(p.divisorDebug[0], 2u);
    EXPECT_EQ(p.divisorDebug[1], 1u);
    EXPECT_FLOAT_EQ(p.forward[2], 1.0f);
}

} // namespace
} // namespace gte
