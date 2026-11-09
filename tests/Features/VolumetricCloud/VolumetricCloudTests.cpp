// Tier-1 tests for the VolumetricCloud settings math and the slice viewer shader names.

#include "Features/VolumetricCloud/VolumetricCloudTypes.h"
#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>

namespace gte {
namespace {

TEST(VolumetricCloudMathTest, DefaultsAreAlreadySane)
{
    const VolumetricCloudSettings defaults;
    const VolumetricCloudSettings sanitized = SanitizeVolumetricCloudSettings(defaults);
    EXPECT_EQ(sanitized.debugView, VolumetricCloudDebugView::Off);
    EXPECT_FLOAT_EQ(sanitized.slice, defaults.slice);
    EXPECT_FLOAT_EQ(sanitized.tiling, defaults.tiling);
    EXPECT_EQ(sanitized.channel, defaults.channel);
    EXPECT_TRUE(sanitized.enabled);
}

TEST(VolumetricCloudMathTest, ClampsEveryRange)
{
    VolumetricCloudSettings s;
    s.slice = 7.0f;
    s.tiling = 1000.0f;
    s.channel = 99;
    const VolumetricCloudSettings high = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(high.slice, 1.0f);
    EXPECT_FLOAT_EQ(high.tiling, 16.0f);
    EXPECT_EQ(high.channel, kCloudDebugChannelRgb);

    s.slice = -3.0f;
    s.tiling = 0.0f;
    const VolumetricCloudSettings low = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(low.slice, 0.0f);
    EXPECT_FLOAT_EQ(low.tiling, 0.25f);
}

TEST(VolumetricCloudMathTest, NonFiniteNumbersFallBackToTheDefault)
{
    VolumetricCloudSettings s;
    s.slice = std::numeric_limits<float>::quiet_NaN();
    s.tiling = std::numeric_limits<float>::infinity();
    const VolumetricCloudSettings out = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(out.slice, VolumetricCloudSettings{}.slice);
    EXPECT_FLOAT_EQ(out.tiling, VolumetricCloudSettings{}.tiling);
}

TEST(VolumetricCloudMathTest, UnknownDebugViewTurnsOff)
{
    VolumetricCloudSettings s;
    s.debugView = static_cast<VolumetricCloudDebugView>(200);
    EXPECT_EQ(SanitizeVolumetricCloudSettings(s).debugView, VolumetricCloudDebugView::Off);
}

TEST(VolumetricCloudMathTest, PushCarriesTheSettings)
{
    VolumetricCloudSettings s;
    s.slice = 0.25f;
    s.tiling = 3.0f;
    s.channel = 2;
    const VolumetricCloudSlicePush push = MakeVolumetricCloudSlicePush(s);
    EXPECT_FLOAT_EQ(push.slice, 0.25f);
    EXPECT_FLOAT_EQ(push.tiling, 3.0f);
    EXPECT_EQ(push.channel, 2u);
}

TEST(VolumetricCloudMathTest, AssetNamesLiveUnderTheCloudsFolder)
{
    for (const char* asset : { kCloudShapeAsset, kCloudDetailAsset, kCloudWeatherAsset, kCloudBlueNoiseAsset }) {
        EXPECT_EQ(std::string(asset).rfind("clouds/", 0), 0u) << asset;
    }
}

const ReflectedDescriptorBinding* Find(const ShaderReflectionResult& result, const std::string& name)
{
    const auto it = std::find_if(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& binding) { return binding.name == name; });
    return it == result.descriptorBindings.end() ? nullptr : &*it;
}

ShaderReflectionResult ReflectCompute(const char* path)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, path };
    return ReflectShader(std::span<const ShaderStageFile>(&stage, 1));
}

void ExpectViewerShader(const char* path, const char* sampler)
{
    const ShaderReflectionResult result = ReflectCompute(path);
    const ReflectedDescriptorBinding* source = Find(result, sampler);
    ASSERT_NE(source, nullptr) << sampler;
    EXPECT_EQ(source->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    const ReflectedDescriptorBinding* output = Find(result, "destinationImage");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 2u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(VolumetricCloudSlicePush));
}

TEST(VolumetricCloudShaderNamesTest, Slice3dExposesBoundNamesAndPushBlock)
{
    ExpectViewerShader(GTE_TEST_CLOUD_SLICE3D_SPV_PATH, "noiseVolume");
}

TEST(VolumetricCloudShaderNamesTest, Slice2dExposesBoundNamesAndPushBlock)
{
    ExpectViewerShader(GTE_TEST_CLOUD_SLICE2D_SPV_PATH, "noiseTexture");
}

} // namespace
} // namespace gte
