// CPU checks of the atmosphere settings sanitizer.

#include "Features/Atmosphere/AtmosphereParameters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

bool SameColor(const Vec3& a, const Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

TEST(AtmosphereSettingsTest, DefaultsPassThroughUnchanged)
{
    const AtmosphereSettings d;
    const AtmosphereSettings s = SanitizeAtmosphereSettings(d);
    EXPECT_FLOAT_EQ(s.planetRadiusKm, d.planetRadiusKm);
    EXPECT_FLOAT_EQ(s.atmosphereThicknessKm, d.atmosphereThicknessKm);
    EXPECT_FLOAT_EQ(s.miePhaseG, d.miePhaseG);
    EXPECT_FLOAT_EQ(s.skyIntensity, d.skyIntensity);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveMaxDistanceKm, d.aerialPerspectiveMaxDistanceKm);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveScatteringExaggeration, d.aerialPerspectiveScatteringExaggeration);
    EXPECT_EQ(s.aerialPerspectiveSamplesPerSlice, d.aerialPerspectiveSamplesPerSlice);
    EXPECT_TRUE(SameColor(s.rayleighScattering, d.rayleighScattering));
    EXPECT_TRUE(SameColor(s.groundAlbedo, d.groundAlbedo));
    EXPECT_EQ(s.autoScale, d.autoScale);
    EXPECT_EQ(s.sceneLightFollowsAtmosphere, d.sceneLightFollowsAtmosphere);
}

TEST(AtmosphereSettingsTest, NonFiniteValuesBecomeTheFieldDefault)
{
    const AtmosphereSettings d;
    AtmosphereSettings bad;
    bad.planetRadiusKm = kNaN;
    bad.atmosphereThicknessKm = kInf;
    bad.miePhaseG = -kInf;
    bad.aerialPerspectiveMaxDistanceKm = kNaN;
    bad.aerialPerspectiveStrength = kInf;
    bad.rayleighScattering = Vec3(kNaN, 0.5f, kInf);

    const AtmosphereSettings s = SanitizeAtmosphereSettings(bad);
    EXPECT_FLOAT_EQ(s.planetRadiusKm, d.planetRadiusKm);
    EXPECT_FLOAT_EQ(s.atmosphereThicknessKm, d.atmosphereThicknessKm);
    EXPECT_FLOAT_EQ(s.miePhaseG, d.miePhaseG);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveMaxDistanceKm, d.aerialPerspectiveMaxDistanceKm);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveStrength, d.aerialPerspectiveStrength);
    EXPECT_FLOAT_EQ(s.rayleighScattering.x, d.rayleighScattering.x);
    EXPECT_FLOAT_EQ(s.rayleighScattering.y, 0.5f);
    EXPECT_FLOAT_EQ(s.rayleighScattering.z, d.rayleighScattering.z);
}

