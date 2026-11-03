// Reflects the real TemporalAa resolve shader and checks it keeps the names the C++ side binds.
// Also pins the GLSL constants that mirror the CPU reference.

#include "Renderer/Vulkan/ShaderReflection.h"
#include "Renderer/Vulkan/ShaderStageMask.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <span>
#include <sstream>
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

TEST(TemporalAaShaderNamesTest, ResolveShaderExposesBoundNames)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_TEMPORALAA_RESOLVE_SPV_PATH };
    const ShaderReflectionResult result = ReflectShader(std::span<const ShaderStageFile>(&stage, 1));

    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);

    for (const char* name : { "sceneColor", "sceneDepth", "colorHistory", "depthHistory" }) {
        ExpectSampler(result, name);
    }
    ExpectWriteOnlyImage(result, "colorOutput");
    ExpectWriteOnlyImage(result, "depthOutput");

    EXPECT_EQ(result.descriptorBindings.size(), 7u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

std::string ReadShaderSource()
{
    std::ifstream file(GTE_TEST_TEMPORALAA_RESOLVE_GLSL_PATH, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << GTE_TEST_TEMPORALAA_RESOLVE_GLSL_PATH;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

TEST(TemporalAaShaderNamesTest, GlslConstantsMatchTheCpuReference)
{
    const std::string source = ReadShaderSource();
    EXPECT_NE(source.find("#include \"ScreenSpaceCommon.glsl\""), std::string::npos);
    EXPECT_NE(source.find("kLumaFloor = 0.2"), std::string::npos);
    EXPECT_NE(source.find("1.0 + Luma(c)"), std::string::npos);
    EXPECT_NE(source.find("1.5 * f3 - 2.5 * f2 + 1.0"), std::string::npos);
    EXPECT_NE(source.find("textureGather(depthHistory"), std::string::npos);
    EXPECT_NE(source.find("shared vec3 tileColor"), std::string::npos);
}

} // namespace
} // namespace gte
