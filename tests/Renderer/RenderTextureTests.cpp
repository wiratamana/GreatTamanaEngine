// Tier-2 (real, headless GPU, tests/Fakes/HeadlessRenderGraphFixture.h) test
// for RenderTexture's own tracked barrier state - mirrors
// RenderTextureColorlessTests.cpp's own fixture usage/GTEST_SKIP() pattern.

#include "Renderer/RenderTexture.h"
#include "Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Fakes/HeadlessRenderGraphFixture.h"
#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(RenderTextureTest, FinalizeForExternalSamplingReadsTrackedStateNotAGuess)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }

    RenderTexture tex = fixture.GetRenderer().CreateRenderTexture(64, 64);

    const rg::ResourceState computeWriteState = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
    tex.SetCurrentState(computeWriteState);
    EXPECT_EQ(tex.CurrentState(), computeWriteState);

    const VkCommandBuffer cmd = fixture.GetRenderer().BeginOffscreenRenderGraphRecording();
    tex.FinalizeForExternalSampling(cmd);
    fixture.GetRenderer().EndOffscreenRenderGraphRecording();

    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    EXPECT_EQ(tex.CurrentState(), shaderReadState);
}

} // namespace
} // namespace gte
