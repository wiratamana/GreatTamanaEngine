// Reflects the real Ssgi shaders and checks they expose every name the C++ side binds.

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

void ExpectWriteOnlyImage(const ShaderReflectionResult& result, const char* name)
{
    const ReflectedDescriptorBinding* binding = Find(result, name);
    ASSERT_NE(binding, nullptr) << name;
    EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) << name;
    EXPECT_EQ(binding->access, ReflectedAccess::WriteOnly) << name;
}

void ExpectReadOnlyParams(const ShaderReflectionResult& result)
{
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
}

std::string ReadText(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << path;
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void ExpectContains(const std::string& text, const char* needle, const char* file)
{
    EXPECT_NE(text.find(needle), std::string::npos) << file << " lacks " << needle;
}

TEST(SsgiShaderNamesTest, PrepareShaderExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_SSGI_PREPARE_SPV_PATH);
    ExpectReadOnlyParams(result);
    ExpectSampler(result, "sceneDepth");
    ExpectSampler(result, "sceneColor");
    ExpectWriteOnlyImage(result, "ssgiColorOut");
    ExpectWriteOnlyImage(result, "ssgiNormalOut");
    EXPECT_EQ(result.descriptorBindings.size(), 5u);
}

TEST(SsgiShaderNamesTest, TraceShaderExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_SSGI_TRACE_SPV_PATH);
    ExpectReadOnlyParams(result);
    ExpectSampler(result, "ssgiColor");
    ExpectSampler(result, "ssgiNormal");
    ExpectWriteOnlyImage(result, "ssgiRaw");
    EXPECT_EQ(result.descriptorBindings.size(), 4u);
}

TEST(SsgiShaderNamesTest, ResolveShaderExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_SSGI_RESOLVE_SPV_PATH);
    ExpectReadOnlyParams(result);
    ExpectSampler(result, "ssgiRaw");
    ExpectSampler(result, "ssgiHistory");
    ExpectWriteOnlyImage(result, "ssgiOutput");
    EXPECT_EQ(result.descriptorBindings.size(), 4u);
}

TEST(SsgiShaderNamesTest, CompositeExposesTheBoundSamplersAndPushBlock)
{
    const ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_SSGI_COMPOSITE_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_SSGI_COMPOSITE_FRAG_SPV_PATH },
    };
    const ShaderReflectionResult result = ReflectShader(stages);

    for (const char* name : { "sceneColor", "sceneDepth", "giTexture", "ssgiNormal" }) {
        ExpectSampler(result, name);
    }
    EXPECT_EQ(result.descriptorBindings.size(), 4u);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, 128u);
    EXPECT_EQ(ToShaderStageMask(result.pushConstantStages), ShaderStageMask::Fragment);
    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());
}

// The shader text must carry the constants the C++ oracle uses.
TEST(SsgiShaderNamesTest, ShaderTextMatchesOracleConstants)
{
    const std::string common = ReadText(GTE_TEST_SSGI_COMMON_GLSL_PATH);
    ExpectContains(common, "vec3(0.2126, 0.7152, 0.0722)", "SsgiCommon.glsl");
    ExpectContains(common, "kFootprintBiasScale = 1.5", "SsgiCommon.glsl");
    ExpectContains(common, "vec2(0.7548776662, 0.5698402910)", "SsgiCommon.glsl");
    ExpectContains(common, "0.6180339887", "SsgiCommon.glsl");
    ExpectContains(common, "vec2(5.3, 17.1)", "SsgiCommon.glsl");
    ExpectContains(common, "vec2(3.1, 5.7)", "SsgiCommon.glsl");

    const std::string noise = ReadText((std::string(GTE_TEST_SHADERS_DIR) + "/InterleavedGradientNoise.glsl").c_str());
    ExpectContains(noise, "52.9829189", "InterleavedGradientNoise.glsl");
    ExpectContains(noise, "vec2(0.06711056, 0.00583715)", "InterleavedGradientNoise.glsl");

    const std::string composite = ReadText(GTE_TEST_SSGI_COMPOSITE_GLSL_PATH);
    ExpectContains(composite, "vec3(0.25), vec3(4.0)", "SsgiComposite.frag");
    ExpectContains(composite, "vec3(0.95)", "SsgiComposite.frag");
}

} // namespace
} // namespace gte
