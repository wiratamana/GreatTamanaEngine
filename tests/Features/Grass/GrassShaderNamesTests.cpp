// Reflects the real Grass shaders and checks they expose every name and layout the C++ side relies on.

#include "Features/Grass/GrassTypes.h"
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

void ExpectStorage(const ShaderReflectionResult& result, const char* name, ReflectedAccess access)
{
    const ReflectedDescriptorBinding* binding = Find(result, name);
    ASSERT_NE(binding, nullptr) << name;
    EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) << name;
    EXPECT_EQ(binding->access, access) << name;
}

std::string ReadText(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << path;
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

TEST(GrassShaderNamesTest, InitShaderExposesBoundNames)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_GRASS_INIT_SPV_PATH };
    const ShaderReflectionResult result = ReflectShader(std::span<const ShaderStageFile>(&stage, 1));
    ExpectStorage(result, "params", ReflectedAccess::ReadOnly);
    ExpectStorage(result, "commands", ReflectedAccess::WriteOnly);
    ExpectStorage(result, "stats", ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 3u);
    EXPECT_EQ(result.localSize.x, kGrassCullGroupSize);
}

TEST(GrassShaderNamesTest, CullShaderExposesBoundNamesAndGroupSize)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_GRASS_CULL_SPV_PATH };
    const ShaderReflectionResult result = ReflectShader(std::span<const ShaderStageFile>(&stage, 1));
    ExpectStorage(result, "params", ReflectedAccess::ReadOnly);
    ExpectStorage(result, "heightTile", ReflectedAccess::ReadOnly);
    ExpectStorage(result, "instances", ReflectedAccess::WriteOnly);
    ExpectStorage(result, "commands", ReflectedAccess::ReadWrite);
    ExpectStorage(result, "stats", ReflectedAccess::ReadWrite);
    ExpectStorage(result, "shadow", ReflectedAccess::ReadOnly);
    for (const char* sampler :
        { "hizAtlas", "shadowDepth0", "shadowDepth1", "shadowDepth2", "shadowDepth3", "cloudShadowMap" }) {
        const ReflectedDescriptorBinding* binding = Find(result, sampler);
        ASSERT_NE(binding, nullptr) << sampler;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << sampler;
    }
    EXPECT_EQ(result.descriptorBindings.size(), 12u);
    EXPECT_EQ(result.localSize.x, kGrassCullGroupSize);

    const BufferObjectHandle buf = static_cast<BufferObjectHandle>(1);
    const rg::BufferHandle tracked{ 1, 1 };
    const rg::TextureHandle colorTex{ 1, 1 };
    const rg::TextureHandle depthTex{ 2, 1 };
    const rg::BindingArgument args[] = {
        { "params", rg::BindingResourceKind::Buffer, {}, buf, {} },
        { "shadow", rg::BindingResourceKind::Buffer, {}, buf, {} },
        { "heightTile", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "instances", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "commands", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "stats", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "hizAtlas", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth0", rg::BindingResourceKind::DepthTexture, depthTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth1", rg::BindingResourceKind::DepthTexture, depthTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth2", rg::BindingResourceKind::DepthTexture, depthTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth3", rg::BindingResourceKind::DepthTexture, depthTex, BufferObjectHandle::Invalid, {} },
        { "cloudShadowMap", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
    };
    const auto describeTexture = [&](rg::TextureHandle h) -> std::optional<rg::TextureDesc> {
        rg::TextureDesc desc{};
        desc.hasColor = (h == colorTex);
        desc.hasDepth = (h == depthTex);
        return desc;
    };
    const rg::BindingPlanResult plan = rg::PlanBindings(result.descriptorBindings, args, describeTexture);
    EXPECT_TRUE(plan.Ok()) << plan.error;
}

TEST(GrassShaderNamesTest, DrawShadersFitTheFullscreenRules)
{
    const ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_GRASS_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_GRASS_FRAG_SPV_PATH },
    };
    const ShaderReflectionResult result = ReflectShader(stages);
    ExpectStorage(result, "params", ReflectedAccess::ReadOnly);
    ExpectStorage(result, "instances", ReflectedAccess::ReadOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 2u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(GrassPushGpu));
    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());
}

// The CPU and the shaders count the same words and vertices.
TEST(GrassShaderNamesTest, ShaderTextCarriesTheSharedConstants)
{
    const std::string text = ReadText(GTE_TEST_GRASS_COMMON_GLSL_PATH);
    EXPECT_NE(text.find("const uint kGrassVertsList1 = 12u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassVertsList2 = 3u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kStatSlot0 = 21u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kStatDropped0 = 18u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassStatsWords = 32u;"), std::string::npos);
    EXPECT_EQ(GrassVertexCount(1, 5), 12u);
    EXPECT_EQ(GrassVertexCount(2, 5), 3u);
    EXPECT_EQ(static_cast<std::uint32_t>(GrassStatsWord::Slot0), 21u);
    EXPECT_EQ(static_cast<std::uint32_t>(GrassStatsWord::Dropped0), 18u);

    // The params block keeps its member order: the first and the last members.
    const std::size_t block = text.find("GrassParamsBlock {");
    ASSERT_NE(block, std::string::npos);
    EXPECT_LT(text.find("mat4 viewProjection;", block), text.find("mat4 drawViewProjection;", block));
    EXPECT_LT(text.find("vec4 sunRadiance;", block), text.find("vec4 depthMap;", block));
    EXPECT_LT(text.find("vec4 depthMap;", block), text.find("vec4 look;", block));
    EXPECT_LT(text.find("vec4 look;", block), text.find("vec4 extra;", block));
}

} // namespace
} // namespace gte
