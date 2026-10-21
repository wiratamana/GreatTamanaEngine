// CPU oracle of the aerial perspective composite pass. Every function under test is pure.

#include "Features/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace gte {
namespace {

const Vec3 kNoShaft = Vec3::Zero();

Vec3 Color(float depth, const Vec3& scene, const Vec3& aerialRgb, float aerialA, const Vec3& shaft, float strength,
    float skyIntensity = 1.0f)
{
    return ComputeAerialPerspectiveCompositeColor(depth, scene, aerialRgb, aerialA, shaft, strength, skyIntensity);
}

void ExpectEqual(const Vec3& a, const Vec3& b)
{
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.z, b.z);
}

TEST(AerialCompositeMathTest, ClearDepthBoundary)
{
    for (const float depth : { 0.999999f, 0.9999995f, 1.0f, 1.5f }) {
        EXPECT_TRUE(IsClearDepth(depth)) << depth;
    }
    for (const float depth : { 0.0f, 0.5f, 0.9f, 0.9999989f }) {
        EXPECT_FALSE(IsClearDepth(depth)) << depth;
    }
}

TEST(AerialCompositeMathTest, SkyWithZeroDeltaIsTheExactSceneColor)
{
    const Vec3 scene(0.2f, 0.4f, 0.6f);
    ExpectEqual(Color(1.0f, scene, Vec3(9.0f, 9.0f, 9.0f), 0.0f, kNoShaft, 1.0f, 6.0f), scene);
}

TEST(AerialCompositeMathTest, SkyWithNegativeDeltaIsDarkerAndNeverNegative)
{
    const Vec3 scene(1.0f, 1.0f, 1.0f);
    const Vec3 shaft(-0.1f, -0.2f, -5.0f);
    const Vec3 result = Color(1.0f, scene, Vec3::Zero(), 1.0f, shaft, 1.0f, 2.0f);
    EXPECT_NEAR(result.x, 0.8f, 1e-6f);
    EXPECT_NEAR(result.y, 0.6f, 1e-6f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
    EXPECT_LE(result.x, scene.x);
    EXPECT_LE(result.y, scene.y);
}

TEST(AerialCompositeMathTest, SkyIgnoresTheVolumeSample)
{
    const Vec3 scene(0.5f, 0.5f, 0.5f);
    const Vec3 shaft(-0.1f, -0.1f, -0.1f);
    const Vec3 a = Color(1.0f, scene, Vec3(0.0f, 0.0f, 0.0f), 1.0f, shaft, 1.0f);
    const Vec3 b = Color(1.0f, scene, Vec3(7.0f, 7.0f, 7.0f), 0.0f, shaft, 1.0f);
    ExpectEqual(a, b);
}

TEST(AerialCompositeMathTest, ZeroStrengthPassesSkyAndGeometryThrough)
{
    const Vec3 scene(0.3f, 0.5f, 0.7f);
    const Vec3 shaft(-1.0f, -1.0f, -1.0f);
    ExpectEqual(Color(1.0f, scene, Vec3(1.0f, 1.0f, 1.0f), 0.2f, shaft, 0.0f, 6.0f), scene);
    ExpectEqual(Color(0.3f, scene, Vec3(1.0f, 1.0f, 1.0f), 1.0f, shaft, 0.0f, 6.0f), scene);
}

TEST(AerialCompositeMathTest, GeometryFullStrengthMatchesHandComputedBlend)
{
    // transmittance = 0.5, inScattering = (0.2, 0.1, 0.05).
    const Vec3 result = Color(0.3f, Vec3(1.0f, 1.0f, 1.0f), Vec3(0.2f, 0.1f, 0.05f), 0.5f, kNoShaft, 1.0f);
    EXPECT_NEAR(result.x, 0.7f, 1e-5f);
    EXPECT_NEAR(result.y, 0.6f, 1e-5f);
    EXPECT_NEAR(result.z, 0.55f, 1e-5f);
}

TEST(AerialCompositeMathTest, GeometryPartialStrengthInterpolatesLinearly)
{
    // transmittance = 0.75, inScattering = (0.1, 0.05, 0.025).
    const Vec3 result = Color(0.3f, Vec3(1.0f, 1.0f, 1.0f), Vec3(0.2f, 0.1f, 0.05f), 0.5f, kNoShaft, 0.5f);
    EXPECT_NEAR(result.x, 0.85f, 1e-5f);
    EXPECT_NEAR(result.y, 0.8f, 1e-5f);
    EXPECT_NEAR(result.z, 0.775f, 1e-5f);
}

TEST(AerialCompositeMathTest, GeometryIgnoresTheShaftDelta)
{
    const Vec3 scene(0.4f, 0.35f, 0.9f);
    ExpectEqual(Color(0.5f, scene, Vec3::Zero(), 1.0f, Vec3(-3.0f, -3.0f, -3.0f), 1.0f), scene);
}

TEST(AerialCompositeMathTest, ShaderDepthLiteralEqualsTheCpuConstant)
{
    std::ifstream file(GTE_TEST_SCENE_DEPTH_CONSTANTS_GLSL_PATH);
    ASSERT_TRUE(file.is_open());
    std::stringstream text;
    text << file.rdbuf();

    std::ostringstream expected;
    expected << "kClearDepthThreshold = " << kAerialPerspectiveFarPlaneDepthThreshold << ";";
    EXPECT_NE(text.str().find(expected.str()), std::string::npos) << expected.str();
}

} // namespace
} // namespace gte
