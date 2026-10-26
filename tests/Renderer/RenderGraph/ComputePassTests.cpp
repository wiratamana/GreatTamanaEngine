// Tier-1 tests: ComputePass dispatch clamping and the inert chain (no renderer, no GPU).

#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/Shader.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(ClampDispatchSizeTest, SmallerRequestIsKept)
{
    const Extent2D size = ClampDispatchSize(Extent2D{ 32, 16 }, Extent2D{ 64, 64 });
    EXPECT_EQ(size.width, 32u);
    EXPECT_EQ(size.height, 16u);
}

TEST(ClampDispatchSizeTest, LargerRequestIsClampedPerAxis)
{
    const Extent2D size = ClampDispatchSize(Extent2D{ 200, 10 }, Extent2D{ 64, 64 });
    EXPECT_EQ(size.width, 64u);
    EXPECT_EQ(size.height, 10u);
}

TEST(ClampDispatchSizeTest, ZeroImageExtentMeansUnknown)
{
    const Extent2D size = ClampDispatchSize(Extent2D{ 200, 100 }, Extent2D{ 0, 0 });
    EXPECT_EQ(size.width, 200u);
    EXPECT_EQ(size.height, 100u);
}

TEST(ComputePassTest, ChainOnABareBuilderIsInert)
{
    RenderGraphBuilder builder;
    Shader shader("DoesNotExist");
    GpuParams<int> params;
    TextureHandle texture;

    ComputePass(builder, shader, "Test.Pass", ViewScope::Primary, RenderPassEvent::AfterOpaques)
        .Params(params, 5)
        .Bind("source", Depth(texture))
        .Bind("output", texture)
        .Push(7)
        .Dispatch(Extent2D{ 8, 8 });

    EXPECT_EQ(builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(params.Handle(), BufferObjectHandle::Invalid);
}

TEST(ComputePassTest, InertChainWithoutDispatchDoesNotAssert)
{
    RenderGraphBuilder builder;
    Shader shader("DoesNotExist");
    {
        ComputePass pass(builder, shader, "Test.Pass", ViewScope::Secondary, RenderPassEvent::Opaques);
        pass.Bind("output", TextureHandle{});
    }
    EXPECT_EQ(builder.DeclaredPassCount(), 0u);
}

TEST(ComputePassTest, GraphBufferBindOnAnInertChainDeclaresNothing)
{
    RenderGraphBuilder builder;
    Shader shader("DoesNotExist");
    ComputePass(builder, shader, "Test.Pass", ViewScope::Primary, RenderPassEvent::PreOpaques)
        .Bind("items", BufferHandle{ 1, 1 })
        .Bind("results", BufferHandle{ 2, 1 })
        .Dispatch(Extent2D{ 8, 1 });
    EXPECT_EQ(builder.DeclaredPassCount(), 0u);
}

} // namespace
} // namespace gte::rg
