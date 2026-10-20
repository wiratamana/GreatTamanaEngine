// Layout and conversion tests for the VolumetricLight GPU blocks.

#include "Features/VolumetricLight/VolumetricLightMath.h"
#include "Features/VolumetricLight/VolumetricLightTypes.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

namespace gte {
namespace {

TEST(VolumetricLightParamsTest, LayoutMatchesTheShaderBlocks)
{
    EXPECT_EQ(sizeof(VolumetricLightInjectParamsGpu), 560u);
    EXPECT_EQ(offsetof(VolumetricLightInjectParamsGpu, cascadeViewProjection), 64u);
    EXPECT_EQ(offsetof(VolumetricLightInjectParamsGpu, cascadeSplitFar), 320u);
    EXPECT_EQ(offsetof(VolumetricLightInjectParamsGpu, eyeAndMaxDistance), 384u);
    EXPECT_EQ(offsetof(VolumetricLightInjectParamsGpu, shadowParams), 528u);
    EXPECT_EQ(offsetof(VolumetricLightInjectParamsGpu, counts), 544u);

    EXPECT_EQ(sizeof(VolumetricLightTemporalParamsGpu), 192u);
    EXPECT_EQ(offsetof(VolumetricLightTemporalParamsGpu, prevViewProjection), 64u);
    EXPECT_EQ(offsetof(VolumetricLightTemporalParamsGpu, exponentsAndBlend), 160u);
    EXPECT_EQ(offsetof(VolumetricLightTemporalParamsGpu, atlas), 176u);

    EXPECT_EQ(sizeof(VolumetricLightIntegratePush), 8u);
    EXPECT_EQ(sizeof(VolumetricLightCompositePush), 112u);
    EXPECT_EQ(offsetof(VolumetricLightCompositePush, frameAndDebug), 96u);
}

SunShadowReceiver MakeReceiver(std::uint32_t count)
{
    SunShadowReceiver r;
    r.cascadeCount = count;
    r.resolution = 2048;
    for (std::uint32_t c = 0; c < count; ++c) {
        r.maps[c] = fx::Texture{ rg::TextureHandle{ c, 1 }, 3 };
        r.viewProjection[c] = Mat4::Translation(Vec3{ static_cast<float>(c + 1), 0.0f, 0.0f });
        r.splitFar[c] = 10.0f * static_cast<float>(c + 1);
        r.blendWidth[c] = 1.0f + static_cast<float>(c);
        r.texelWorld[c] = 0.1f * static_cast<float>(c + 1);
        r.depthRange[c] = 100.0f * static_cast<float>(c + 1);
    }
    return r;
}

VolumetricLightFrameFacts MakeFacts()
{
    VolumetricLightFrameFacts f;
    f.eye = Vec3{ 1.0f, 2.0f, 3.0f };
    f.forward = Vec3::Forward();
    f.sunDirection = Vec3::Up();
    f.sunIlluminance = Vec3{ 2.0f, 1.0f, 0.5f };
    f.jitter = Vec3{ 0.1f, 0.2f, 0.3f };
    f.windOffset = { 4.0, 0.0, 5.0 };
    f.maxDistance = 40.0f;
    f.frameIndex = 77;
    return f;
}

TEST(VolumetricLightParamsTest, InjectCopiesTheCascadesAndRepeatsTheLastSplit)
{
    const SunShadowReceiver receiver = MakeReceiver(3);
    const VolumetricLightInjectParamsGpu p =
        MakeVolumetricLightInjectParams(VolumetricLightSettings{}, &receiver, MakeFacts());

    EXPECT_EQ(p.counts[0], 3u);
    for (std::uint32_t c = 0; c < 3; ++c) {
        EXPECT_TRUE(ApproximatelyEqual(p.cascadeViewProjection[c], receiver.viewProjection[c])) << c;
        EXPECT_FLOAT_EQ(p.cascadeSplitFar[c], receiver.splitFar[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeBlendWidth[c], receiver.blendWidth[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeTexelWorld[c], receiver.texelWorld[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeDepthRange[c], receiver.depthRange[c]) << c;
    }
    EXPECT_FLOAT_EQ(p.cascadeSplitFar[3], 30.0f);
    EXPECT_FLOAT_EQ(p.cascadeTexelWorld[3], 0.0f);
    EXPECT_FLOAT_EQ(p.cascadeDepthRange[3], 0.0f);
    EXPECT_TRUE(ApproximatelyEqual(p.cascadeViewProjection[3], Mat4::Zero()));
    EXPECT_FLOAT_EQ(p.shadowParams[2], 1.0f / 2048.0f);
}

TEST(VolumetricLightParamsTest, InjectWithoutAReceiverHasNoCascades)
{
    const VolumetricLightInjectParamsGpu p =
        MakeVolumetricLightInjectParams(VolumetricLightSettings{}, nullptr, MakeFacts());
    EXPECT_EQ(p.counts[0], 0u);
    EXPECT_FLOAT_EQ(p.cascadeSplitFar[0], 0.0f);
    EXPECT_FLOAT_EQ(p.shadowParams[2], 0.0f);
}

TEST(VolumetricLightParamsTest, InjectCapsTheCascadeCountAtFour)
{
    SunShadowReceiver receiver = MakeReceiver(4);
    receiver.cascadeCount = 99;
    EXPECT_EQ(MakeVolumetricLightInjectParams(VolumetricLightSettings{}, &receiver, MakeFacts()).counts[0], 4u);
}

TEST(VolumetricLightParamsTest, InjectFieldsLandInTheirSlots)
{
    VolumetricLightSettings s = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    s.density = 0.05f;
    s.albedo = 0.8f;
    s.startDistance = 3.0f;
    s.noiseAmount = 0.25f;
    s.heightDensity = 0.4f;
    s.heightBase = 1.5f;
    s.heightFalloff = 0.3f;
    s.noiseScale = 0.7f;
    s.anisotropyG = 0.6f;
    s.depthExponent = 2.5f;
    s.shadowSamples = 8;
    s.shadowFilterTexels = 2.0f;
    s.shadowBiasTexels = 0.5f;
    s.ambientShadowedFraction = 0.25f;

    const VolumetricLightFrameFacts facts = MakeFacts();
    const VolumetricLightInjectParamsGpu p = MakeVolumetricLightInjectParams(s, nullptr, facts);

    EXPECT_FLOAT_EQ(p.eyeAndMaxDistance[0], 1.0f);
    EXPECT_FLOAT_EQ(p.eyeAndMaxDistance[2], 3.0f);
    EXPECT_FLOAT_EQ(p.eyeAndMaxDistance[3], 40.0f);
    EXPECT_FLOAT_EQ(p.forwardAndExponent[2], 1.0f);
    EXPECT_FLOAT_EQ(p.forwardAndExponent[3], 2.5f);
    EXPECT_FLOAT_EQ(p.sunDirectionAndG[1], 1.0f);
    EXPECT_FLOAT_EQ(p.sunDirectionAndG[3], 0.6f);
    EXPECT_FLOAT_EQ(p.densityParams[0], 0.05f);
    EXPECT_FLOAT_EQ(p.densityParams[1], 0.8f);
    EXPECT_FLOAT_EQ(p.densityParams[2], 3.0f);
    EXPECT_FLOAT_EQ(p.densityParams[3], 0.25f);
    EXPECT_FLOAT_EQ(p.heightParams[0], 0.4f);
    EXPECT_FLOAT_EQ(p.heightParams[1], 1.5f);
    EXPECT_FLOAT_EQ(p.heightParams[2], 0.3f);
    EXPECT_FLOAT_EQ(p.heightParams[3], 0.7f);
    EXPECT_FLOAT_EQ(p.noiseOffset[0], 4.0f);
    EXPECT_FLOAT_EQ(p.noiseOffset[2], 5.0f);
    EXPECT_FLOAT_EQ(p.jitter[0], 0.1f);
    EXPECT_FLOAT_EQ(p.jitter[2], 0.3f);
    EXPECT_FLOAT_EQ(p.shadowParams[0], 0.5f);
    EXPECT_FLOAT_EQ(p.shadowParams[1], 2.0f);
    EXPECT_FLOAT_EQ(p.shadowParams[3], 0.25f);
    EXPECT_EQ(p.counts[1], 8u);
    EXPECT_EQ(p.counts[2], 77u);
}

TEST(VolumetricLightParamsTest, SunLightScalesWithTheDirectScaleAndFadesBelowTheHorizon)
{
    VolumetricLightSettings s = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    s.sunIntensity = 2.0f;
    s.ambientIntensity = 1.0f;
    s.ambientR = 1.0f;
    s.ambientG = 2.0f;
    s.ambientB = 4.0f;

    VolumetricLightFrameFacts facts = MakeFacts();
    const VolumetricLightInjectParamsGpu day = MakeVolumetricLightInjectParams(s, nullptr, facts);
    EXPECT_FLOAT_EQ(day.sunRadiance[0], 2.0f * kVolumetricLightSunDirectScale * 2.0f);
    EXPECT_FLOAT_EQ(day.sunRadiance[2], 0.5f * kVolumetricLightSunDirectScale * 2.0f);
    EXPECT_FLOAT_EQ(day.ambientRadiance[1], 2.0f);

    facts.sunDirection = Vec3{ 0.0f, -1.0f, 0.0f };
    const VolumetricLightInjectParamsGpu night = MakeVolumetricLightInjectParams(s, nullptr, facts);
    EXPECT_FLOAT_EQ(night.sunRadiance[0], 0.0f);
    EXPECT_FLOAT_EQ(night.sunRadiance[1], 0.0f);
    EXPECT_FLOAT_EQ(night.ambientRadiance[1], 2.0f * kVolumetricLightNightAmbientFraction);
}

TEST(VolumetricLightParamsTest, TemporalIgnoresTheHistoryUnlessUsable)
{
    const VolumetricLightSettings s = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    VolumetricLightFrameFacts facts = MakeFacts();
    facts.viewProjection = Mat4::Translation(Vec3{ 1.0f, 0.0f, 0.0f });
    VolumetricLightHistory previous = MakeVolumetricLightHistory(facts, 3.0f, 1, VolumetricLightHistory{});
    previous.maxDistance = 25.0f;

    const VolumetricLightTemporalParamsGpu on = MakeVolumetricLightTemporalParams(s, facts, previous, 0.9f, true);
    EXPECT_FLOAT_EQ(on.exponentsAndBlend[0], s.depthExponent);
    EXPECT_FLOAT_EQ(on.exponentsAndBlend[1], 3.0f);
    EXPECT_FLOAT_EQ(on.exponentsAndBlend[2], 0.9f);
    EXPECT_FLOAT_EQ(on.exponentsAndBlend[3], 1.0f);
    EXPECT_FLOAT_EQ(on.eyeAndMaxDistance[3], 40.0f);
    EXPECT_FLOAT_EQ(on.prevEyeAndMaxDistance[3], 25.0f);
    EXPECT_TRUE(ApproximatelyEqual(on.prevViewProjection, facts.viewProjection));
    EXPECT_EQ(on.atlas[0], kVolumetricLightAtlasTilesX);
    EXPECT_EQ(on.atlas[1], kVolumetricLightAtlasTilesY);

    const VolumetricLightTemporalParamsGpu off = MakeVolumetricLightTemporalParams(s, facts, previous, 0.9f, false);
    EXPECT_FLOAT_EQ(off.exponentsAndBlend[2], 0.0f);
    EXPECT_FLOAT_EQ(off.exponentsAndBlend[3], 0.0f);
}

TEST(VolumetricLightParamsTest, CompositePushCarriesTheResultAndSettings)
{
    VolumetricLightSettings s = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    s.strength = 0.5f;
    s.skyDistanceFraction = 0.25f;
    s.debugView = VolumetricLightDebugView::Transmittance;

    VolumetricLightResult result;
    result.maxDistance = 33.0f;
    result.depthExponent = 2.0f;
    const VolumetricLightCompositePush p =
        MakeVolumetricLightCompositePush(s, Mat4::Identity(), Vec3{ 1.0f, 2.0f, 3.0f }, result, 5);

    EXPECT_FLOAT_EQ(p.eyeAndMaxDistance[1], 2.0f);
    EXPECT_FLOAT_EQ(p.eyeAndMaxDistance[3], 33.0f);
    EXPECT_FLOAT_EQ(p.strengthExponentReserved[0], 0.5f);
    EXPECT_FLOAT_EQ(p.strengthExponentReserved[1], 2.0f);
    EXPECT_FLOAT_EQ(p.strengthExponentReserved[2], 0.0f);
    EXPECT_FLOAT_EQ(p.strengthExponentReserved[3], 0.0f);
    EXPECT_EQ(p.frameAndDebug[0], 5u);
    EXPECT_EQ(p.frameAndDebug[1], 2u);
    EXPECT_EQ(std::bit_cast<float>(p.frameAndDebug[2]), 0.25f);
}

TEST(VolumetricLightParamsTest, SunDirectScaleMatchesTheMaterialLighting)
{
    std::ifstream file(GTE_TEST_LIGHTING_GLSL_PATH, std::ios::binary);
    ASSERT_TRUE(file.good()) << "cannot read " << GTE_TEST_LIGHTING_GLSL_PATH;
    std::stringstream text;
    text << file.rdbuf();
    EXPECT_NE(text.str().find("kDirectScale = 0.65"), std::string::npos);
    EXPECT_FLOAT_EQ(kVolumetricLightSunDirectScale, 0.65f);
}

} // namespace
} // namespace gte
