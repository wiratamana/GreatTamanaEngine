// Text checks for VolumetricCloudSunShafts.comp and the sun-shafts composite in VolumetricCloudResolve.comp.

#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace gte {
namespace {

std::string ReadShaderText(const char* file)
{
    std::ifstream in(std::filesystem::path(GTE_TEST_CLOUD_SHADER_DIR) / file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

TEST(VolumetricCloudSunShaftsShaderTest, ShaderExistsAndDeclaresWorkgroupAndBindings)
{
    const std::string shader = ReadShaderText("VolumetricCloudSunShafts.comp");
    ASSERT_FALSE(shader.empty());
    EXPECT_NE(shader.find("local_size_x = 8"), std::string::npos);
    EXPECT_NE(shader.find("local_size_y = 8"), std::string::npos);

    EXPECT_NE(shader.find("readonly buffer VolumetricCloudSunShaftsParams"), std::string::npos);
    const std::size_t m0 = shader.find("vec4 sunUvAndReachAndIntensity;");
    const std::size_t m1 = shader.find("vec4 decayTapsAnisoContrast;");
    const std::size_t m2 = shader.find("vec4 sunColorAndHazeMix;");
    const std::size_t m3 = shader.find("vec4 frameFadeAspectActive;");
    const std::size_t m4 = shader.find("vec4 depthParams;");
    const std::size_t blockEnd = shader.find("} params;");
    ASSERT_NE(m0, std::string::npos);
    ASSERT_NE(m1, std::string::npos);
    ASSERT_NE(m2, std::string::npos);
    ASSERT_NE(m3, std::string::npos);
    ASSERT_NE(m4, std::string::npos);
    ASSERT_NE(blockEnd, std::string::npos);
    EXPECT_LT(m0, m1);
    EXPECT_LT(m1, m2);
    EXPECT_LT(m2, m3);
    EXPECT_LT(m3, m4);
    EXPECT_LT(m4, blockEnd);

    EXPECT_NE(shader.find("uniform sampler2D historyColor;"), std::string::npos);
    EXPECT_NE(shader.find("uniform sampler2D sceneDepth;"), std::string::npos);
    EXPECT_NE(shader.find("rgba16f) uniform writeonly image2D shaftsOut;"), std::string::npos);
}

TEST(VolumetricCloudSunShaftsShaderTest, ShaderUsesSharedIncludesGuardsAndNoDiscard)
{
    const std::string shader = ReadShaderText("VolumetricCloudSunShafts.comp");
    ASSERT_FALSE(shader.empty());
    EXPECT_NE(shader.find("#include \"SceneDepthConstants.glsl\""), std::string::npos);
    EXPECT_NE(shader.find("#include \"InterleavedGradientNoise.glsl\""), std::string::npos);
    EXPECT_NE(shader.find("kClearDepthThreshold"), std::string::npos);
    EXPECT_EQ(shader.find("0.999999"), std::string::npos);
    EXPECT_EQ(shader.find("float InterleavedGradientNoise("), std::string::npos);

    EXPECT_NE(shader.find("imageSize(shaftsOut)"), std::string::npos);
    EXPECT_NE(shader.find("isnan("), std::string::npos);
    EXPECT_NE(shader.find("isinf("), std::string::npos);
    EXPECT_NE(shader.find("Saturate("), std::string::npos);
    EXPECT_NE(shader.find("max("), std::string::npos);
    EXPECT_NE(shader.find("clamp("), std::string::npos);
    EXPECT_NE(shader.find("kTapJitterScale = 0.6"), std::string::npos);
    EXPECT_NE(shader.find("floor(vec2(pixel) * 0.5)"), std::string::npos);
    EXPECT_NE(shader.find("kOpenSkyBaseline = 0.25"), std::string::npos);
    EXPECT_NE(shader.find("1.0 - kOpenSkyBaseline"), std::string::npos);
    EXPECT_NE(shader.find("kThickCloudKeep = 0.65"), std::string::npos);
    EXPECT_NE(shader.find("kThickOccluderMaxS = 0.5"), std::string::npos);
    EXPECT_NE(shader.find("kBlockedRampLow = 0.04"), std::string::npos);
    EXPECT_NE(shader.find("kBlockedRampHigh = 0.45"), std::string::npos);
    EXPECT_NE(shader.find("1.0 - rayFrac"), std::string::npos);
    EXPECT_NE(shader.find("blockedRamp"), std::string::npos);
    EXPECT_NE(shader.find("max(coreHalo, blockedRamp)"), std::string::npos);
    EXPECT_NE(shader.find("mix(0.35, 1.0, blockedRamp)"), std::string::npos);
    EXPECT_EQ(shader.find("kPartialOcclusionVar"), std::string::npos);
    EXPECT_NE(shader.find("depthParams"), std::string::npos);
    EXPECT_NE(shader.find("params.depthParams"), std::string::npos);
    EXPECT_NE(shader.find("LinearizeDepth("), std::string::npos);
    EXPECT_NE(shader.find("DepthFromLinearM("), std::string::npos);
    EXPECT_NE(shader.find("kGeometryAirFloor = 0.0"), std::string::npos);
    EXPECT_NE(shader.find("kNearOccluderMinM = 4.0"), std::string::npos);
    EXPECT_NE(shader.find("nearTapDepth"), std::string::npos);
    EXPECT_NE(shader.find("exp(-linearDistanceM / airLengthM)"), std::string::npos);
    for (const std::string_view removed : { "kNearAir", "kNearAirDepth", "kNearAirRange", "kNearAirMinWeight" }) {
        EXPECT_EQ(shader.find(removed), std::string::npos) << removed;
    }
    EXPECT_NE(shader.find("kShaftStorageScale = 0.5"), std::string::npos);
    EXPECT_NE(shader.find("kShaftSoftCap = 3.0"), std::string::npos);
    EXPECT_NE(shader.find("imageStore(shaftsOut, pixel, vec4(clamp(color, vec3(0.0), vec3(kShaftSoftCap)), rayFrac));"),
        std::string::npos);
    EXPECT_EQ(shader.find("vec3(kShaftSoftCap)), 1.0)"), std::string::npos);
    EXPECT_EQ(shader.find("discard"), std::string::npos);

    for (std::size_t at = shader.find("texture("); at != std::string::npos; at = shader.find("texture(", at + 1)) {
        const bool part = at > 0 && (std::isalnum(static_cast<unsigned char>(shader[at - 1])) || shader[at - 1] == '_');
        EXPECT_TRUE(part) << "VolumetricCloudSunShafts.comp uses implicit LOD texture()";
    }
}

TEST(VolumetricCloudSunShaftsShaderTest, ResolveCompositesSunShaftsAndAngularSpokesWithoutLegacySunRays)
{
    const std::string resolve = ReadShaderText("VolumetricCloudResolve.comp");
    ASSERT_FALSE(resolve.empty());
    EXPECT_NE(resolve.find("uniform sampler2D sunShafts;"), std::string::npos);
    EXPECT_NE(resolve.find("sunRaysSpoke"), std::string::npos);
    EXPECT_NE(resolve.find("const float kShaftGain = 2.0;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kShaftCloudKeep = 0.5;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSpokeWidth = 0.012;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSpokeGain = 6.0;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSpokeFalloff = 1.5;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSpokeSoftCap = 3.0;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSpokeMinLuma = 1e-3;"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSpokeMinDist = 1e-5;"), std::string::npos);
    EXPECT_NE(resolve.find("textureLod(sunShafts, uv, 0.0)"), std::string::npos);
    EXPECT_NE(resolve.find("textureLod(sunShafts, clamp(uv - tangent * kSpokeWidth, uvMin, uvMax), 0.0).a"), std::string::npos);
    EXPECT_NE(resolve.find("textureLod(sunShafts, clamp(uv + tangent * kSpokeWidth, uvMin, uvMax), 0.0).a"), std::string::npos);
    EXPECT_NE(resolve.find("max(0.0, centerAlpha - 0.5 * (aLeft + aRight))"), std::string::npos);
    EXPECT_NE(resolve.find("exp(-distToSun * kSpokeFalloff)"), std::string::npos);
    EXPECT_NE(resolve.find("params.sunRayColor.rgb"), std::string::npos);
    EXPECT_NE(resolve.find("params.sunRay.z"), std::string::npos);
    EXPECT_NE(resolve.find("1.0 + peak / kSpokeSoftCap"), std::string::npos);
    EXPECT_NE(resolve.find("kShaftGain * shaftCloudWeight"), std::string::npos);
    EXPECT_NE(resolve.find("SunShaftSpokes(uv, shaftCenter, spokeStrength) * shaftCloudWeight"), std::string::npos);
    EXPECT_EQ(resolve.find("SunRays("), std::string::npos);
    for (const std::string_view removed : { "kRayDeadZone", "kRaySurroundRadius", "openHere", "onGround", "discard" }) {
        EXPECT_EQ(resolve.find(removed), std::string::npos) << removed;
    }
}

} // namespace
} // namespace gte
