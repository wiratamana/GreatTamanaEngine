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

#include "Renderer/Renderer.h"
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

// PHASE5 (PHASE5_SUBMIT_AND_FRAMERECORDER_BIND_WIRING.md) - debug-only
// death test for Renderer::SubmitIndirect()'s ONE allowed functional change
// (PHASE0's second scope boundary): a Pipeline that carries a real
// sceneServicesSetLayout (legal per Pipeline's own contiguous-pSetLayouts
// logic, PHASE3) must never reach SubmitIndirect()'s actual bind logic -
// this project's own ctest Debug build does NOT define NDEBUG, so assert()
// stays LIVE, mirroring RenderViewRegistryTests.cpp's own
// "probe-then-EXPECT_DEATH, freshly-constructed-inside-the-lambda fixture"
// idiom exactly. A real, legally-indexed Mesh and a real
// VertexLayout::PositionNormalInstanced Pipeline are used so the two
// EARLIER asserts inside SubmitIndirect() (recording-in-progress,
// mesh.HasIndexBuffer()) both pass cleanly first, proving THIS specific
// assert (not an earlier one) is what fires.
//
// Pulled out into its own free function (rather than inline inside
// EXPECT_DEATH's own statement argument) because the preprocessor's macro
// argument splitting only respects PARENTHESES, never braces - an
// aggregate initializer list's own top-level commas (see `vertices` below)
// would otherwise be mis-parsed as extra EXPECT_DEATH() arguments.
#ifndef NDEBUG

void SubmitIndirectAgainstPipelineWithSceneServicesSet()
{
    HeadlessRenderGraphFixture fixture;
    Renderer& renderer = fixture.GetRenderer();
    const VkDevice device = renderer.GetVulkanContextInfo().device;

    // Stands in for Core's real SceneServicesDescriptorSet::Layout()
    // (PHASE2/PHASE7) - only Renderer::SubmitIndirect()'s own new misuse
    // guard is under test here.
    const VkDescriptorSetLayout throwawaySceneServicesLayout =
        DescriptorSetLayoutBuilder(device).AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    Pipeline pipeline = renderer.CreatePipeline("shaders/MeshInstanced.vert.spv", "shaders/Mesh.frag.spv",
        VertexLayout::PositionNormalInstanced, /*useMaterialTexture=*/false,
        "RendererSubmitIndirectDeathTest.Pipeline", /*useInstanceBuffer=*/true, throwawaySceneServicesLayout);

    // A trivial, real, indexed triangle - HasIndexBuffer() == true, so
    // SubmitIndirect()'s own earlier mesh.HasIndexBuffer() assert passes
    // cleanly before this phase's new assert is ever reached.
    MeshVertex vertices[3]{};
    vertices[0].position[0] = 0.0f;
    vertices[0].position[1] = 0.0f;
    vertices[0].position[2] = 0.0f;
    vertices[1].position[0] = 1.0f;
    vertices[1].position[1] = 0.0f;
    vertices[1].position[2] = 0.0f;
    vertices[2].position[0] = 0.0f;
    vertices[2].position[1] = 1.0f;
    vertices[2].position[2] = 0.0f;
    for (MeshVertex& v : vertices) {
        v.normal[0] = 0.0f;
        v.normal[1] = 0.0f;
        v.normal[2] = 1.0f;
    }
    std::uint32_t indices[3]{ 0, 1, 2 };
    Mesh mesh = renderer.CreateMesh(
        vertices, sizeof(vertices), 3, indices, sizeof(indices), 3, "RendererSubmitIndirectDeathTest.Mesh");

    const VkDescriptorSet instanceBufferDescriptorSet =
        renderer.AllocateComputeDescriptorSet(renderer.InstanceBufferDescriptorSetLayout());

    // SubmitIndirect()'s FIRST assert requires a render-graph pass
    // recording to already be in progress.
    const VkCommandBuffer cmd = renderer.BeginOffscreenRenderGraphRecording();
    renderer.BeginGraphPassRecording(cmd, {});

    // indirectBuffer/countBuffer are never dereferenced - this phase's new
    // assert fires before IssueIndirectDrawCommand() is ever called.
    renderer.SubmitIndirect(pipeline, mesh, /*indirectBuffer=*/VK_NULL_HANDLE, /*indirectOffset=*/0,
        /*maxDrawCount=*/1, /*countBuffer=*/VK_NULL_HANDLE, /*countBufferOffset=*/0, instanceBufferDescriptorSet);
}

TEST(RendererSubmitIndirectDeathTest, PipelineWithSceneServicesSetAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(SubmitIndirectAgainstPipelineWithSceneServicesSet(), "");
}

#endif

} // namespace
} // namespace gte
