// Reflects the real shadow composite shader and checks it still exposes every name the C++ side binds.

#include "Renderer/RenderGraph/ShaderBindings.h"
#include "Renderer/Vulkan/ShaderReflection.h"
#include "Renderer/Vulkan/ShaderStageMask.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <string>

namespace gte {
namespace {

ShaderReflectionResult ReflectComposite()
{
    const ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_SHADOW_COMPOSITE_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_SHADOW_COMPOSITE_FRAG_SPV_PATH },
    };
    return ReflectShader(stages);
}

const ReflectedDescriptorBinding* Find(const ShaderReflectionResult& result, const std::string& name)
{
    const auto it = std::find_if(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& binding) { return binding.name == name; });
    return it == result.descriptorBindings.end() ? nullptr : &*it;
}

TEST(ShadowCompositeShaderNamesTest, ExposesTheBoundSamplers)
{
    const ShaderReflectionResult result = ReflectComposite();

    for (const char* name : { "sceneColor", "shadowMask", "cascadeId" }) {
        const ReflectedDescriptorBinding* binding = Find(result, name);
        ASSERT_NE(binding, nullptr) << name;
        EXPECT_EQ(binding->set, 0u) << name;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << name;
        EXPECT_EQ(binding->stages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_FRAGMENT_BIT)) << name;
    }
    EXPECT_EQ(result.descriptorBindings.size(), 3u);
}

TEST(ShadowCompositeShaderNamesTest, PushBlockMatchesCompositeParamsInTheFragmentStage)
{
    const ShaderReflectionResult result = ReflectComposite();

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, 16u); // strength, debugMode, cascadeCount, pad
    EXPECT_EQ(ToShaderStageMask(result.pushConstantStages), ShaderStageMask::Fragment);
}

TEST(ShadowCompositeShaderNamesTest, SatisfiesTheFullscreenPassRules)
{
    const ShaderReflectionResult result = ReflectComposite();

    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());
}

} // namespace
} // namespace gte
