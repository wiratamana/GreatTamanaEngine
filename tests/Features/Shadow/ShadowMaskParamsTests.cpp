// Layout and conversion tests for the shadow mask GPU parameter block.

#include "Features/Shadow/ShadowMath.h"
#include "Features/Shadow/ShadowTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace gte {
namespace {

// Three hand-made cascades, so every packed value has a known source.
ShadowMapData MakeMapWithThreeCascades()
{
    ShadowMapData map;
    map.resolution = 1024;
    map.cascades.valid = true;
    map.cascades.count = 3;

    const float sliceStart[3] = { 0.5f, 9.0f, 28.0f };
    const float sliceEnd[3] = { 10.0f, 30.0f, 90.0f };
    for (std::uint32_t c = 0; c < 3; ++c) {
        ShadowCascade& cascade = map.cascades.cascades[c];
        cascade.viewProjection = Mat4::Translation(Vec3{ static_cast<float>(c + 1), 0.0f, 0.0f });
        cascade.sliceStart = sliceStart[c];
        cascade.sliceEnd = sliceEnd[c];
        cascade.texelWorldSize = 0.1f * static_cast<float>(c + 1);
        cascade.depthRange = 100.0f * static_cast<float>(c + 1);
    }

    map.cascades.cameraPosition = Vec3{ 1.0f, 2.0f, 3.0f };
    map.cascades.cameraForward = Vec3{ 0.0f, 0.0f, 1.0f };
    map.cascades.directionTowardSun = Vec3{ 0.0f, 1.0f, 0.0f };
    map.cascades.shadowDistance = 90.0f;
    map.cascades.fadeStart = 81.0f;
    return map;
}

TEST(ShadowMaskParamsTest, LayoutMatchesStd430Block)
{
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, invViewProjection), 0u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cascadeViewProjection), 64u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cascadeSplitFar), 320u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cascadeBlendWidth), 336u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cascadeTexelWorld), 352u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cascadeDepthRange), 368u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cameraPositionAndFadeStart), 384u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cameraForwardAndFadeEnd), 400u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, sunDirection), 416u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, biasParams), 432u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, filterRadiusTexels), 448u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, texelUvSize), 452u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, filterSamples), 456u);
    EXPECT_EQ(offsetof(ShadowMaskParamsGpu, cascadeCount), 460u);
    EXPECT_EQ(sizeof(ShadowMaskParamsGpu), 464u);
}

TEST(ShadowMaskParamsTest, ZeroResolutionGivesZeroTexelUvSize)
{
    ShadowMapData map;
    map.resolution = 0;
    const ShadowMaskParamsGpu params = MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    EXPECT_EQ(params.texelUvSize, 0.0f);
}

TEST(ShadowMaskParamsTest, TexelUvSizeIsReciprocalOfResolution)
{
    ShadowMapData map;
    map.resolution = 2048;
    const ShadowMaskParamsGpu params = MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    EXPECT_FLOAT_EQ(params.texelUvSize, 1.0f / 2048.0f);
}

TEST(ShadowMaskParamsTest, NoCascadesGivesCountZero)
{
    const ShadowMapData map; // Default cascade set: count 0.
    const ShadowMaskParamsGpu params = MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    EXPECT_EQ(params.cascadeCount, 0u);
}

TEST(ShadowMaskParamsTest, CopiesInverseViewProjectionAndPerCascadeData)
{
    const ShadowMapData map = MakeMapWithThreeCascades();
    const Mat4 inverse = Mat4::Translation(Vec3{ 1.0f, 2.0f, 3.0f });

    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), inverse);

    EXPECT_EQ(params.cascadeCount, 3u);
    EXPECT_TRUE(ApproximatelyEqual(params.invViewProjection, inverse));
    for (std::uint32_t c = 0; c < 3; ++c) {
        const ShadowCascade& cascade = map.cascades.cascades[c];
        EXPECT_TRUE(ApproximatelyEqual(params.cascadeViewProjection[c], cascade.viewProjection)) << "cascade " << c;
        EXPECT_FLOAT_EQ(params.cascadeSplitFar[c], cascade.sliceEnd) << "cascade " << c;
        EXPECT_FLOAT_EQ(params.cascadeTexelWorld[c], cascade.texelWorldSize) << "cascade " << c;
        EXPECT_FLOAT_EQ(params.cascadeDepthRange[c], cascade.depthRange) << "cascade " << c;
    }
}

