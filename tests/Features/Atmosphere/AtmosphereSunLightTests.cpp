// CPU checks of the atmosphere sun light record: transmittance, tint, fade and failure cases.

#include "Features/Atmosphere/AtmosphereMath.h"
#include "Features/Atmosphere/AtmosphereParameters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr float kHeightKm = 0.002f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

SunLightModulation SunAt(float upDot)
{
    return ComputeAtmosphereSunLight(MakeDefaultEarthAtmosphereParameters(), kHeightKm, upDot);
}

float Luminance(const SunLightModulation& light)
{
    return 0.2126f * light.sunTint.x + 0.7152f * light.sunTint.y + 0.0722f * light.sunTint.z;
}

void ExpectInRange(const Vec3& v)
{
    for (const float c : { v.x, v.y, v.z }) {
        EXPECT_TRUE(std::isfinite(c));
        EXPECT_GE(c, 0.0f);
        EXPECT_LE(c, 1.0f);
    }
}

TEST(AtmosphereSunLightTest, OverheadSunIsTheReference)
{
    const SunLightModulation light = SunAt(1.0f);
    EXPECT_TRUE(light.active);
    EXPECT_NEAR(light.sunTint.x, 1.0f, 1e-5f);
    EXPECT_NEAR(light.sunTint.y, 1.0f, 1e-5f);
    EXPECT_NEAR(light.sunTint.z, 1.0f, 1e-5f);
    EXPECT_NEAR(light.ambientScale, 1.0f, 1e-5f);
}

TEST(AtmosphereSunLightTest, MatchesTheReferenceMarch)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 fast = ComputeSunTransmittance(params, kHeightKm, 0.5f, 128);
    const Vec3 reference =
        ComputeTransmittanceToTopOfAtmosphere(params, Vec3(0.0f, 6371.002f, 0.0f), Vec3(0.8660254f, 0.5f, 0.0f), 4000);
    EXPECT_NEAR(fast.x, reference.x, 2e-3f);
    EXPECT_NEAR(fast.y, reference.y, 2e-3f);
    EXPECT_NEAR(fast.z, reference.z, 2e-3f);
}

TEST(AtmosphereSunLightTest, LowerSunIsDimmerAndRedder)
{
    float previous = 2.0f;
    for (const float u : { 0.5f, 0.17f, 0.087f }) {
        const SunLightModulation light = SunAt(u);
        EXPECT_LT(light.ambientScale, previous) << u;
        previous = light.ambientScale;
    }
    const SunLightModulation low = SunAt(0.087f);
    EXPECT_GT(low.sunTint.x, low.sunTint.y);
    EXPECT_GT(low.sunTint.y, low.sunTint.z);
}

TEST(AtmosphereSunLightTest, BelowTheHorizonIsBlack)
{
    for (const float u : { -0.05f, -1.0f, 0.0f }) {
        const SunLightModulation light = SunAt(u);
        EXPECT_TRUE(light.active) << u;
        EXPECT_EQ(light.sunTint, Vec3::Zero()) << u;
        EXPECT_EQ(light.ambientScale, 0.0f) << u;
    }
}

TEST(AtmosphereSunLightTest, FadeIsMonotonic)
{
    float previous = 0.0f;
    for (int i = 0; i <= 30; ++i) {
        const float luminance = Luminance(SunAt(static_cast<float>(i) * 0.001f));
        EXPECT_GE(luminance, previous - 1e-6f) << i;
        previous = luminance;
    }
}

TEST(AtmosphereSunLightTest, PlanetBlocksTheRay)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    EXPECT_EQ(ComputeSunTransmittance(params, 0.0f, -0.01f, 128), Vec3::Zero());
    const Vec3 up = ComputeSunTransmittance(params, 0.0f, 0.01f, 128);
    EXPECT_GT(up.x, 0.0f);
    EXPECT_GT(up.y, 0.0f);
    EXPECT_GT(up.z, 0.0f);
}

TEST(AtmosphereSunLightTest, HigherCameraSeesMoreSun)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 low = ComputeSunTransmittance(params, kHeightKm, 0.05f, 128);
    const Vec3 high = ComputeSunTransmittance(params, 3.0f, 0.05f, 128);
    EXPECT_GE(high.x, low.x);
    EXPECT_GE(high.y, low.y);
    EXPECT_GE(high.z, low.z);
}

TEST(AtmosphereSunLightTest, NonFiniteInputIsInactive)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    for (const SunLightModulation& light : { ComputeAtmosphereSunLight(params, kNaN, 0.5f),
             ComputeAtmosphereSunLight(params, kHeightKm, kNaN), ComputeAtmosphereSunLight(params, kHeightKm, kInf) }) {
        EXPECT_FALSE(light.active);
        EXPECT_EQ(light.sunTint, Vec3::One());
        EXPECT_EQ(light.ambientScale, 1.0f);
    }
}

TEST(AtmosphereSunLightTest, CustomPlanetStaysFinite)
{
    AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    params.planetRadiusKm = 10.0f;
    params.atmosphereThicknessKm = 1.0f;
    for (const float u : { -1.0f, -0.1f, 0.0f, 0.3f, 1.0f }) {
        const SunLightModulation light = ComputeAtmosphereSunLight(params, kHeightKm, u);
        ExpectInRange(light.sunTint);
        ExpectInRange(ComputeSunTransmittance(params, kHeightKm, u, 128));
        EXPECT_TRUE(std::isfinite(light.ambientScale));
    }
}

TEST(AtmosphereSunLightTest, InvalidPlanetIsInactive)
{
    for (const float bad : { 0.0f, -5.0f, kNaN }) {
        AtmosphereParametersGpu radius = MakeDefaultEarthAtmosphereParameters();
        radius.planetRadiusKm = bad;
        AtmosphereParametersGpu thickness = MakeDefaultEarthAtmosphereParameters();
        thickness.atmosphereThicknessKm = bad;
        for (const AtmosphereParametersGpu& params : { radius, thickness }) {
            EXPECT_FALSE(ComputeAtmosphereSunLight(params, kHeightKm, 0.5f).active);
            EXPECT_EQ(ComputeSunTransmittance(params, kHeightKm, 0.5f, 128), Vec3::One());
        }
    }
}

TEST(AtmosphereSunLightTest, NonFiniteCoefficientIsInactive)
{
    AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    params.rayleighScattering.x = kNaN;
    EXPECT_FALSE(ComputeAtmosphereSunLight(params, kHeightKm, 0.5f).active);
}

TEST(AtmosphereSunLightTest, CameraAboveTheAtmosphereStaysFinite)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    for (const float u : { -0.5f, 0.01f, 1.0f }) {
        const SunLightModulation light = ComputeAtmosphereSunLight(params, 500.0f, u);
        ExpectInRange(light.sunTint);
        EXPECT_TRUE(std::isfinite(light.ambientScale));
    }
}

} // namespace
} // namespace gte
