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

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include "Renderer/Renderer.h"
#include "Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(PipelineTest, SceneServicesSetLayoutProducesHasSceneServicesSetTrueWithSyntheticSetZeroFiller)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = renderer.GetVulkanContextInfo().device;

    // A small, throwaway, 1-binding layout - stands in for Core's real
    // SceneServicesDescriptorSet::Layout() (PHASE2), which this phase does
    // not yet consume (that is PHASE7's job) - only Pipeline's OWN
    // contiguous-set-array/synthetic-filler logic is under test here.
    const VkDescriptorSetLayout throwawaySceneServicesLayout =
        DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    {
        // VertexLayout::PositionColor, no materialSetLayout/
        // instanceBufferSetLayout - set = 0 has no real content, so Pipeline
        // must synthesize its own zero-binding filler layout to keep set = 1
        // contiguous.
        Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/Triangle.vert.spv",
            "shaders/Triangle.frag.spv", VertexLayout::PositionColor,
            /*materialSetLayout=*/VK_NULL_HANDLE, /*debugName=*/"PipelineTest.SceneServicesSetLayout",
            /*instanceBufferSetLayout=*/VK_NULL_HANDLE, throwawaySceneServicesLayout);

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
    const VkDevice device = renderer.GetVulkanContextInfo().device;

    // Every positional argument up through debugName, exactly as any
    // pre-existing call site would supply - sceneServicesSetLayout left at
    // its default (VK_NULL_HANDLE).
    Pipeline pipeline(device, renderer.ColorFormat(), renderer.DepthFormat(), "shaders/Triangle.vert.spv",
        "shaders/Triangle.frag.spv", VertexLayout::PositionColor, VK_NULL_HANDLE,
        "PipelineTest.NoSceneServicesSet");

    EXPECT_FALSE(pipeline.HasSceneServicesSet());
    EXPECT_NE(pipeline.Native(), static_cast<VkPipeline>(VK_NULL_HANDLE));
}

} // namespace
} // namespace gte
