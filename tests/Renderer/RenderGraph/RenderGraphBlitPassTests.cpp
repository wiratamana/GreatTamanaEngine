// Unit tests for RenderGraphBuilder::AddBlitPass() - proves a blit pass
// participates correctly in culling/compilation, mirroring
// RenderGraphCompilerTests.cpp's own established fixture style. No live
// VkDevice/Renderer involved - CreateTexture() mints pooled/transient
// handles, never ImportTexture().

#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TextureDesc MakeBlitTestTextureDesc()
{
    return TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
}

// A blit's destination reaching this call's own finalOutputs root set must
// keep the whole blit pass (and both its implicit src-read/dst-write
// declarations) alive through compilation.
TEST(RenderGraphBlitPassTest, BlitPassWithRootedDestinationSurvivesCompilation)
{
    RenderGraphBuilder builder;
    const TextureHandle src = builder.CreateTexture("BlitTestSource", MakeBlitTestTextureDesc());
    const TextureHandle dst = builder.CreateTexture("BlitTestDestination", MakeBlitTestTextureDesc());

    BlitSpec spec;
    spec.src = src;
    spec.dst = dst;
    builder.AddBlitPass("BlitTestPass", spec, RenderPassEvent::AfterEverything, ViewScope::Shared,
        RenderPassCategory::Debug);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { dst };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    ASSERT_EQ(compiled.executionOrder.size(), 1u);
    EXPECT_EQ(compiled.executionOrder[0].index, 0u);
    EXPECT_FALSE(input.passes[0].isCulled);
}

// The same pass, with nothing ever rooting its destination - must be
// culled entirely, same as any other dead write.
TEST(RenderGraphBlitPassTest, BlitPassWithUnrootedDestinationIsCulled)
{
    RenderGraphBuilder builder;
    const TextureHandle src = builder.CreateTexture("BlitTestSource", MakeBlitTestTextureDesc());
    const TextureHandle dst = builder.CreateTexture("BlitTestDestination", MakeBlitTestTextureDesc());

    BlitSpec spec;
    spec.src = src;
    spec.dst = dst;
    builder.AddBlitPass("BlitTestPass", spec, RenderPassEvent::AfterEverything, ViewScope::Shared,
        RenderPassCategory::Debug);

    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

} // namespace
} // namespace gte::rg
