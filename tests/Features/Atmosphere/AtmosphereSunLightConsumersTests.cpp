// Materials and light shafts must read the same sun light record the same way.

#include "Features/SceneLighting/SceneLightingTypes.h"
#include "Features/VolumetricLight/VolumetricLightMath.h"
#include "Game/Lighting/SunLightModulation.h"

#include <gtest/gtest.h>

#include <limits>

namespace gte {
namespace {

TEST(SunLightModulationTest, SanitizeRepairsTheRecord)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    const SunLightModulation inactive = SanitizeSunLightModulation({ false, Vec3(0.5f, 0.5f, 0.5f), 0.5f });
    EXPECT_FALSE(inactive.active);
    EXPECT_EQ(inactive.sunTint, Vec3::One());
    EXPECT_EQ(inactive.ambientScale, 1.0f);

    for (const SunLightModulation& bad : { SunLightModulation{ true, Vec3(nan, 1.0f, 1.0f), 1.0f },
             SunLightModulation{ true, Vec3(1.0f, inf, 1.0f), 1.0f }, SunLightModulation{ true, Vec3::One(), nan } }) {
        const SunLightModulation out = SanitizeSunLightModulation(bad);
        EXPECT_FALSE(out.active);
        EXPECT_EQ(out.sunTint, Vec3::One());
        EXPECT_EQ(out.ambientScale, 1.0f);
    }

    const SunLightModulation clamped = SanitizeSunLightModulation({ true, Vec3(3.0f, -1.0f, 0.5f), 2.0f });
    EXPECT_TRUE(clamped.active);
    EXPECT_EQ(clamped.sunTint, Vec3(1.0f, 0.0f, 0.5f));
    EXPECT_EQ(clamped.ambientScale, 1.0f);
}

TEST(SunLightModulationTest, SameRecordSameNumbers)
{
    const SunLightModulation record{ true, Vec3(0.5f, 0.25f, 0.1f), 0.4f };
    const Vec3 sunIlluminance(2.0f, 1.0f, 0.5f);

    ResolvedDirectionalLight sun{};
    sun.sunIlluminance = sunIlluminance;
    SceneLightingSettings material;
    material.ambientIntensity = 1.0f;
    const SceneLightingUniformData packed = PackSceneLightingUniformData(sun, material, Vec3::Zero(), nullptr, &record);

    VolumetricLightSettings shafts = SanitizeVolumetricLightSettings(VolumetricLightSettings{});
    shafts.sunIntensity = 1.0f;
    shafts.ambientIntensity = 1.0f;
    shafts.ambientR = shafts.ambientG = shafts.ambientB = 1.0f;
    VolumetricLightFrameFacts facts;
    facts.sunIlluminance = sunIlluminance;
    facts.atmosphereLight = record.active;
    facts.sunTint = record.sunTint;
    facts.ambientScale = record.ambientScale;
    const VolumetricLightInjectParamsGpu params = MakeVolumetricLightInjectParams(shafts, nullptr, facts);

    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(params.sunRadiance[i] / kVolumetricLightSunDirectScale, packed.sunIlluminance[i], 1e-6f) << i;
        EXPECT_NEAR(params.ambientRadiance[i], packed.skyAmbientAndIntensity[3], 1e-6f) << i;
    }
}

} // namespace
} // namespace gte
