// Tier-2 (real, headless GPU) smoke test for Pipeline's new, trailing,
// defaulted `sceneServicesSetLayout` constructor parameter - Block 4 "Global
// Scene Services Descriptor Set" campaign (task_manager/better-render-pass-6),
// PHASE3 (PHASE3_PIPELINE_SET1_WIRING.md). This codebase has no pre-existing
// PipelineTests.cpp - per that phase file's own instruction, this is
// deliberately a MINIMAL, device-backed smoke test, not a large new test
// infrastructure: confirm a Pipeline built with a real, throwaway
// VkDescriptorSetLayout passed as `sceneServicesSetLayout` reports
// HasSceneServicesSet() == true and does not throw (VertexLayout::
// PositionColor - no set = 0 content, so Pipeline must synthesize its own
// zero-binding filler layout for set = 0 - see Pipeline.cpp), and that an
// UNCHANGED existing construction (no sceneServicesSetLayout at all) still
// reports HasSceneServicesSet() == false, exactly as before this parameter
// existed.
//
// Uses the SAME HeadlessRenderGraphFixture (tests/Fakes/HeadlessRenderGraphFixture.h)
// every other real-device Renderer test in this codebase already uses -
// GTEST_SKIP()-guarded exactly like every other HeadlessSurfaceProvider
// consumer whenever this machine's Vulkan driver/loader doesn't report
// VK_EXT_headless_surface.

#include "Renderer/Pipeline.h"

#include "Renderer/Mesh.h"
#include "Renderer/MeshVertex.h"

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include "Renderer/DescriptorSet.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph/RecordingAccess.h"
#include "Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <gtest/gtest.h>

#include <cstdint>

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

} // namespace
} // namespace gte
