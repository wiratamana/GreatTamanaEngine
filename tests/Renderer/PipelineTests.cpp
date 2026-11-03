// Pipeline tests: set-layout bookkeeping and fixed-state mapping.
// Device-backed tests skip when the headless Vulkan surface is unavailable.

#include "Renderer/Pipeline.h"

#include "Renderer/Mesh.h"
#include "Renderer/MeshVertex.h"
#include "Renderer/PipelineStateMapping.h"

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include "Renderer/DescriptorSet.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph/RecordingAccess.h"
#include "Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>

namespace gte {
namespace {

TEST(PipelineTest, SceneServicesSetLayoutProducesHasSceneServicesSetTrueWithSyntheticSetZeroFiller)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = fixture.Device();

    // Throwaway 1-binding layout standing in for the scene-services set; only Pipeline's set-array/filler logic is tested.
    const VkDescriptorSetLayout throwawaySceneServicesLayout =
        DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    {
        // VertexLayout::PositionColor, no materialSetLayout/bufferSetLayout: set 0 has no real content, so
        // Pipeline must synthesize a zero-binding filler layout to keep set 1 contiguous.
        Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/Triangle.vert.spv",
            "shaders/Triangle.frag.spv", VertexLayout::PositionColor,
            /*materialSetLayout=*/VK_NULL_HANDLE, /*debugName=*/"PipelineTest.SceneServicesSetLayout",
            /*bufferSetLayout=*/VK_NULL_HANDLE, throwawaySceneServicesLayout);

        EXPECT_TRUE(pipeline.HasSceneServicesSet());
        EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));
        EXPECT_NE(pipeline.Layout(), static_cast<VkPipelineLayout>(VK_NULL_HANDLE));
    }

    vkDestroyDescriptorSetLayout(device, throwawaySceneServicesLayout, nullptr);
}

TEST(PipelineTest, UnchangedConstructionWithNoSceneServicesSetLayoutReportsHasSceneServicesSetFalse)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = fixture.Device();

    // Every positional argument up through debugName, exactly as any
    // pre-existing call site would supply - sceneServicesSetLayout left at
    // its default (VK_NULL_HANDLE).
    Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/Triangle.vert.spv",
        "shaders/Triangle.frag.spv", VertexLayout::PositionColor, VK_NULL_HANDLE,
        "PipelineTest.NoSceneServicesSet");

    EXPECT_FALSE(pipeline.HasSceneServicesSet());
    EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));
}

// Mirrors the two SceneServicesSetLayout tests above, for Pipeline's
// materialSetLayout parameter + HasMaterialSet() accessor.

TEST(PipelineTest, MaterialSetLayoutProducesHasMaterialSetTrue)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = fixture.Device();

    // A small, throwaway, 1-binding layout - only Pipeline's own
    // materialSetLayout-is-non-null bookkeeping is under test here, not
    // whether it is binding-compatible with a real MaterialTexture.
    const VkDescriptorSetLayout throwawayMaterialLayout =
        DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/TexturedMesh.vert.spv",
        "shaders/TexturedMesh.frag.spv", VertexLayout::PositionNormalUv, throwawayMaterialLayout,
        "PipelineTest.MaterialSetLayout");

    EXPECT_TRUE(pipeline.HasMaterialSet());
    EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));

    vkDestroyDescriptorSetLayout(device, throwawayMaterialLayout, nullptr);
}

TEST(PipelineTest, TangentVertexLayoutWithMaterialSetLayoutBuilds)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = fixture.Device();

    const VkDescriptorSetLayout throwawayMaterialLayout =
        DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/TexturedMesh.vert.spv",
        "shaders/TexturedMesh.frag.spv", VertexLayout::PositionNormalUvTangent, throwawayMaterialLayout,
        "PipelineTest.TangentLayout");

    EXPECT_TRUE(pipeline.HasMaterialSet());
    EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));

    vkDestroyDescriptorSetLayout(device, throwawayMaterialLayout, nullptr);
}


TEST(PipelineTest, UnchangedConstructionWithNoMaterialSetLayoutReportsHasMaterialSetFalse)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = fixture.Device();

    // Every positional argument up through debugName, exactly as any
    // pre-existing call site would supply - materialSetLayout left at its
    // default (VK_NULL_HANDLE).
    Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/Triangle.vert.spv",
        "shaders/Triangle.frag.spv", VertexLayout::PositionColor, VK_NULL_HANDLE, "PipelineTest.NoMaterialSet");

    EXPECT_FALSE(pipeline.HasMaterialSet());
    EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));
}

