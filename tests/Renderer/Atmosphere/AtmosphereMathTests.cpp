// Unit tests for the Atmosphere Scattering + Aerial Perspective campaign's
// Phase 1 CPU oracle (src/Renderer/Atmosphere/AtmosphereMath.h/.cpp,
// AtmosphereParameters.h/.cpp) - see
// task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md,
// Step 3.5. No Vulkan/Renderer/live GPU device involved at all - every
// function under test is pure, taking/returning only plain
// AtmosphereParametersGpu/Vec3/float values.

#include "Renderer/Atmosphere/AtmosphereMath.h"
#include "Renderer/Atmosphere/AtmosphereParameters.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// --- RayleighDensityAtHeight() / MieDensityAtHeight() ----------------------

TEST(AtmosphereMathTest, RayleighDensityAtHeightIsOneAtSeaLevel)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    EXPECT_NEAR(RayleighDensityAtHeight(params, 0.0f), 1.0f, 1e-6f);
}

TEST(AtmosphereMathTest, MieDensityAtHeightIsOneAtSeaLevel)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    EXPECT_NEAR(MieDensityAtHeight(params, 0.0f), 1.0f, 1e-6f);
}

TEST(AtmosphereMathTest, RayleighDensityAtHeightIsMonotonicallyDecreasing)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    float previous = RayleighDensityAtHeight(params, 0.0f);
    for (float heightKm = 1.0f; heightKm <= 100.0f; heightKm += 1.0f) {
        const float current = RayleighDensityAtHeight(params, heightKm);
        EXPECT_LT(current, previous) << "at heightKm=" << heightKm;
        previous = current;
    }
}

TEST(AtmosphereMathTest, MieDensityAtHeightIsMonotonicallyDecreasing)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    float previous = MieDensityAtHeight(params, 0.0f);
    for (float heightKm = 1.0f; heightKm <= 100.0f; heightKm += 1.0f) {
        const float current = MieDensityAtHeight(params, heightKm);
        EXPECT_LT(current, previous) << "at heightKm=" << heightKm;
        previous = current;
    }
}

TEST(AtmosphereMathTest, MieDensityFallsOffFasterThanRayleighDensity)
{
    // Mie's scale height (1.2km) is much shorter than Rayleigh's (8km), so at
    // any given altitude above sea level the Mie density must have decayed
    // further.
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    EXPECT_LT(MieDensityAtHeight(params, 5.0f), RayleighDensityAtHeight(params, 5.0f));
}

// --- OzoneDensityAtHeight() -------------------------------------------------

TEST(AtmosphereMathTest, OzoneDensityPeaksAtTentCenter)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    EXPECT_NEAR(OzoneDensityAtHeight(params, params.ozoneTentCenterKm), 1.0f, 1e-6f);
}

TEST(AtmosphereMathTest, OzoneDensityIsZeroAtTentEdgesAndBeyond)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const float edgeLow = params.ozoneTentCenterKm - params.ozoneTentHalfWidthKm;
    const float edgeHigh = params.ozoneTentCenterKm + params.ozoneTentHalfWidthKm;

    EXPECT_NEAR(OzoneDensityAtHeight(params, edgeLow), 0.0f, 1e-6f);
    EXPECT_NEAR(OzoneDensityAtHeight(params, edgeHigh), 0.0f, 1e-6f);
    EXPECT_NEAR(OzoneDensityAtHeight(params, edgeLow - 10.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(OzoneDensityAtHeight(params, edgeHigh + 10.0f), 0.0f, 1e-6f);
}

TEST(AtmosphereMathTest, OzoneDensityIsSymmetricAroundTentCenter)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const float below = OzoneDensityAtHeight(params, params.ozoneTentCenterKm - 5.0f);
    const float above = OzoneDensityAtHeight(params, params.ozoneTentCenterKm + 5.0f);
    EXPECT_NEAR(below, above, 1e-6f);
    EXPECT_GT(below, 0.0f);
}

TEST(AtmosphereMathTest, OzoneDensityDegradesToZeroForNonPositiveHalfWidth)
{
    AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    params.ozoneTentHalfWidthKm = 0.0f;
    EXPECT_EQ(OzoneDensityAtHeight(params, params.ozoneTentCenterKm), 0.0f);

    params.ozoneTentHalfWidthKm = -1.0f;
    EXPECT_EQ(OzoneDensityAtHeight(params, params.ozoneTentCenterKm), 0.0f);
}

// --- ComputeExtinctionCoefficientAtHeight() ---------------------------------

TEST(AtmosphereMathTest, ExtinctionCoefficientAtSeaLevelIsPositiveEveryChannel)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 extinction = ComputeExtinctionCoefficientAtHeight(params, 0.0f);
    EXPECT_GT(extinction.x, 0.0f);
    EXPECT_GT(extinction.y, 0.0f);
    EXPECT_GT(extinction.z, 0.0f);
}

