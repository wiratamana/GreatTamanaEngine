// CPU checks of the surface clamp and the planet-hides-the-sun test used by the aerial perspective march.

#include "Features/Atmosphere/AtmosphereMath.h"
#include "Features/Atmosphere/AtmosphereParameters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr double kPi = 3.14159265358979323846;
constexpr float kR = 6371.0f;

double ToRadians(double degrees) { return degrees * kPi / 180.0; }

// Unit direction to the sun; azimuth 0 points along the ray's horizontal direction (+x).
Vec3 SunDirection(double elevationDeg, double azimuthDeg = 0.0)
{
    const double e = ToRadians(elevationDeg);
    const double az = ToRadians(azimuthDeg);
    return Vec3(static_cast<float>(std::cos(e) * std::cos(az)), static_cast<float>(std::sin(e)),
        static_cast<float>(std::cos(e) * std::sin(az)));
}

bool IsSurfacePoint(Vec3 v, float radiusKm)
{
    return v.x == 0.0f && v.y == radiusKm && v.z == 0.0f;
}

// Counts samples whose visibility differs from `expected`. The march starts 8 m above the surface and goes down.
int CountSweepMismatches(float radiusKm, double sunElevationDeg, double sunAzimuthDeg, float expected)
{
    constexpr int kDirections = 100;
    constexpr int kDistances = 100;
    const Vec3 origin(0.0f, radiusKm + 0.008f, 0.0f);
    const Vec3 toSun = SunDirection(sunElevationDeg, sunAzimuthDeg);

    int mismatches = 0;
    for (int i = 0; i < kDirections; ++i) {
        const double pitch = ToRadians(60.0 * i / (kDirections - 1));
        const Vec3 direction(static_cast<float>(std::cos(pitch)), static_cast<float>(-std::sin(pitch)), 0.0f);
        for (int j = 0; j < kDistances; ++j) {
            const float distanceKm = 0.01f + (10.0f - 0.01f) * static_cast<float>(j) / (kDistances - 1);
            const Vec3 sample = ClampSampleToSurfaceKm(origin + direction * distanceKm, radiusKm);
            if (PlanetVisibility(sample, toSun, radiusKm) != expected) {
                ++mismatches;
            }
        }
    }
    return mismatches;
}

TEST(SurfaceClampTest, PointAboveTheSurfaceIsReturnedUnchanged)
{
    const Vec3 p(10.0f, kR + 1.0f, 5.0f);
    EXPECT_EQ(ClampSampleToSurfaceKm(p, kR), p);
}

TEST(SurfaceClampTest, PointOnTheSurfaceIsReturnedUnchanged)
{
    const Vec3 p(0.0f, kR, 0.0f);
    EXPECT_EQ(ClampSampleToSurfaceKm(p, kR), p);
}

TEST(SurfaceClampTest, PointBelowTheSurfaceLandsOnItAndKeepsItsDirection)
{
    for (const Vec3 p : { Vec3(1000.0f, 6000.0f, 500.0f), Vec3(0.0f, kR - 8.0f, 0.0f), Vec3(-3000.0f, -200.0f, 4000.0f) }) {
        const Vec3 c = ClampSampleToSurfaceKm(p, kR);
        EXPECT_NEAR(Length(c), kR, 1e-6f * kR);
        EXPECT_GE(Dot(Normalize(c), Normalize(p)), 1.0f - 1e-6f);
    }
}

TEST(SurfaceClampTest, ZeroLengthGivesTheSurfacePointAboveTheOrigin)
{
    const Vec3 c = ClampSampleToSurfaceKm(Vec3::Zero(), kR);
    EXPECT_TRUE(IsSurfacePoint(c, kR));
}

TEST(SurfaceClampTest, NonFiniteInputGivesTheSurfacePointAboveTheOrigin)
{
    for (const Vec3 p : { Vec3(kNaN, 1.0f, 1.0f), Vec3(1.0f, kNaN, 1.0f), Vec3(1.0f, 1.0f, kNaN), Vec3(kInf, 0.0f, 0.0f),
             Vec3(0.0f, -kInf, 0.0f), Vec3(0.0f, 0.0f, kInf) }) {
        EXPECT_TRUE(IsSurfacePoint(ClampSampleToSurfaceKm(p, kR), kR));
    }
}

TEST(SurfaceClampTest, ResultIsNeverBelowThePlanetRadius)
{
    for (int i = 0; i < 200; ++i) {
        const float t = static_cast<float>(i) / 199.0f;
        const Vec3 p(-4000.0f + 8000.0f * t, -6400.0f + 12800.0f * t * t, 3000.0f - 6000.0f * t);
        EXPECT_GE(Length(ClampSampleToSurfaceKm(p, kR)), kR - 1e-6f * kR);
    }
}

