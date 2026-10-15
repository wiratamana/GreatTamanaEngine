// Reflects the real shadow mask shader and checks it still exposes every name the C++ side binds.

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

TEST(ShadowShaderNamesTest, MaskShaderExposesBoundNames)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_SHADOW_MASK_SPV_PATH };
    const ShaderReflectionResult result = ReflectShader(std::span<const ShaderStageFile>(&stage, 1));

    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);

    const ReflectedDescriptorBinding* shadowDepth = Find(result, "shadowDepth");
    ASSERT_NE(shadowDepth, nullptr);
    EXPECT_EQ(shadowDepth->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    const ReflectedDescriptorBinding* sceneDepth = Find(result, "sceneDepth");
    ASSERT_NE(sceneDepth, nullptr);
    EXPECT_EQ(sceneDepth->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    const ReflectedDescriptorBinding* maskOutput = Find(result, "maskOutput");
    ASSERT_NE(maskOutput, nullptr);
    EXPECT_EQ(maskOutput->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(maskOutput->access, ReflectedAccess::WriteOnly);

    EXPECT_EQ(result.descriptorBindings.size(), 4u);
}

} // namespace
} // namespace gte
