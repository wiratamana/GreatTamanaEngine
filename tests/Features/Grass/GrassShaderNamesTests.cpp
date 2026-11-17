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
    const auto describeTexture = [&](rg::TextureHandle h) -> std::optional<rg::TextureDesc> {
        rg::TextureDesc desc{};
        desc.hasColor = (h == colorTex);
        desc.hasDepth = (h == depthTex);
        return desc;
    };

    const rg::BindingArgument withCascades[] = {
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
    const rg::BindingPlanResult planWithCascades = rg::PlanBindings(result.descriptorBindings, withCascades, describeTexture);
    EXPECT_TRUE(planWithCascades.Ok()) << planWithCascades.error;

    // Without a published shadow receiver in PreOpaque, a 1x1 colour stand-in fills the unused sampler slots.
    const rg::BindingArgument withoutCascades[] = {
        { "params", rg::BindingResourceKind::Buffer, {}, buf, {} },
        { "shadow", rg::BindingResourceKind::Buffer, {}, buf, {} },
        { "heightTile", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "instances", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "commands", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "stats", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
        { "hizAtlas", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth0", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth1", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth2", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
        { "shadowDepth3", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
        { "cloudShadowMap", rg::BindingResourceKind::Texture, colorTex, BufferObjectHandle::Invalid, {} },
    };
    const rg::BindingPlanResult planWithoutCascades =
        rg::PlanBindings(result.descriptorBindings, withoutCascades, describeTexture);
    EXPECT_TRUE(planWithoutCascades.Ok()) << planWithoutCascades.error;
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
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(GrassPushGpu));
    EXPECT_EQ(offsetof(GrassPushGpu, viewportHeight), 8u);
    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());
}

TEST(GrassShaderNamesTest, ShadowShadersFitDepthOnlyFullscreenRules)
{
    const ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_GRASS_SHADOW_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_GRASS_SHADOW_FRAG_SPV_PATH },
    };
    const ShaderReflectionResult result = ReflectShader(stages);
    ExpectStorage(result, "params", ReflectedAccess::ReadOnly);
    ExpectStorage(result, "instances", ReflectedAccess::ReadOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 2u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(GrassShadowPushGpu));
    EXPECT_FALSE(rg::ValidateFullscreenReflection(result).has_value());

    const BufferObjectHandle buf = static_cast<BufferObjectHandle>(1);
    const rg::BufferHandle tracked{ 1, 1 };
    const rg::BindingArgument args[] = {
        { "params", rg::BindingResourceKind::Buffer, {}, buf, {} },
        { "instances", rg::BindingResourceKind::TrackedBuffer, {}, BufferObjectHandle::Invalid, tracked },
    };
    const rg::BindingPlanResult bindingPlan = rg::PlanBindings(result.descriptorBindings, args, {});
    EXPECT_TRUE(bindingPlan.Ok()) << bindingPlan.error;

    // Cascade shadow draws attach no colour target and write depth with CompareOp::Less.
    rg::TextureDesc cascadeDesc{};
    cascadeDesc.width = 1024;
    cascadeDesc.height = 1024;
    cascadeDesc.hasColor = false;
    cascadeDesc.hasDepth = true;
    cascadeDesc.depthFormat = PixelFormat::D32_Float;
    rg::FullscreenDepthRequest depth{};
    depth.enabled = true;
    depth.desc = cascadeDesc;
    depth.compare = CompareOp::Less;
    depth.write = true;
    const rg::FullscreenAttachmentPlan attachmentPlan =
        rg::PlanFullscreenAttachments({}, depth, PixelFormat::D32_Float, BlendMode::Opaque);
    ASSERT_TRUE(attachmentPlan.Ok()) << attachmentPlan.error;
    EXPECT_EQ(attachmentPlan.key.colorCount, 0u);
    EXPECT_TRUE(attachmentPlan.key.depthTest);
    EXPECT_TRUE(attachmentPlan.key.depthWrite);
    EXPECT_EQ(attachmentPlan.key.depthCompare, CompareOp::Less);
}

// The CPU and the shaders count the same words, commands and vertices.
TEST(GrassShaderNamesTest, ShaderTextCarriesTheSharedConstants)
{
    const std::string text = ReadText(GTE_TEST_GRASS_COMMON_GLSL_PATH);
    EXPECT_NE(text.find("const uint kGrassVertsList1 = 12u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassVertsList2 = 3u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassCommandCount = 6u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kStatSlot0 = 21u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kStatDropped0 = 18u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassStatsWords = 32u;"), std::string::npos);
    EXPECT_EQ(kGrassCommandCount, 6u);
    EXPECT_EQ(kGrassCommandBytes, 96u);
    EXPECT_EQ(GrassVertexCount(1, 5), 12u);
    EXPECT_EQ(GrassVertexCount(2, 5), 3u);
    EXPECT_EQ(static_cast<std::uint32_t>(GrassStatsWord::Slot0), 21u);
    EXPECT_EQ(static_cast<std::uint32_t>(GrassStatsWord::Dropped0), 18u);
    EXPECT_EQ(static_cast<std::uint32_t>(GrassStatsWord::ShadowSlot0), 24u);
    EXPECT_EQ(static_cast<std::uint32_t>(GrassStatsWord::ShadowSlot1), 25u);
    EXPECT_NE(text.find("const uint kStatShadowSlot0 = 24u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kStatShadowSlot1 = 25u;"), std::string::npos);

    const std::string bladeText = ReadText(GTE_TEST_GRASS_BLADE_GLSL_PATH);
    EXPECT_NE(bladeText.find("float GrassBladeWidth("), std::string::npos);
    EXPECT_NE(bladeText.find("vec3 GrassBladeSpine("), std::string::npos);
    EXPECT_NE(bladeText.find("vec3 GrassBladePosition("), std::string::npos);

    // The params block keeps its member order: the first and the last members.
    const std::size_t block = text.find("GrassParamsBlock {");
    ASSERT_NE(block, std::string::npos);
    EXPECT_LT(text.find("mat4 viewProjection;", block), text.find("mat4 drawViewProjection;", block));
    EXPECT_LT(text.find("vec4 sunRadiance;", block), text.find("vec4 depthMap;", block));
    EXPECT_LT(text.find("vec4 depthMap;", block), text.find("vec4 look;", block));
    EXPECT_LT(text.find("vec4 look;", block), text.find("vec4 extra;", block));
    EXPECT_LT(text.find("vec4 extra;", block), text.find("vec4 densityLog;", block));
    EXPECT_LT(text.find("vec4 densityLog;", block), text.find("vec4 variety;", block));
    EXPECT_LT(text.find("vec4 variety;", block), text.find("vec4 variety2;", block));
    EXPECT_LT(text.find("vec4 variety2;", block), text.find("vec4 shapeWind;", block));
    EXPECT_LT(text.find("vec4 shapeWind;", block), text.find("vec4 lodFar;", block));
    EXPECT_NE(text.find("(720 bytes"), std::string::npos);
    EXPECT_EQ(sizeof(GrassParamsGpu), 720u);
    EXPECT_NE(text.find("const uint kGrassPatchMax = 12u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassKindFlower = 13u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassKindDry = 14u;"), std::string::npos);
    EXPECT_NE(text.find("const uint kGrassKindWeed = 15u;"), std::string::npos);
}

} // namespace
} // namespace gte