TEST(PlanetVisibilityTest, SunAboveTheHorizonIsVisibleFromTheSurface)
{
    EXPECT_EQ(PlanetVisibility(Vec3(0.0f, kR, 0.0f), SunDirection(3.6), kR), 1.0f);
}

TEST(PlanetVisibilityTest, SunBelowTheHorizonIsBlockedFromTheSurface)
{
    EXPECT_EQ(PlanetVisibility(Vec3(0.0f, kR, 0.0f), SunDirection(-3.6), kR), 0.0f);
}

TEST(PlanetVisibilityTest, SunExactlyOnTheHorizonIsVisible)
{
    EXPECT_EQ(PlanetVisibility(Vec3(0.0f, kR, 0.0f), Vec3(1.0f, 0.0f, 0.0f), kR), 1.0f);
}

TEST(PlanetVisibilityTest, ElevatedPointSeesTheSunSlightlyBelowTheLocalHorizon)
{
    const Vec3 p(0.0f, kR + 0.008f, 0.0f);
    EXPECT_EQ(PlanetVisibility(p, SunDirection(-0.05), kR), 1.0f);
    EXPECT_EQ(PlanetVisibility(p, SunDirection(-1.0), kR), 0.0f);
}

TEST(PlanetVisibilityTest, DaySweepOfClampedSamplesIsAlwaysLit)
{
    for (const float radiusKm : { 6371.0f, 6360.0f, 1000.0f }) {
        for (const double azimuthDeg : { 0.0, 90.0, 180.0, 270.0 }) {
            EXPECT_EQ(CountSweepMismatches(radiusKm, 3.6, azimuthDeg, 1.0f), 0) << radiusKm << " " << azimuthDeg;
        }
    }
}

TEST(PlanetVisibilityTest, NightSweepOfClampedSamplesIsAlwaysBlocked)
{
    for (const float radiusKm : { 6371.0f, 6360.0f, 1000.0f }) {
        for (const double azimuthDeg : { 0.0, 90.0, 180.0, 270.0 }) {
            EXPECT_EQ(CountSweepMismatches(radiusKm, -3.6, azimuthDeg, 0.0f), 0) << radiusKm << " " << azimuthDeg;
        }
    }
}

TEST(PlanetVisibilityTest, NonFiniteInputIsNotBlocked)
{
    const Vec3 up(0.0f, 1.0f, 0.0f);
    EXPECT_EQ(PlanetVisibility(Vec3(kNaN, kR, 0.0f), up, kR), 1.0f);
    EXPECT_EQ(PlanetVisibility(Vec3(0.0f, kR, 0.0f), Vec3(kNaN, -1.0f, 0.0f), kR), 1.0f);
    EXPECT_EQ(PlanetVisibility(Vec3(kInf, kR, 0.0f), Vec3(0.0f, -1.0f, 0.0f), kR), 1.0f);
    EXPECT_EQ(PlanetVisibility(Vec3(0.0f, kR, 0.0f), Vec3(kInf, kInf, 0.0f), kR), 1.0f);
}

// The float test must agree with the double-precision sun transmittance: zero exactly when the planet blocks the sun.
TEST(PlanetVisibilityTest, AgreesWithSunTransmittanceBlocking)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const double radius = params.planetRadiusKm;
    int checked = 0;
    for (const float heightKm : { 0.0f, 0.008f, 0.5f, 5.0f, 50.0f }) {
        for (const float up : { -1.0f, -0.5f, -0.1f, -0.0628f, -0.01f, 0.01f, 0.0628f, 0.5f, 1.0f }) {
            const double r = radius + heightKm;
            if (up < 0.0f && std::abs(r * std::sqrt(1.0 - static_cast<double>(up) * up) - radius) < 0.01) {
                continue; // A hair from the horizon: float and double may differ.
            }
            const Vec3 position(0.0f, params.planetRadiusKm + heightKm, 0.0f);
            const Vec3 toSun(std::sqrt(1.0f - up * up), up, 0.0f);
            const Vec3 t = ComputeSunTransmittance(params, heightKm, up, 16);
            const bool blocked = t.x == 0.0f && t.y == 0.0f && t.z == 0.0f;
            EXPECT_EQ(PlanetVisibility(position, toSun, params.planetRadiusKm) == 0.0f, blocked)
                << "h=" << heightKm << " up=" << up;
            ++checked;
        }
    }
    EXPECT_GT(checked, 30);
}

} // namespace
} // namespace gte
