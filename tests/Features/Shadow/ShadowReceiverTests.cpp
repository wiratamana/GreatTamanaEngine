// Tests for the sun shadow receiver contract that Shadow publishes to other features.

#include "Core/FeatureKit/ChannelKey.h"
#include "Features/Shadow/ShadowMath.h"
#include "Features/Shadow/ShadowTypes.h"
#include "Game/Lighting/SunShadowReceiver.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace gte {
namespace {

ShadowMapData MakeMap(std::uint32_t count)
{
    ShadowMapData map;
    map.resolution = 1024;
    map.cascades.valid = true;
    map.cascades.count = count;
    for (std::uint32_t c = 0; c < count; ++c) {
        ShadowCascade& cascade = map.cascades.cascades[c];
        cascade.viewProjection = Mat4::Translation(Vec3{ static_cast<float>(c + 1), 0.0f, 0.0f });
        cascade.sliceStart = c == 0 ? 0.5f : 10.0f * static_cast<float>(c) - 1.0f;
        cascade.sliceEnd = 10.0f * static_cast<float>(c + 1);
        cascade.texelWorldSize = 0.1f * static_cast<float>(c + 1);
        cascade.depthRange = 100.0f * static_cast<float>(c + 1);
        map.maps[c] = fx::Texture{ rg::TextureHandle{ c + 1, 1 }, 7 };
    }
    map.cascades.cameraPosition = Vec3{ 1.0f, 2.0f, 3.0f };
    map.cascades.cameraForward = Vec3{ 0.0f, 0.0f, 1.0f };
    map.cascades.directionTowardSun = Vec3{ 0.0f, 1.0f, 0.0f };
    map.cascades.shadowDistance = 10.0f * static_cast<float>(count);
    return map;
}

void ExpectMatchesMaskParams(std::uint32_t count)
{
    const ShadowMapData map = MakeMap(count);
    const ShadowMaskParamsGpu mask =
        MakeShadowMaskParams(map, SanitizeShadowSettings(ShadowSettings{}), Mat4::Identity());
    const SunShadowReceiver receiver = MakeSunShadowReceiver(map);

    EXPECT_EQ(receiver.cascadeCount, count);
    EXPECT_EQ(receiver.resolution, map.resolution);
    for (std::uint32_t c = 0; c < kSunShadowReceiverMaxCascades; ++c) {
        EXPECT_TRUE(ApproximatelyEqual(receiver.viewProjection[c], mask.cascadeViewProjection[c])) << c;
        EXPECT_FLOAT_EQ(receiver.splitFar[c], mask.cascadeSplitFar[c]) << c;
        EXPECT_FLOAT_EQ(receiver.blendWidth[c], mask.cascadeBlendWidth[c]) << c;
        EXPECT_FLOAT_EQ(receiver.texelWorld[c], mask.cascadeTexelWorld[c]) << c;
        EXPECT_FLOAT_EQ(receiver.depthRange[c], mask.cascadeDepthRange[c]) << c;
        EXPECT_EQ(receiver.maps[c].IsValid(), c < count) << c;
    }
    EXPECT_FLOAT_EQ(receiver.cameraPosition.x, mask.cameraPositionAndFadeStart[0]);
    EXPECT_FLOAT_EQ(receiver.cameraPosition.y, mask.cameraPositionAndFadeStart[1]);
    EXPECT_FLOAT_EQ(receiver.cameraPosition.z, mask.cameraPositionAndFadeStart[2]);
    EXPECT_FLOAT_EQ(receiver.cameraForward.z, mask.cameraForwardAndFadeEnd[2]);
    EXPECT_FLOAT_EQ(receiver.shadowDistance, mask.cameraForwardAndFadeEnd[3]);
    EXPECT_FLOAT_EQ(receiver.directionTowardSun.y, mask.sunDirection[1]);
}

TEST(ShadowReceiverTest, MatchesTheMaskParamsForOneCascade)
{
    ExpectMatchesMaskParams(1);
}

TEST(ShadowReceiverTest, MatchesTheMaskParamsForFourCascades)
{
    ExpectMatchesMaskParams(4);
}

TEST(ShadowReceiverTest, CascadeCountIsCappedAtTheMaximum)
{
    ShadowMapData map = MakeMap(4);
    map.cascades.count = 99;
    EXPECT_EQ(MakeSunShadowReceiver(map).cascadeCount, kSunShadowReceiverMaxCascades);
}

TEST(ShadowReceiverTest, EmptyMapGivesAnEmptyReceiver)
{
    const SunShadowReceiver receiver = MakeSunShadowReceiver(ShadowMapData{});
    EXPECT_EQ(receiver.cascadeCount, 0u);
    EXPECT_FLOAT_EQ(receiver.splitFar[0], 0.0f);
}

TEST(ShadowReceiverTest, CarriesTheMaterialSamplingSettings)
{
    ShadowMapData map = MakeMap(3);
    map.cascades.fadeStart = 27.0f;
    ShadowSettings settings;
    settings.strength = 0.8f;
    settings.constBiasTexels = 2.0f;
    settings.normalOffsetTexels = 3.0f;
    settings.slopeBiasTexels = 0.5f;
    settings.filterRadiusTexels = 1.5f;
    settings.filterSamples = 4;
    const SunShadowReceiver receiver = MakeSunShadowReceiver(map, SanitizeShadowSettings(settings));

    EXPECT_FLOAT_EQ(receiver.fadeStart, 27.0f);
    EXPECT_FLOAT_EQ(receiver.materialStrength, 0.8f);
    EXPECT_FLOAT_EQ(receiver.constBiasTexels, 2.0f);
    EXPECT_FLOAT_EQ(receiver.normalOffsetTexels, 3.0f);
    EXPECT_FLOAT_EQ(receiver.filterRadiusTexels, 1.5f);
    EXPECT_FLOAT_EQ(receiver.slopeBiasTexels, 1.5f); // max(slope, filter radius)
    EXPECT_EQ(receiver.filterSamples, 4u);
}

TEST(ShadowReceiverTest, MaterialStrengthIsZeroWhenMaterialsDoNotApplyShadows)
{
    ShadowSettings settings;
    settings.applyInMaterials = false;
    EXPECT_FLOAT_EQ(MakeSunShadowReceiver(MakeMap(2), SanitizeShadowSettings(settings)).materialStrength, 0.0f);
}

TEST(ShadowReceiverTest, DepthFittedFollowsSdsmActive)
{
    ShadowMapData map = MakeMap(2);
    EXPECT_FALSE(MakeSunShadowReceiver(map).depthFitted);
    map.cascades.sdsmActive = true;
    EXPECT_TRUE(MakeSunShadowReceiver(map).depthFitted);
}

TEST(ShadowReceiverTest, OwnerAndChannelMatchTheShadowFeatureKey)
{
    EXPECT_STREQ(kSunShadowReceiverOwner, kShadowFeatureName);
    for (const char* view : { "Primary", "Secondary" }) {
        const rg::RenderViewId id = rg::RenderViewId::Named(view);
        EXPECT_EQ(fx::ChannelKey(kSunShadowReceiverOwner, kSunShadowReceiverChannel, id),
            fx::ChannelKey(kShadowFeatureName, "Receiver", id))
            << view;
    }
}

} // namespace
} // namespace gte
