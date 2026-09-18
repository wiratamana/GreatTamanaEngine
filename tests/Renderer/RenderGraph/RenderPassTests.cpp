// Unit tests for the Render Pass campaign's PHASE1
// (task_manager/render-pass-1/PHASE1_RENDER_PASS_CORE_ABSTRACTION.md) new
// chokepoint, RenderGraphBuilder::AddRenderPass() - a thin, lightweight
// wrapper around the pre-existing AddPass()/AddComputePass() methods that
// additionally stamps PassRecord::kind/category in one place. No live
// VkDevice/Renderer/Registry involved at all - mirrors
// RenderGraphBuilderTests.cpp's own Tier-1 style exactly.

#include "Renderer/RenderGraph/RenderGraphBuilder.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

// --- AddRenderPass() with PassKind::Graphics ------------------------------

TEST(RenderPassTest, AddRenderPassWithGraphicsKindRunsSetupAndStampsGraphicsKind)
{
    RenderGraphBuilder builder;
    int setupCallCount = 0;

    builder.AddRenderPass(
        "RenderOpaque", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder&) { ++setupCallCount; },
        NoOpExecute);

    EXPECT_EQ(setupCallCount, 1);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "RenderOpaque");
    EXPECT_EQ(input.passes[0].kind, PassKind::Graphics);
    EXPECT_EQ(input.passes[0].category, RenderPassCategory::General);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::Shared);
}

// --- AddRenderPass() with PassKind::Compute -------------------------------

TEST(RenderPassTest, AddRenderPassWithComputeKindRunsSetupAndStampsComputeKind)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });
    int setupCallCount = 0;

    builder.AddRenderPass(
        "AtmosphereTransmittanceLutPass", PassKind::Compute,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            ++setupCallCount;
            pass.WriteTexture(handle);
        },
        NoOpExecute);

    EXPECT_EQ(setupCallCount, 1);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "AtmosphereTransmittanceLutPass");
    EXPECT_EQ(input.passes[0].kind, PassKind::Compute);
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::ComputeShaderWrite);
}

// --- 4-argument AddRenderPass() overload: explicit ViewScope + category ---

TEST(RenderPassTest, AddRenderPassFourArgumentOverloadStampsViewScopeAndCategory)
{
    RenderGraphBuilder builder;

    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", PassKind::Compute, ViewScope::GameView, RenderPassCategory::AtmosphereLut,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "AtmosphereSkyViewLutPass");
    EXPECT_EQ(input.passes[0].kind, PassKind::Compute);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);
    EXPECT_EQ(input.passes[0].category, RenderPassCategory::AtmosphereLut);
}

TEST(RenderPassTest, AddRenderPassFourArgumentOverloadWorksForGraphicsKindToo)
{
    RenderGraphBuilder builder;

    builder.AddRenderPass(
        "DrawSkyBackground", PassKind::Graphics, ViewScope::GameView, RenderPassCategory::General,
        [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_EQ(input.passes[0].kind, PassKind::Graphics);
    EXPECT_EQ(input.passes[0].viewScope, ViewScope::GameView);
    EXPECT_EQ(input.passes[0].category, RenderPassCategory::General);
}

// execute is captured but never invoked by AddRenderPass()/Finish() -
// mirrors AddPass()'s own equivalent guarantee (RenderGraphBuilderTests.cpp).
TEST(RenderPassTest, AddRenderPassExecuteIsNeverInvokedByAddRenderPassOrFinish)
{
    RenderGraphBuilder builder;
    int executeCallCount = 0;

    builder.AddRenderPass(
        "TestPass", PassKind::Graphics,
        [](RenderGraphBuilder::PassBuilder&) { },
        [&](PassContext&) { ++executeCallCount; });

    EXPECT_EQ(executeCallCount, 0);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_EQ(executeCallCount, 0);
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_TRUE(static_cast<bool>(input.passes[0].execute));
}

} // namespace
} // namespace gte::rg