TEST(AtmosphereMathTest, ExtinctionCoefficientDecaysTowardZeroFarAboveTheAtmosphere)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 farExtinction = ComputeExtinctionCoefficientAtHeight(params, 500.0f);
    EXPECT_NEAR(farExtinction.x, 0.0f, 1e-3f);
    EXPECT_NEAR(farExtinction.y, 0.0f, 1e-3f);
    EXPECT_NEAR(farExtinction.z, 0.0f, 1e-3f);
}

// --- ComputeOpticalDepthToTopOfAtmosphere() / ComputeTransmittanceToTopOfAtmosphere() ---

TEST(AtmosphereMathTest, OpticalDepthIsZeroForNonPositiveSampleCount)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 positionKm(0.0f, params.planetRadiusKm, 0.0f);
    EXPECT_EQ(ComputeOpticalDepthToTopOfAtmosphere(params, positionKm, Vec3::Up(), 0), Vec3::Zero());
    EXPECT_EQ(ComputeOpticalDepthToTopOfAtmosphere(params, positionKm, Vec3::Up(), -5), Vec3::Zero());
}

TEST(AtmosphereMathTest, OpticalDepthIsZeroForDegenerateDirection)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 positionKm(0.0f, params.planetRadiusKm, 0.0f);
    EXPECT_EQ(ComputeOpticalDepthToTopOfAtmosphere(params, positionKm, Vec3::Zero(), 40), Vec3::Zero());
}

TEST(AtmosphereMathTest, TransmittanceIsAlwaysWithinZeroToOnePerChannel)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();

    struct Case {
        float heightKm;
        Vec3 direction;
    };
    const Case cases[] = {
        {0.0f, Vec3::Up()},
        {0.0f, Vec3(1.0f, 0.05f, 0.0f)},
        {10.0f, Vec3::Up()},
        {50.0f, Vec3(0.3f, 0.7f, 0.1f)},
        {99.0f, Vec3::Up()},
    };

    for (const Case& c : cases) {
        const Vec3 positionKm(0.0f, params.planetRadiusKm + c.heightKm, 0.0f);
        const Vec3 transmittance = ComputeTransmittanceToTopOfAtmosphere(params, positionKm, c.direction, 40);

        EXPECT_GE(transmittance.x, 0.0f);
        EXPECT_LE(transmittance.x, 1.0f);
        EXPECT_GE(transmittance.y, 0.0f);
        EXPECT_LE(transmittance.y, 1.0f);
        EXPECT_GE(transmittance.z, 0.0f);
        EXPECT_LE(transmittance.z, 1.0f);
    }
}

TEST(AtmosphereMathTest, TransmittanceStraightUpFromSeaLevelIsSmallerThanFromHighAltitude)
{
    // More air molecules sit above a sea-level observer than above one
    // already high in the atmosphere, so less light survives the trip to
    // the top of the atmosphere from sea level - straight-up transmittance
    // must be strictly smaller at sea level than at high altitude, in every
    // channel.
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();

    const Vec3 seaLevelPositionKm(0.0f, params.planetRadiusKm, 0.0f);
    const Vec3 highAltitudePositionKm(0.0f, params.planetRadiusKm + 80.0f, 0.0f);

    const Vec3 seaLevelTransmittance = ComputeTransmittanceToTopOfAtmosphere(params, seaLevelPositionKm, Vec3::Up(), 64);
    const Vec3 highAltitudeTransmittance = ComputeTransmittanceToTopOfAtmosphere(params, highAltitudePositionKm, Vec3::Up(), 64);

    EXPECT_LT(seaLevelTransmittance.x, highAltitudeTransmittance.x);
    EXPECT_LT(seaLevelTransmittance.y, highAltitudeTransmittance.y);
    EXPECT_LT(seaLevelTransmittance.z, highAltitudeTransmittance.z);
}

TEST(AtmosphereMathTest, TransmittanceAtTopOfAtmosphereIsNearlyFullyTransparent)
{
    // Starting exactly at the outer boundary and looking straight up (away
    // from the planet) leaves essentially zero atmosphere left to traverse.
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    const Vec3 positionKm(0.0f, AtmosphereRadiusKm(params), 0.0f);
    const Vec3 transmittance = ComputeTransmittanceToTopOfAtmosphere(params, positionKm, Vec3::Up(), 8);

    EXPECT_NEAR(transmittance.x, 1.0f, 1e-3f);
    EXPECT_NEAR(transmittance.y, 1.0f, 1e-3f);
    EXPECT_NEAR(transmittance.z, 1.0f, 1e-3f);
}

// --- RayleighPhaseFunction() / CornetteShanksMiePhaseFunction() ------------

TEST(AtmosphereMathTest, RayleighPhaseFunctionMatchesHandComputedValues)
{
    // p(cosTheta) = 3/(16*PI) * (1 + cosTheta^2)
    EXPECT_NEAR(RayleighPhaseFunction(1.0f), 6.0f / (16.0f * kPi), 1e-5f);
    EXPECT_NEAR(RayleighPhaseFunction(-1.0f), 6.0f / (16.0f * kPi), 1e-5f);
    EXPECT_NEAR(RayleighPhaseFunction(0.0f), 3.0f / (16.0f * kPi), 1e-5f);
}

