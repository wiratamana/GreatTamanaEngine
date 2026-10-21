// Layout and conversion tests for the Ssgi GPU parameter blocks.

#include "Features/Ssgi/SsgiMath.h"
#include "Features/Ssgi/SsgiTypes.h"

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

TEST(SsgiParamsTest, PrepareBlockLayoutMatchesStd430)
{
    EXPECT_EQ(offsetof(SsgiPrepareParamsGpu, invViewProjection), 0u);
    EXPECT_EQ(offsetof(SsgiPrepareParamsGpu, eye), 64u);
    EXPECT_EQ(offsetof(SsgiPrepareParamsGpu, forward), 80u);
    EXPECT_EQ(offsetof(SsgiPrepareParamsGpu, tuning), 96u);
    EXPECT_EQ(offsetof(SsgiPrepareParamsGpu, counts), 112u);
    EXPECT_EQ(sizeof(SsgiPrepareParamsGpu), 128u);
}

TEST(SsgiParamsTest, TraceBlockLayoutMatchesStd430)
{
    EXPECT_EQ(offsetof(SsgiTraceParamsGpu, rightAndMaxDistance), 0u);
    EXPECT_EQ(offsetof(SsgiTraceParamsGpu, downAndThickness), 16u);
    EXPECT_EQ(offsetof(SsgiTraceParamsGpu, forwardAndBias), 32u);
    EXPECT_EQ(offsetof(SsgiTraceParamsGpu, viewAndFade), 48u);
    EXPECT_EQ(offsetof(SsgiTraceParamsGpu, tuning), 64u);
    EXPECT_EQ(offsetof(SsgiTraceParamsGpu, counts), 80u);
    EXPECT_EQ(sizeof(SsgiTraceParamsGpu), 96u);
}

TEST(SsgiParamsTest, CompositePushLayout)
{
    EXPECT_EQ(offsetof(SsgiCompositePush, invViewProjection), 0u);
    EXPECT_EQ(offsetof(SsgiCompositePush, eye), 64u);
    EXPECT_EQ(offsetof(SsgiCompositePush, forward), 80u);
    EXPECT_EQ(offsetof(SsgiCompositePush, intensityAlbedo), 96u);
    EXPECT_EQ(offsetof(SsgiCompositePush, divisorDebug), 112u);
    EXPECT_EQ(sizeof(SsgiCompositePush), 128u);
}

TEST(SsgiParamsTest, PrepareParamsCarryCameraAndSettings)
{
    SsgiSettings cfg;
    cfg.maxRadiance = 12.0f;
    cfg.resolutionDivisor = 1;
    const Mat4 inverse = Mat4::Translation(Vec3{ 4.0f, 5.0f, 6.0f });

    const SsgiPrepareParamsGpu p = MakeSsgiPrepareParams(MakeBasis(), SanitizeSsgiSettings(cfg), inverse);

    EXPECT_TRUE(ApproximatelyEqual(p.invViewProjection, inverse));
    EXPECT_FLOAT_EQ(p.eye[0], 1.0f);
    EXPECT_FLOAT_EQ(p.eye[2], 3.0f);
    EXPECT_FLOAT_EQ(p.forward[2], 1.0f);
    EXPECT_FLOAT_EQ(p.tuning[0], 12.0f);
    EXPECT_EQ(p.counts[0], 1u);
}

TEST(SsgiParamsTest, TraceParamsCarryBasisSettingsAndCounts)
{
    SsgiSettings cfg;
    cfg.maxRayDistance = 6.0f;
    cfg.thickness = 0.4f;
    cfg.depthBias = 0.05f;
    cfg.stepDistributionPower = 3.0f;
    cfg.endFadeFraction = 0.2f;
    cfg.maxDistance = 50.0f;
    cfg.distanceFadeFraction = 0.5f;
    cfg.rayCount = 3;
    cfg.stepCount = 20;
    cfg.resolutionDivisor = 2;

    const SsgiTraceParamsGpu p = MakeSsgiTraceParams(MakeBasis(), SanitizeSsgiSettings(cfg), Extent2D{ 1280, 720 }, 9);

    EXPECT_FLOAT_EQ(p.rightAndMaxDistance[0], 1.0f);
    EXPECT_FLOAT_EQ(p.rightAndMaxDistance[3], 6.0f);
    EXPECT_FLOAT_EQ(p.downAndThickness[1], -1.0f);
    EXPECT_FLOAT_EQ(p.downAndThickness[3], 0.4f);
    EXPECT_FLOAT_EQ(p.forwardAndBias[2], 1.0f);
    EXPECT_FLOAT_EQ(p.forwardAndBias[3], 0.05f);
    EXPECT_FLOAT_EQ(p.viewAndFade[0], 1280.0f);
    EXPECT_FLOAT_EQ(p.viewAndFade[1], 720.0f);
    EXPECT_FLOAT_EQ(p.viewAndFade[2], 25.0f);
    EXPECT_FLOAT_EQ(p.viewAndFade[3], 50.0f);
    EXPECT_FLOAT_EQ(p.tuning[0], 3.0f);
    EXPECT_FLOAT_EQ(p.tuning[1], 0.2f);
    EXPECT_FLOAT_EQ(p.tuning[2], 0.002f);
    EXPECT_EQ(p.counts[0], 3u);
    EXPECT_EQ(p.counts[1], 20u);
    EXPECT_EQ(p.counts[2], 9u);
    EXPECT_EQ(p.counts[3], 2u);
}

TEST(SsgiParamsTest, DistanceFadeKeepsDistinctEdgesForZeroFraction)
{
    SsgiSettings cfg;
    cfg.distanceFadeFraction = 0.0f;
    const SsgiTraceParamsGpu p = MakeSsgiTraceParams(MakeBasis(), SanitizeSsgiSettings(cfg), Extent2D{ 640, 360 }, 0);
    EXPECT_LT(p.viewAndFade[2], p.viewAndFade[3]);
}

TEST(SsgiParamsTest, CompositePushCarriesIntensityAlbedoSigmaDivisorAndMode)
{
    SsgiSettings cfg;
    cfg.intensity = 1.5f;
    cfg.albedoLuma = 0.4f;
    cfg.albedoSaturation = 0.6f;
    cfg.depthSigma = 0.05f;
    cfg.debugView = SsgiDebugView::Normals;

    const SsgiCompositePush p = MakeSsgiCompositePush(MakeBasis(), SanitizeSsgiSettings(cfg), Mat4::Identity(), 2);

    EXPECT_FLOAT_EQ(p.eye[1], 2.0f);
    EXPECT_FLOAT_EQ(p.forward[2], 1.0f);
    EXPECT_FLOAT_EQ(p.intensityAlbedo[0], 1.5f);
    EXPECT_FLOAT_EQ(p.intensityAlbedo[1], 0.4f);
    EXPECT_FLOAT_EQ(p.intensityAlbedo[2], 0.6f);
    EXPECT_FLOAT_EQ(p.intensityAlbedo[3], 0.05f);
    EXPECT_EQ(p.divisorDebug[0], 2u);
    EXPECT_EQ(p.divisorDebug[1], 2u);
}

} // namespace
} // namespace gte
