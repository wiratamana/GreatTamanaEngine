// Reflects the real Gtao shaders and checks they still expose every name the C++ side binds.

#include "Renderer/RenderGraph/ShaderBindings.h"
#include "Renderer/Vulkan/ShaderReflection.h"
#include "Renderer/Vulkan/ShaderStageMask.h"

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

TEST(GtaoShaderNamesTest, MainShaderExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_GTAO_MAIN_SPV_PATH);

    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    ExpectSampler(result, "sceneDepth");

    const ReflectedDescriptorBinding* output = Find(result, "aoOutput");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 3u);
}

TEST(GtaoShaderNamesTest, ResolveShaderExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_GTAO_RESOLVE_SPV_PATH);

    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    ExpectSampler(result, "aoRaw");
    ExpectSampler(result, "aoHistory");

    const ReflectedDescriptorBinding* output = Find(result, "aoOutput");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 4u);
}

ShaderReflectionResult ReflectComposite()
{
    const ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_GTAO_COMPOSITE_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_GTAO_COMPOSITE_FRAG_SPV_PATH },
    };
    return ReflectShader(stages);
}

TEST(GtaoShaderNamesTest, CompositeExposesTheBoundSamplersAndPushBlock)
{
    const ShaderReflectionResult result = ReflectComposite();

    for (const char* name : { "sceneColor", "sceneDepth", "aoTexture" }) {
        ExpectSampler(result, name);
    }
    EXPECT_EQ(result.descriptorBindings.size(), 3u);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, 128u);
    EXPECT_EQ(ToShaderStageMask(result.pushConstantStages), ShaderStageMask::Fragment);
    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());
}

} // namespace
} // namespace gte