TEST(AtmosphereMathTest, RayleighPhaseFunctionIntegratesToOneOverTheFullSphere)
{
    // A normalized phase function must integrate to exactly 1.0 over the
    // full 4*PI steradians - numerically integrate via a fine midpoint-rule
    // sweep over the polar angle (azimuthally symmetric, so dOmega =
    // 2*PI*sin(theta)*dTheta).
    constexpr int kSteps = 20000;
    const double dTheta = static_cast<double>(kPi) / kSteps;
    double total = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const double theta = (i + 0.5) * dTheta;
        const double cosTheta = std::cos(theta);
        const double p = static_cast<double>(RayleighPhaseFunction(static_cast<float>(cosTheta)));
        total += p * 2.0 * kPi * std::sin(theta) * dTheta;
    }
    EXPECT_NEAR(total, 1.0, 1e-3);
}

TEST(AtmosphereMathTest, CornetteShanksMiePhaseFunctionIntegratesToOneOverTheFullSphere)
{
    constexpr float g = 0.76f; // this campaign's own default miePhaseG
    constexpr int kSteps = 20000;
    const double dTheta = static_cast<double>(kPi) / kSteps;
    double total = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const double theta = (i + 0.5) * dTheta;
        const double cosTheta = std::cos(theta);
        const double p = static_cast<double>(CornetteShanksMiePhaseFunction(g, static_cast<float>(cosTheta)));
        total += p * 2.0 * kPi * std::sin(theta) * dTheta;
    }
    EXPECT_NEAR(total, 1.0, 1e-3);
}

TEST(AtmosphereMathTest, CornetteShanksMiePhaseFunctionIsStronglyForwardScatteringForPositiveG)
{
    // With g close to +1, far more light should scatter forward (cosTheta
    // near 1, i.e. continuing roughly the same direction) than backward
    // (cosTheta near -1).
    constexpr float g = 0.76f;
    EXPECT_GT(CornetteShanksMiePhaseFunction(g, 1.0f), CornetteShanksMiePhaseFunction(g, -1.0f));
}

// --- AtmosphereParameters.h -------------------------------------------------

TEST(AtmosphereParametersTest, DefaultEarthParametersMatchTheCitedReferenceValues)
{
    // Spot-check a handful of fields directly against
    // task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md's
    // own citation table (transcribed from _reference/pl-sky/src/app.c).
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();

    EXPECT_FLOAT_EQ(params.planetRadiusKm, 6371.0f);
    EXPECT_FLOAT_EQ(params.atmosphereThicknessKm, 100.0f);
    EXPECT_FLOAT_EQ(params.miePhaseG, 0.76f);

    EXPECT_FLOAT_EQ(params.rayleighScattering.x, 0.0058f);
    EXPECT_FLOAT_EQ(params.rayleighScattering.y, 0.0135f);
    EXPECT_FLOAT_EQ(params.rayleighScattering.z, 0.0331f);

    EXPECT_FLOAT_EQ(params.mieScattering.x, 0.006f);
    EXPECT_NEAR(params.mieAbsorption.x, 0.00066f, 1e-6f);

    EXPECT_FLOAT_EQ(params.ozoneAbsorption.x, 0.00065f);
    EXPECT_FLOAT_EQ(params.ozoneAbsorption.y, 0.00188f);
    EXPECT_FLOAT_EQ(params.ozoneAbsorption.z, 0.00008f);
}

TEST(AtmosphereParametersTest, AtmosphereRadiusKmIsPlanetRadiusPlusThickness)
{
    const AtmosphereParametersGpu params = MakeDefaultEarthAtmosphereParameters();
    EXPECT_FLOAT_EQ(AtmosphereRadiusKm(params), params.planetRadiusKm + params.atmosphereThicknessKm);
}

TEST(AtmosphereParametersTest, WorldPositionToAtmosphereSpaceKmDividesByTheConfiguredScale)
{
    const Vec3 worldPosition(1000.0f, 2000.0f, -500.0f);
    const Vec3 km = WorldPositionToAtmosphereSpaceKm(worldPosition, 1000.0f);
    EXPECT_FLOAT_EQ(km.x, 1.0f);
    EXPECT_FLOAT_EQ(km.y, 2.0f);
    EXPECT_FLOAT_EQ(km.z, -0.5f);
}

TEST(AtmosphereParametersTest, WorldPositionToAtmosphereSpaceKmUsesTheDocumentedDefaultScale)
{
    const Vec3 worldPosition(1000.0f, 0.0f, 0.0f);
    const Vec3 km = WorldPositionToAtmosphereSpaceKm(worldPosition);
    EXPECT_FLOAT_EQ(km.x, 1.0f);
}

TEST(AtmosphereParametersTest, WorldPositionToAtmosphereSpaceKmDegradesToZeroForNonPositiveScale)
{
    const Vec3 worldPosition(1000.0f, 2000.0f, -500.0f);
    EXPECT_EQ(WorldPositionToAtmosphereSpaceKm(worldPosition, 0.0f), Vec3::Zero());
    EXPECT_EQ(WorldPositionToAtmosphereSpaceKm(worldPosition, -1.0f), Vec3::Zero());
}

} // namespace
} // namespace gte
