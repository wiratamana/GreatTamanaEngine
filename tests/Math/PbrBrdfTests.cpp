// Tier-1 tests for the scalar BRDF reference and its agreement with the GLSL text.

#include "Math/PbrBrdf.h"
#include "../Support/Std140Block.h"

#include <string>

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(PbrBrdfTest, DistributionGgxMatchesClosedForms)
{
    EXPECT_NEAR(pbr::DistributionGgx(1.0f, 0.25f), 5.0930f, 0.001f);
    EXPECT_NEAR(pbr::DistributionGgx(0.3f, 1.0f), 0.3183f, 0.0005f);
}

TEST(PbrBrdfTest, VisibilityIsOneQuarterHeadOn)
{
    for (const float alpha : { 0.1f, 0.5f, 1.0f }) {
        EXPECT_NEAR(pbr::VisibilitySmithGgxCorrelated(1.0f, 1.0f, alpha), 0.25f, 1e-5f);
    }
}

TEST(PbrBrdfTest, FresnelEndpointsAndMidpoint)
{
    EXPECT_NEAR(pbr::FresnelSchlick(0.04f, 1.0f), 0.04f, 1e-6f);
    EXPECT_NEAR(pbr::FresnelSchlick(0.04f, 0.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(pbr::FresnelSchlick(0.04f, 0.5f), 0.07f, 1e-5f);
}

TEST(PbrBrdfTest, EnvBrdfFitMatchesReferenceValues)
{
    EXPECT_NEAR(pbr::EnvBrdfApprox(0.04f, 0.0f, 1.0f), 0.0456f, 0.001f);
    EXPECT_NEAR(pbr::EnvBrdfApprox(1.0f, 1.0f, 1.0f), 0.45f, 0.001f);
}

TEST(PbrBrdfTest, SpecularOcclusionEndpoints)
{
    EXPECT_NEAR(pbr::SpecularOcclusion(1.0f, 1.0f, 0.5f), 1.0f, 1e-5f);
    EXPECT_NEAR(pbr::SpecularOcclusion(1.0f, 0.0f, 0.5f), 0.0f, 1e-5f);
}

TEST(PbrBrdfTest, MinimumRoughnessPeakNeedsTheRadianceClamp)
{
    const float alpha = pbr::kMinPerceptualRoughness * pbr::kMinPerceptualRoughness;
    EXPECT_GT(pbr::DistributionGgx(1.0f, alpha), 7e4f);
    EXPECT_FLOAT_EQ(pbr::ClampSceneRadiance(1e6f), 8192.0f);
    EXPECT_FLOAT_EQ(pbr::ClampSceneRadiance(1.0f), 1.0f);
}

TEST(PbrBrdfTest, ShaderTextCarriesTheSameConstantsAndFormulas)
{
    const std::string text = test_support::ReadTextFile(GTE_TEST_PBR_BRDF_GLSL_PATH)
        + test_support::ReadTextFile(GTE_TEST_PBR_MATERIAL_GLSL_PATH);
    for (const char* needle : { "kMinPerceptualRoughness = 0.045", "kDielectricF0 = 0.04", "kMaxSceneRadiance = 8192.0",
             "-9.28", "-0.0275", "0.0425", "1.04", "exp2(-16.0 * roughness - 1.0)", "kDirectScale * kPi" }) {
        EXPECT_NE(text.find(needle), std::string::npos) << needle;
    }
}

} // namespace
} // namespace gte
