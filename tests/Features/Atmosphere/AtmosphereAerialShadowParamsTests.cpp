// CPU checks of the aerial volume's shadow block and the receiver classification.

#include "Features/Atmosphere/AtmosphereAerialPerspectiveMath.h"
#include "Features/Atmosphere/AtmosphereParameters.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

namespace gte {
namespace {

SunShadowReceiver MakeReceiver(std::uint32_t count)
{
    SunShadowReceiver r;
    r.cascadeCount = count;
    r.resolution = 2048;
    for (std::uint32_t c = 0; c < count; ++c) {
        r.maps[c] = fx::Texture{ rg::TextureHandle{ c, 1 }, 3 };
        r.viewProjection[c] = Mat4::Translation(Vec3{ static_cast<float>(c + 1), 0.0f, 0.0f });
        r.splitFar[c] = 10.0f * static_cast<float>(c + 1);
        r.blendWidth[c] = c + 1 < count ? 2.0f : 0.0f;
        r.texelWorld[c] = 0.05f * static_cast<float>(c + 1);
        r.depthRange[c] = 100.0f * static_cast<float>(c + 1);
    }
    r.cameraPosition = Vec3{ 1.0f, 2.0f, 3.0f };
    r.cameraForward = Vec3::Forward();
    r.directionTowardSun = Vec3::Up();
    return r;
}

TEST(AerialShadowParamsTest, LayoutMatchesTheShaderBlock)
{
    EXPECT_EQ(sizeof(AerialShadowParamsGpu), 384u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, cascadeSplitFar), 256u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, cascadeBlendWidth), 272u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, cascadeTexelWorld), 288u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, cascadeDepthRange), 304u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, eyeAndUnitsPerKm), 320u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, forwardAndBias), 336u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, shadowParams), 352u);
    EXPECT_EQ(offsetof(AerialShadowParamsGpu, counts), 368u);
}

TEST(AerialShadowParamsTest, CopiesTheCascadeData)
{
    const SunShadowReceiver r = MakeReceiver(3);
    const AerialShadowParamsGpu p = MakeAerialShadowParams(&r, 1.0f);

    EXPECT_EQ(p.counts[0], 3u);
    EXPECT_EQ(p.counts[1], kAerialShadowTaps);
    for (std::uint32_t c = 0; c < 3; ++c) {
        EXPECT_TRUE(ApproximatelyEqual(p.cascadeViewProjection[c], r.viewProjection[c])) << c;
        EXPECT_FLOAT_EQ(p.cascadeSplitFar[c], r.splitFar[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeBlendWidth[c], r.blendWidth[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeTexelWorld[c], r.texelWorld[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeDepthRange[c], r.depthRange[c]) << c;
    }
    EXPECT_FLOAT_EQ(p.cascadeSplitFar[3], r.splitFar[2]);
    EXPECT_FLOAT_EQ(p.eyeAndUnitsPerKm[0], 1.0f);
    EXPECT_FLOAT_EQ(p.eyeAndUnitsPerKm[1], 2.0f);
    EXPECT_FLOAT_EQ(p.eyeAndUnitsPerKm[2], 3.0f);
    EXPECT_FLOAT_EQ(p.eyeAndUnitsPerKm[3], kWorldUnitsPerKilometer);
    EXPECT_FLOAT_EQ(p.forwardAndBias[2], 1.0f);
    EXPECT_FLOAT_EQ(p.forwardAndBias[3], kAerialShadowBiasTexels);
    EXPECT_FLOAT_EQ(p.shadowParams[0], kAerialShadowFilterTexels);
    EXPECT_FLOAT_EQ(p.shadowParams[1], 1.0f / 2048.0f);
    EXPECT_FLOAT_EQ(p.shadowParams[2], 1.0f);
}

TEST(AerialShadowParamsTest, NoShadowTapsWithoutAReceiverOrBlend)
{
    EXPECT_EQ(MakeAerialShadowParams(nullptr, 1.0f).counts[0], 0u);

    const SunShadowReceiver r = MakeReceiver(4);
    EXPECT_EQ(MakeAerialShadowParams(&r, 0.0f).counts[0], 0u);
    EXPECT_EQ(MakeAerialShadowParams(&r, -1.0f).counts[0], 0u);
    EXPECT_EQ(MakeAerialShadowParams(&r, std::numeric_limits<float>::quiet_NaN()).counts[0], 0u);
    EXPECT_EQ(MakeAerialShadowParams(&r, 0.5f).counts[0], 4u);
}

TEST(AerialShadowParamsTest, BlendIsClampedToOne)
{
    const SunShadowReceiver r = MakeReceiver(2);
    EXPECT_FLOAT_EQ(MakeAerialShadowParams(&r, 5.0f).shadowParams[2], 1.0f);
}

TEST(AerialReceiverTest, ClassifiesEveryState)
{
    const Vec3 sun = Vec3::Up();
    EXPECT_EQ(ClassifyAerialReceiver(nullptr, sun), AerialReceiverState::Missing);

    SunShadowReceiver r = MakeReceiver(2);
    EXPECT_EQ(ClassifyAerialReceiver(&r, sun), AerialReceiverState::Used);

    SunShadowReceiver broken = MakeReceiver(2);
    broken.maps[1] = fx::Texture{};
    EXPECT_EQ(ClassifyAerialReceiver(&broken, sun), AerialReceiverState::Rejected);

    r.directionTowardSun = Normalize(Vec3{ 0.05f, 1.0f, 0.0f });
    EXPECT_EQ(ClassifyAerialReceiver(&r, sun), AerialReceiverState::Mismatch);
}

} // namespace
} // namespace gte