TEST(ScenePipelineStateTest, DefaultsAndEquality)
{
    const ScenePipelineState a{};
    EXPECT_EQ(a.cull, CullMode::Back);
    EXPECT_EQ(a.depthCompare, CompareOp::Less);
    EXPECT_TRUE(a.depthWrite);
    EXPECT_EQ(a.blend, BlendMode::Opaque);
    EXPECT_EQ(a, ScenePipelineState{});

    ScenePipelineState b{};
    b.cull = CullMode::Front;
    EXPECT_FALSE(a == b);
}

TEST(ScenePipelineStateTest, DefaultMapsToVulkanBackLessOpaque)
{
    const PipelineFixedState fixed = ToPipelineFixedState(ScenePipelineState{});
    EXPECT_EQ(fixed.cullMode, static_cast<VkCullModeFlags>(VK_CULL_MODE_BACK_BIT));
    EXPECT_EQ(fixed.depthCompare, VK_COMPARE_OP_LESS);
    EXPECT_TRUE(fixed.depthWrite);
    EXPECT_EQ(fixed.blend.blendEnable, static_cast<VkBool32>(VK_FALSE));
    EXPECT_EQ(fixed.blend.colorWriteMask, OpaqueBlendAttachment().colorWriteMask);
}

TEST(ScenePipelineStateTest, FrontCullLessEqualMapsToVulkan)
{
    ScenePipelineState state{};
    state.cull = CullMode::Front;
    state.depthCompare = CompareOp::LessEqual;
    state.depthWrite = false;
    const PipelineFixedState fixed = ToPipelineFixedState(state);
    EXPECT_EQ(fixed.cullMode, static_cast<VkCullModeFlags>(VK_CULL_MODE_FRONT_BIT));
    EXPECT_EQ(fixed.depthCompare, VK_COMPARE_OP_LESS_OR_EQUAL);
    EXPECT_FALSE(fixed.depthWrite);
}

TEST(ScenePipelineStateTest, AdditiveBlendIsOneOnePerChannelAndOnlyAdditiveBlends)
{
    ScenePipelineState state{};
    state.blend = BlendMode::Additive;
    const VkPipelineColorBlendAttachmentState additive = ToPipelineFixedState(state).blend;
    EXPECT_EQ(additive.blendEnable, static_cast<VkBool32>(VK_TRUE));
    EXPECT_EQ(additive.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(additive.dstColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(additive.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(additive.dstAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(additive.colorBlendOp, VK_BLEND_OP_ADD);

    state.blend = BlendMode::Opaque;
    EXPECT_EQ(ToPipelineFixedState(state).blend.blendEnable, static_cast<VkBool32>(VK_FALSE));
}

TEST(ScenePipelineStateTest, FullscreenAdditiveBlendKeepsSourceAlphaFactor)
{
    EXPECT_EQ(ToVkBlendState(BlendMode::Additive).srcColorBlendFactor, VK_BLEND_FACTOR_SRC_ALPHA);
}

TEST(ScenePipelineStateTest, SceneBlendNeverRewritesDestinationAlphaForAlphaBlend)
{
    const VkPipelineColorBlendAttachmentState opaque = ToVkSceneBlendState(BlendMode::Opaque);
    EXPECT_EQ(opaque.blendEnable, static_cast<VkBool32>(VK_FALSE));

    const VkPipelineColorBlendAttachmentState alpha = ToVkSceneBlendState(BlendMode::AlphaBlend);
    EXPECT_EQ(alpha.blendEnable, static_cast<VkBool32>(VK_TRUE));
    EXPECT_EQ(alpha.srcColorBlendFactor, VK_BLEND_FACTOR_SRC_ALPHA);
    EXPECT_EQ(alpha.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(alpha.srcAlphaBlendFactor, VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(alpha.dstAlphaBlendFactor, VK_BLEND_FACTOR_ONE);

    // The fullscreen mapping is unchanged.
    EXPECT_EQ(ToVkBlendState(BlendMode::AlphaBlend).srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(ToVkBlendState(BlendMode::AlphaBlend).dstAlphaBlendFactor, VK_BLEND_FACTOR_ZERO);
}

TEST(PipelineTest, FixedStateFormBuildsWithFrontCullAdditiveLessEqual)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkFormat colorFormat = renderer.ColorFormat();
    ScenePipelineState state{};
    state.cull = CullMode::Front;
    state.depthCompare = CompareOp::LessEqual;
    state.depthWrite = false;
    state.blend = BlendMode::Additive;

    Pipeline pipeline(fixture.Device(), std::span<const VkFormat>(&colorFormat, 1), renderer.DepthFormat(),
        "shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv", VertexLayout::PositionColor, VK_NULL_HANDLE,
        "PipelineTest.FixedState", VK_NULL_HANDLE, VK_NULL_HANDLE, ToPipelineFixedState(state));

    EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));
}

} // namespace
} // namespace gte