TEST(ShadowMaskParamsTest, BlendWidthIsTheOverlapWithTheNextCascadeAndZeroForTheLast)
{
    const ShadowMapData map = MakeMapWithThreeCascades();
    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());

    EXPECT_FLOAT_EQ(params.cascadeBlendWidth[0], 10.0f - 9.0f);
    EXPECT_FLOAT_EQ(params.cascadeBlendWidth[1], 30.0f - 28.0f);
    EXPECT_FLOAT_EQ(params.cascadeBlendWidth[2], 0.0f);
    EXPECT_FLOAT_EQ(params.cascadeBlendWidth[3], 0.0f);
}

TEST(ShadowMaskParamsTest, BlendWidthNeverGoesNegativeWhenCascadesDoNotOverlap)
{
    ShadowMapData map = MakeMapWithThreeCascades();
    map.cascades.cascades[1].sliceStart = 20.0f; // Gap after cascade 0, which ends at 10.
    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    EXPECT_FLOAT_EQ(params.cascadeBlendWidth[0], 0.0f);
}

TEST(ShadowMaskParamsTest, UnusedCascadeSlotsRepeatTheLastSplitSoTheShaderNeverSelectsThem)
{
    const ShadowMapData map = MakeMapWithThreeCascades();
    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    EXPECT_FLOAT_EQ(params.cascadeSplitFar[3], 90.0f);
}

TEST(ShadowMaskParamsTest, CopiesCameraAndSunData)
{
    const ShadowMapData map = MakeMapWithThreeCascades();
    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());

    EXPECT_FLOAT_EQ(params.cameraPositionAndFadeStart[0], 1.0f);
    EXPECT_FLOAT_EQ(params.cameraPositionAndFadeStart[1], 2.0f);
    EXPECT_FLOAT_EQ(params.cameraPositionAndFadeStart[2], 3.0f);
    EXPECT_FLOAT_EQ(params.cameraPositionAndFadeStart[3], 81.0f);

    EXPECT_FLOAT_EQ(params.cameraForwardAndFadeEnd[0], 0.0f);
    EXPECT_FLOAT_EQ(params.cameraForwardAndFadeEnd[1], 0.0f);
    EXPECT_FLOAT_EQ(params.cameraForwardAndFadeEnd[2], 1.0f);
    EXPECT_FLOAT_EQ(params.cameraForwardAndFadeEnd[3], 90.0f);

    EXPECT_FLOAT_EQ(params.sunDirection[0], 0.0f);
    EXPECT_FLOAT_EQ(params.sunDirection[1], 1.0f);
    EXPECT_FLOAT_EQ(params.sunDirection[2], 0.0f);
}

TEST(ShadowMaskParamsTest, CopiesFilterAndBiasSettings)
{
    ShadowSettings settings;
    settings.constBiasTexels = 2.0f;
    settings.normalOffsetTexels = 3.0f;
    settings.slopeBiasTexels = 4.0f;
    settings.filterRadiusTexels = 2.5f;
    settings.filterSamples = 16;
    const ShadowSettings safe = SanitizeShadowSettings(settings);

    const ShadowMaskParamsGpu params = MakeShadowMaskParams(MakeMapWithThreeCascades(), safe, Mat4::Identity());

    EXPECT_FLOAT_EQ(params.biasParams[0], 2.0f);
    EXPECT_FLOAT_EQ(params.biasParams[1], 3.0f);
    EXPECT_FLOAT_EQ(params.biasParams[2], 4.0f);
    EXPECT_FLOAT_EQ(params.filterRadiusTexels, 2.5f);
    EXPECT_EQ(params.filterSamples, 16u);
}

TEST(ShadowMaskParamsTest, EffectiveSlopeBiasCoversTheFilterRadius)
{
    ShadowSettings settings;
    settings.slopeBiasTexels = 1.0f;
    settings.filterRadiusTexels = 3.0f;
    const ShadowSettings safe = SanitizeShadowSettings(settings);

    const ShadowMaskParamsGpu params = MakeShadowMaskParams(MakeMapWithThreeCascades(), safe, Mat4::Identity());
    EXPECT_FLOAT_EQ(params.biasParams[2], std::max(safe.slopeBiasTexels, safe.filterRadiusTexels));
    EXPECT_FLOAT_EQ(params.biasParams[2], 3.0f);
}

TEST(ShadowMaskParamsTest, CascadeCountIsCappedAtTheMaximum)
{
    ShadowMapData map = MakeMapWithThreeCascades();
    map.cascades.count = 99;
    const ShadowMaskParamsGpu params =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    EXPECT_EQ(params.cascadeCount, kMaxShadowCascades);
}

} // namespace
} // namespace gte
