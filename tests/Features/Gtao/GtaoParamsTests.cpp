// Layout and conversion tests for the Gtao GPU parameter blocks.

#include "Features/Gtao/GtaoMath.h"
#include "Features/Gtao/GtaoTypes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace gte {
namespace {

GtaoCameraBasis MakeBasis()
{
    GtaoCameraBasis b;
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

TEST(GtaoParamsTest, ResolveAndCompositeBlockLayout)
{
    EXPECT_EQ(offsetof(GtaoResolveParamsGpu, prevViewProjection), 64u);
    EXPECT_EQ(offsetof(GtaoResolveParamsGpu, eye), 128u);
    EXPECT_EQ(offsetof(GtaoResolveParamsGpu, filter), 192u);
    EXPECT_EQ(offsetof(GtaoResolveParamsGpu, counts), 208u);
    EXPECT_EQ(sizeof(GtaoResolveParamsGpu), 224u);

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

TEST(GtaoParamsTest, ResolveParamsCarryHistoryAndExtent)
{
    GtaoHistory previous;
    previous.viewProjection = Mat4::Translation(Vec3{ 1.0f, 0.0f, 0.0f });
    previous.eye = Vec3{ 9.0f, 8.0f, 7.0f };
    previous.forward = Vec3{ 0.0f, 1.0f, 0.0f };
    GtaoSettings cfg;
    cfg.spatialDenoise = false;
    cfg.historyWeight = 0.5f;

    const GtaoResolveParamsGpu p = MakeGtaoResolveParams(
        MakeBasis(), previous, SanitizeGtaoSettings(cfg), Extent2D{ 1280, 720 }, Mat4::Identity(), true);

    EXPECT_TRUE(ApproximatelyEqual(p.prevViewProjection, previous.viewProjection));
    EXPECT_FLOAT_EQ(p.eye[2], 3.0f);
    EXPECT_FLOAT_EQ(p.prevEye[0], 9.0f);
    EXPECT_FLOAT_EQ(p.prevForward[1], 1.0f);
    EXPECT_FLOAT_EQ(p.filter[1], 0.5f);
    EXPECT_FLOAT_EQ(p.filter[3], 0.0f);
    EXPECT_EQ(p.counts[1], 1u);
    EXPECT_EQ(p.counts[2], 1280u);
    EXPECT_EQ(p.counts[3], 720u);
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