TEST(AtmosphereSettingsTest, OutOfRangeValuesClampToTheirRanges)
{
    AtmosphereSettings low;
    low.planetRadiusKm = -5.0f;
    low.atmosphereThicknessKm = 0.0f;
    low.multiScatteringStrength = -1.0f;
    low.rayleighScaleHeightKm = 0.0f;
    low.miePhaseG = -9.0f;
    low.aerialPerspectiveStrength = -1.0f;
    low.skyIntensity = -1.0f;
    low.aerialPerspectiveMaxDistanceKm = 0.0f;
    low.aerialPerspectiveDepthExponent = 0.0f;
    low.aerialPerspectiveSamplesPerSlice = -4;
    low.aerialPerspectiveScatteringExaggeration = 0.0f;
    low.groundAlbedo = Vec3(-1.0f, -1.0f, -1.0f);
    const AtmosphereSettings a = SanitizeAtmosphereSettings(low);
    EXPECT_FLOAT_EQ(a.planetRadiusKm, 1.0f);
    EXPECT_FLOAT_EQ(a.atmosphereThicknessKm, 1.0f);
    EXPECT_FLOAT_EQ(a.multiScatteringStrength, 0.0f);
    EXPECT_FLOAT_EQ(a.rayleighScaleHeightKm, 0.01f);
    EXPECT_FLOAT_EQ(a.miePhaseG, -0.99f);
    EXPECT_FLOAT_EQ(a.aerialPerspectiveStrength, 0.0f);
    EXPECT_FLOAT_EQ(a.skyIntensity, 0.0f);
    EXPECT_FLOAT_EQ(a.aerialPerspectiveMaxDistanceKm, 0.01f);
    EXPECT_FLOAT_EQ(a.aerialPerspectiveDepthExponent, 1.0f);
    EXPECT_EQ(a.aerialPerspectiveSamplesPerSlice, 1);
    EXPECT_FLOAT_EQ(a.aerialPerspectiveScatteringExaggeration, 0.1f);
    EXPECT_FLOAT_EQ(a.groundAlbedo.x, 0.0f);

    AtmosphereSettings high;
    high.planetRadiusKm = 1e30f;
    high.atmosphereThicknessKm = 1e30f;
    high.multiScatteringStrength = 1e30f;
    high.miePhaseG = 5.0f;
    high.aerialPerspectiveStrength = 1e30f;
    high.skyIntensity = 1e9f;
    high.aerialPerspectiveMaxDistanceKm = 1e9f;
    high.aerialPerspectiveDepthExponent = 99.0f;
    high.aerialPerspectiveSamplesPerSlice = 1000000;
    high.aerialPerspectiveScatteringExaggeration = 1e9f;
    const AtmosphereSettings b = SanitizeAtmosphereSettings(high);
    EXPECT_FLOAT_EQ(b.planetRadiusKm, 50000.0f);
    EXPECT_FLOAT_EQ(b.atmosphereThicknessKm, 500.0f);
    EXPECT_FLOAT_EQ(b.multiScatteringStrength, 10.0f);
    EXPECT_FLOAT_EQ(b.miePhaseG, 0.99f);
    EXPECT_FLOAT_EQ(b.aerialPerspectiveStrength, 1e30f);
    EXPECT_FLOAT_EQ(b.skyIntensity, 50.0f);
    EXPECT_FLOAT_EQ(b.aerialPerspectiveMaxDistanceKm, 50.0f);
    EXPECT_FLOAT_EQ(b.aerialPerspectiveDepthExponent, 4.0f);
    EXPECT_EQ(b.aerialPerspectiveSamplesPerSlice, 16);
    EXPECT_FLOAT_EQ(b.aerialPerspectiveScatteringExaggeration, 50.0f);
}

TEST(AtmosphereSettingsTest, LargeStrengthIsKept)
{
    const AtmosphereSettings d;
    AtmosphereSettings s;
    s.aerialPerspectiveStrength = 250.0f;
    EXPECT_FLOAT_EQ(SanitizeAtmosphereSettings(s).aerialPerspectiveStrength, 250.0f);
    s.aerialPerspectiveStrength = kInf;
    EXPECT_FLOAT_EQ(SanitizeAtmosphereSettings(s).aerialPerspectiveStrength, d.aerialPerspectiveStrength);
    s.aerialPerspectiveStrength = kNaN;
    EXPECT_FLOAT_EQ(SanitizeAtmosphereSettings(s).aerialPerspectiveStrength, d.aerialPerspectiveStrength);
}

TEST(AtmosphereSettingsTest, SanitizeIsIdempotent)
{
    AtmosphereSettings bad;
    bad.planetRadiusKm = kNaN;
    bad.aerialPerspectiveSamplesPerSlice = 77;
    bad.skyIntensity = 1e9f;
    bad.ozoneTentCenterKm = -3.0f;
    const AtmosphereSettings once = SanitizeAtmosphereSettings(bad);
    const AtmosphereSettings twice = SanitizeAtmosphereSettings(once);
    EXPECT_FLOAT_EQ(once.planetRadiusKm, twice.planetRadiusKm);
    EXPECT_EQ(once.aerialPerspectiveSamplesPerSlice, twice.aerialPerspectiveSamplesPerSlice);
    EXPECT_FLOAT_EQ(once.skyIntensity, twice.skyIntensity);
    EXPECT_FLOAT_EQ(once.ozoneTentCenterKm, twice.ozoneTentCenterKm);
}

} // namespace
} // namespace gte
