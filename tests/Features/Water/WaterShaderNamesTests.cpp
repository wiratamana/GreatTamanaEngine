// Reflects the real Water shaders and checks they expose every name the C++ side binds.

#include "Renderer/RenderGraph/ShaderBindings.h"
#include "Renderer/Vulkan/ShaderReflection.h"
#include "Renderer/Vulkan/ShaderStageMask.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
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

void ExpectSampler(const ShaderReflectionResult& result, const char* name)
{
    const ReflectedDescriptorBinding* binding = Find(result, name);
    ASSERT_NE(binding, nullptr) << name;
    EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << name;
}

void ExpectWriteOnlyImage(const ShaderReflectionResult& result, const char* name)
{
    const ReflectedDescriptorBinding* binding = Find(result, name);
    ASSERT_NE(binding, nullptr) << name;
    EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) << name;
    EXPECT_EQ(binding->access, ReflectedAccess::WriteOnly) << name;
}

std::string ReadText(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << path;
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

TEST(WaterShaderNamesTest, ShadeShaderExposesBoundNames)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_WATER_SHADE_SPV_PATH };
    const ShaderReflectionResult result = ReflectShader(std::span<const ShaderStageFile>(&stage, 1));

    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    ExpectSampler(result, "sceneDepth");
    ExpectSampler(result, "sceneColor");
    ExpectSampler(result, "skyViewLut");
    ExpectSampler(result, "cloudShadowMap");
    ExpectWriteOnlyImage(result, "waterColorOut");
    ExpectWriteOnlyImage(result, "waterDepthOut");
    const ReflectedDescriptorBinding* bodies = Find(result, "bodies");
    ASSERT_NE(bodies, nullptr);
    EXPECT_EQ(bodies->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(bodies->access, ReflectedAccess::ReadOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 8u);
}

TEST(WaterShaderNamesTest, CompositeFitsTheFullscreenRules)
{
    const ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_WATER_COMPOSITE_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_WATER_COMPOSITE_FRAG_SPV_PATH },
    };
    const ShaderReflectionResult result = ReflectShader(stages);

    ExpectSampler(result, "waterColor");
    ExpectSampler(result, "waterDepth");
    EXPECT_EQ(result.descriptorBindings.size(), 2u);
    EXPECT_FALSE(result.hasPushConstantRange);
    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());
}

// The wave exponent and the ripple hash must keep their CPU twins in step.
TEST(WaterShaderNamesTest, ShaderTextCarriesTheSharedConstants)
{
    const std::string text = ReadText(GTE_TEST_WATER_SHADE_GLSL_PATH);
    EXPECT_NE(text.find("float s = 0.5 + 0.5 * sin(theta);"), std::string::npos);
    EXPECT_NE(text.find("6.2831853 / w.dirAndK.z"), std::string::npos);
    EXPECT_NE(text.find("kDirectScale = 0.65"), std::string::npos);
}

} // namespace
} // namespace gte
