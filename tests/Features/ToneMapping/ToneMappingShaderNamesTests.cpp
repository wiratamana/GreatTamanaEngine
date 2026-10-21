// Reflects the real ToneMapping shader and checks it keeps the names the C++ side binds.
// Also checks the GLSL curve constants against the CPU reference.

#include "Math/ToneMapCurves.h"
#include "Renderer/Vulkan/ShaderReflection.h"
#include "Renderer/Vulkan/ShaderStageMask.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
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

TEST(ToneMappingShaderNamesTest, ShaderExposesBoundNamesAndPushBlock)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_TONEMAPPING_SPV_PATH };
    const ShaderReflectionResult result = ReflectShader(std::span<const ShaderStageFile>(&stage, 1));

    const ReflectedDescriptorBinding* source = Find(result, "sourceColor");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    const ReflectedDescriptorBinding* output = Find(result, "destinationImage");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 2u);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, 32u);
}

std::string ReadShaderSource()
{
    std::ifstream file(GTE_TEST_TONEMAPPING_GLSL_PATH, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << GTE_TEST_TONEMAPPING_GLSL_PATH;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::string Shortest(float value)
{
    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), value);
    return std::string(text, written.ptr);
}

TEST(ToneMappingShaderNamesTest, GlslCurveConstantsMatchTheCpuReference)
{
    const std::string source = ReadShaderSource();
    for (const float constant : { tonemap::kAcesA, tonemap::kAcesB, tonemap::kAcesC, tonemap::kAcesD,
             tonemap::kAcesE, tonemap::kPbrDesaturation }) {
        EXPECT_NE(source.find(Shortest(constant)), std::string::npos) << Shortest(constant);
    }
    EXPECT_NE(source.find("0.8 - 0.04"), std::string::npos);
    EXPECT_NEAR(tonemap::kPbrStartCompression, 0.76f, 1e-6f);
}

} // namespace
} // namespace gte
