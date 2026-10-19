// Reflects the real VolumetricLight shaders and checks they still expose every name the C++ side binds.

#include "Features/VolumetricLight/VolumetricLightTypes.h"
#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <string>

namespace gte {
namespace {

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

void ExpectSampler(const ShaderReflectionResult& result, const char* name)
{
    const ReflectedDescriptorBinding* binding = Find(result, name);
    ASSERT_NE(binding, nullptr) << name;
    EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << name;
}

void ExpectParams(const ShaderReflectionResult& result)
{
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
}

void ExpectOutput(const ShaderReflectionResult& result, const char* name)
{
    const ReflectedDescriptorBinding* output = Find(result, name);
    ASSERT_NE(output, nullptr) << name;
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) << name;
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly) << name;
}

TEST(VolumetricLightShaderNamesTest, InjectExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_VOLUMETRIC_INJECT_SPV_PATH);

    ExpectParams(result);
    for (const char* name : { "shadowDepth0", "shadowDepth1", "shadowDepth2", "shadowDepth3" }) {
        ExpectSampler(result, name);
    }
    ExpectOutput(result, "injectOutput");
    EXPECT_EQ(result.descriptorBindings.size(), 6u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricLightShaderNamesTest, TemporalExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_VOLUMETRIC_TEMPORAL_SPV_PATH);

    ExpectParams(result);
    ExpectSampler(result, "rawVolume");
    ExpectSampler(result, "historyAtlas");
    ExpectOutput(result, "blendedOutput");
    ExpectOutput(result, "atlasOutput");
    EXPECT_EQ(result.descriptorBindings.size(), 5u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricLightShaderNamesTest, IntegrateExposesBoundNamesAndPushBlock)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_VOLUMETRIC_INTEGRATE_SPV_PATH);

    ExpectSampler(result, "blendedVolume");
    ExpectOutput(result, "integratedOutput");
    EXPECT_EQ(result.descriptorBindings.size(), 2u);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(VolumetricLightIntegratePush));
}

TEST(VolumetricLightShaderNamesTest, CompositeExposesBoundNamesAndPushBlock)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_VOLUMETRIC_COMPOSITE_SPV_PATH);

    for (const char* name : { "sourceColor", "sourceDepth", "integratedVolume" }) {
        ExpectSampler(result, name);
    }
    ExpectOutput(result, "destinationImage");
    EXPECT_EQ(result.descriptorBindings.size(), 4u);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(VolumetricLightCompositePush));
}

} // namespace
} // namespace gte
