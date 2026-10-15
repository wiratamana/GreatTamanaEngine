// Tier-1 tests for one-shot final-state planning and write-back, plus a Tier-2 compute run on the headless GPU.

#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/OneShot.h"
#include "Renderer/RenderGraph/OneShotPlan.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/Shader.h"
#include "Renderer/RenderTexture.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <vector>

namespace gte::rg {
namespace {

const ResourceState kShaderRead = RequiredStateFor(ResourceAccess::ShaderRead, false);
const ResourceState kComputeWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);

TEST(OneShotPlanTest, RestoreReturnsTheInitialState)
{
    const std::optional<ResourceState> planned = PlanFinalState(kComputeWrite, FinalState::Restore);
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(*planned, kComputeWrite);
}

TEST(OneShotPlanTest, ShaderReadIgnoresTheInitialState)
{
    const std::optional<ResourceState> planned = PlanFinalState(kComputeWrite, FinalState::ShaderRead);
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(*planned, kShaderRead);
}

TEST(OneShotPlanTest, KeepRequestsNoTransition)
{
    EXPECT_FALSE(PlanFinalState(kShaderRead, FinalState::Keep).has_value());
}

TEST(OneShotPlanTest, WriteBackSkipsUntouchedAndOutOfRangeTextures)
{
    const std::vector<TextureHandle> handles{ TextureHandle{ 0, 1 }, TextureHandle{ 1, 1 }, TextureHandle{ 9, 1 } };
    const std::vector<std::optional<ResourceState>> states{ kShaderRead, std::nullopt };

    std::vector<std::size_t> written;
    WriteBackStates(handles, states, [&written](std::size_t i, const ResourceState& state) {
        written.push_back(i);
        EXPECT_EQ(state, kShaderRead);
    });
    EXPECT_EQ(written, std::vector<std::size_t>{ 0 });
}

TEST(OneShotPlanTest, NoStatesMeansNothingIsWrittenBack)
{
    const std::vector<TextureHandle> handles{ TextureHandle{ 0, 1 } };
    int calls = 0;
    WriteBackStates(handles, std::vector<std::optional<ResourceState>>{}, [&calls](std::size_t, const ResourceState&) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(OneShotGpuTest, ComputeRunRestoresTheSourceAndLeavesTheOutputReadable)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    Shader shader("ReflectFixture", ShaderKind::Compute);
    GpuParams<std::array<float, 4>> params;

    RenderTexture inputA = renderer.CreateRenderTexture(16, 16, PixelFormat::RGBA8_UNorm, "OneShotInputA");
    RenderTexture inputB = renderer.CreateRenderTexture(16, 16, PixelFormat::RGBA8_UNorm, "OneShotInputB");
    RenderTexture output = renderer.CreateRenderTexture(16, 16, PixelFormat::R8_UNorm, "OneShotOutput", nullptr,
        /*allowStorageImageAccess=*/true, false, /*createDepthCompanion=*/false);
    inputA.SetCurrentState(kShaderRead);
    inputB.SetCurrentState(kShaderRead);

    OneShot oneShot(renderer);
    const TextureHandle a = oneShot.ImportReadOnly(inputA);
    const TextureHandle b = oneShot.ImportReadOnly(inputB);
    const TextureHandle out = oneShot.Import(output, FinalState::ShaderRead);
    ComputePass(oneShot.Builder(), shader, "OneShotTest", ViewScope::Shared, RenderPassEvent::Opaques)
        .Params(params, std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.0f })
        .Bind("inputDepthA", a)
        .Bind("inputDepthB", b)
        .Bind("maskOutput", out)
        .Dispatch(Extent2D{ 16, 16 });

    ASSERT_TRUE(oneShot.Run());
    EXPECT_EQ(inputA.CurrentState(), kShaderRead);
    EXPECT_EQ(output.CurrentState(), kShaderRead);
}

TEST(OneShotGpuTest, CopyRunLeavesTheDestinationReadable)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderTexture source = renderer.CreateRenderTexture(16, 16, PixelFormat::RGBA8_UNorm, "OneShotCopySource");
    RenderTexture target = renderer.CreateRenderTexture(16, 16, PixelFormat::RGBA8_UNorm, "OneShotCopyTarget", nullptr,
        false, false, true, true, /*transferSrc=*/false, /*transferDst=*/true);
    source.SetCurrentState(kShaderRead);

    OneShot oneShot(renderer);
    ASSERT_TRUE(oneShot.CopyTexture(oneShot.ImportReadOnly(source), oneShot.Import(target, FinalState::ShaderRead)));
    ASSERT_TRUE(oneShot.Run());
    EXPECT_EQ(target.CurrentState(), kShaderRead);
}

TEST(OneShotGpuTest, RunningTwiceFailsAndKeepsTheStates)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RenderTexture texture = fixture.GetRenderer().CreateRenderTexture(8, 8, PixelFormat::RGBA8_UNorm, "OneShotTwice");
    texture.SetCurrentState(kShaderRead);

    OneShot oneShot(fixture.GetRenderer());
    oneShot.ImportReadOnly(texture);
    ASSERT_TRUE(oneShot.Run());
    EXPECT_FALSE(oneShot.Run());
    EXPECT_EQ(texture.CurrentState(), kShaderRead);
}

} // namespace
} // namespace gte::rg
