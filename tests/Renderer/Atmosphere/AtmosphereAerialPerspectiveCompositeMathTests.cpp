// Unit tests for the atmosphere-scattering-4 campaign's Phase 1 CPU oracle
// (src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp) -
// see task_manager/atmosphere-scattering-4/
// PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md, Step 3.4. No Vulkan/
// Renderer/live GPU device involved at all - every function under test is
// pure, taking/returning only plain float/Vec3 values.

#include "Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// --- ShouldBypassAerialPerspectiveComposite() -------------------------------

TEST(ShouldBypassAerialPerspectiveCompositeTest, TrueExactlyAtClearDepthValue)
{
    EXPECT_TRUE(ShouldBypassAerialPerspectiveComposite(1.0f));
}

TEST(ShouldBypassAerialPerspectiveCompositeTest, TrueAtTheDocumentedThresholdItself)
{
    EXPECT_TRUE(ShouldBypassAerialPerspectiveComposite(0.999999f));
}

TEST(ShouldBypassAerialPerspectiveCompositeTest, FalseJustBelowTheThreshold)
{
    EXPECT_FALSE(ShouldBypassAerialPerspectiveComposite(0.9999989f));
}

TEST(ShouldBypassAerialPerspectiveCompositeTest, FalseForOrdinaryOpaqueGeometryDepth)
{
    EXPECT_FALSE(ShouldBypassAerialPerspectiveComposite(0.5f));
}

// --- ComputeAerialPerspectiveCompositeColor() -------------------------------

TEST(ComputeAerialPerspectiveCompositeColorTest, BypassReturnsSceneColorUnchangedRegardlessOfAerialInputs)
{
    const Vec3 sceneColorRgb(0.2f, 0.4f, 0.6f);
    const Vec3 extremeAerialRgb(9.0f, 9.0f, 9.0f);
    const Vec3 result = ComputeAerialPerspectiveCompositeColor(
        1.0f, sceneColorRgb, extremeAerialRgb, 0.0f, 2.0f);

    EXPECT_FLOAT_EQ(result.x, sceneColorRgb.x);
    EXPECT_FLOAT_EQ(result.y, sceneColorRgb.y);
    EXPECT_FLOAT_EQ(result.z, sceneColorRgb.z);
}

TEST(ComputeAerialPerspectiveCompositeColorTest, ZeroStrengthIsAPureSceneColorPassThroughEvenWithGeometry)
{
    const Vec3 sceneColorRgb(0.3f, 0.5f, 0.7f);
    const Vec3 result = ComputeAerialPerspectiveCompositeColor(
        0.3f, sceneColorRgb, Vec3(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);

    EXPECT_FLOAT_EQ(result.x, sceneColorRgb.x);
    EXPECT_FLOAT_EQ(result.y, sceneColorRgb.y);
    EXPECT_FLOAT_EQ(result.z, sceneColorRgb.z);
}

TEST(ComputeAerialPerspectiveCompositeColorTest, FullStrengthMatchesHandComputedBlend)
{
    const Vec3 sceneColorRgb(1.0f, 1.0f, 1.0f);
    const Vec3 sampledAerialRgb(0.2f, 0.1f, 0.05f);
    const float sampledAerialA = 0.5f;
    const float strength = 1.0f;

    const Vec3 result = ComputeAerialPerspectiveCompositeColor(
        0.3f, sceneColorRgb, sampledAerialRgb, sampledAerialA, strength);

    // transmittance = mix(1.0, 0.5, 1.0) = 0.5, inScattering = (0.2,0.1,0.05)
    // sceneColor*0.5 + inScattering = (0.7, 0.6, 0.55)
    EXPECT_NEAR(result.x, 0.7f, 1e-5f);
    EXPECT_NEAR(result.y, 0.6f, 1e-5f);
    EXPECT_NEAR(result.z, 0.55f, 1e-5f);
}

TEST(ComputeAerialPerspectiveCompositeColorTest, PartialStrengthInterpolatesLinearly)
{
    const Vec3 sceneColorRgb(1.0f, 1.0f, 1.0f);
    const Vec3 sampledAerialRgb(0.2f, 0.1f, 0.05f);
    const float sampledAerialA = 0.5f;
    const float strength = 0.5f;

    const Vec3 result = ComputeAerialPerspectiveCompositeColor(
        0.3f, sceneColorRgb, sampledAerialRgb, sampledAerialA, strength);

    // transmittance = mix(1.0, 0.5, 0.5) = 0.75, inScattering = (0.1, 0.05, 0.025)
    // sceneColor*0.75 + inScattering = (0.85, 0.8, 0.775)
    EXPECT_NEAR(result.x, 0.85f, 1e-5f);
    EXPECT_NEAR(result.y, 0.8f, 1e-5f);
    EXPECT_NEAR(result.z, 0.775f, 1e-5f);
}

} // namespace
} // namespace gte
