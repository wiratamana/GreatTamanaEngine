// Pins the blend shader names the compositor binds and the push block it fills.

#include "Core/Plugins/RenderFeatureBlendPush.h"
#include "Core/Plugins/RenderFeatureNamePool.h"
#include "Renderer/RenderGraph/StaticName.h"
#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

namespace gte {
namespace {

ShaderReflectionResult ReflectBlend()
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_RENDER_FEATURE_BLEND_SPV_PATH };
    return ReflectShader(std::span<const ShaderStageFile>(&stage, 1));
}

const ReflectedDescriptorBinding* Find(const ShaderReflectionResult& result, const std::string& name)
{
    const auto it = std::find_if(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& binding) { return binding.name == name; });
    return it == result.descriptorBindings.end() ? nullptr : &*it;
}

TEST(RenderFeatureBlendTest, ShaderExposesTheBoundNames)
{
    const ShaderReflectionResult result = ReflectBlend();

    for (const char* name : { "dstIn", "srcIn" }) {
        const ReflectedDescriptorBinding* binding = Find(result, name);
        ASSERT_NE(binding, nullptr) << name;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << name;
    }
    const ReflectedDescriptorBinding* output = Find(result, "destinationImage");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 3u);
}

TEST(RenderFeatureBlendTest, PushBlockMatchesTheCppStruct)
{
    const ShaderReflectionResult result = ReflectBlend();

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(RenderFeatureBlendPush));
}

TEST(RenderFeatureBlendTest, PushCarriesTheBlendModeAsAFloat)
{
    for (const RenderFeatureBlendMode mode : { RenderFeatureBlendMode::Replace, RenderFeatureBlendMode::AlphaOver,
             RenderFeatureBlendMode::Additive, RenderFeatureBlendMode::Multiply, RenderFeatureBlendMode::ScreenSpaceMask }) {
        const RenderFeatureBlendPush push = MakeRenderFeatureBlendPush(mode);
        EXPECT_EQ(static_cast<std::uint32_t>(push.blendModeAndPad[0]), static_cast<std::uint32_t>(mode));
        EXPECT_EQ(push.blendModeAndPad[1], 0.0f);
    }
}

TEST(RenderFeatureNamePoolTest, NamesAreStableAndDistinctPerKeyAndView)
{
    RenderFeatureNamePool pool;
    const char* game = pool.BlendPassName("Fx", "Game");
    const char* scene = pool.BlendPassName("Fx", "Scene");

    EXPECT_STREQ(game, "Fx_Game_Blend");
    EXPECT_STREQ(scene, "Fx_Scene_Blend");
    EXPECT_EQ(pool.BlendPassName("Fx", "Game"), game);
    for (int i = 0; i < 200; ++i) {
        pool.PrivateName("Key" + std::to_string(i), "Game");
    }
    EXPECT_STREQ(game, "Fx_Game_Blend"); // Earlier pointers survive later insertions.
    EXPECT_STREQ(pool.SeedName("Game"), "RenderFeatureCompositor_Game_Seed");
    EXPECT_STREQ(pool.SeedCopyPassName("Scene"), "RenderFeatureCompositor_Scene_SeedCopy");
    EXPECT_STREQ(pool.FinalCopyPassName("Scene"), "RenderFeatureCompositor_Scene_FinalCopy");
}

TEST(StaticNameInternedTest, KeepsThePooledPointer)
{
    RenderFeatureNamePool pool;
    const char* name = pool.SeedName("Game");
    EXPECT_EQ(rg::StaticName::Interned(name).value, name);
}

} // namespace
} // namespace gte
