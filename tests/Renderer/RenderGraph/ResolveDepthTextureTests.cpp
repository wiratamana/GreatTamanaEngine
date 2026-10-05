// Tier-2 (real, headless GPU) proof that PassContext::resolveDepthTexture()
// works correctly for an imported depth-carrying texture even when NOTHING
// else in the same graph execution ever declares a depth-attachment write
// against it first - the "untested territory" case EnsureTextureResolved()'s
// own doc comment (RenderGraph.cpp) flags: an imported resource's depth
// sub-resource is always seeded to a synthetic "never touched" tracked
// state, regardless of its real prior state. Built on the same
// HeadlessRenderGraphFixture every other Tier-2 test in this folder uses.

#include "Renderer/Renderer.h"
#include "Renderer/RenderTexture.h"
#include "Renderer/RenderGraph/RenderGraph.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <array>

namespace gte::rg {
namespace {

TEST(ResolveDepthTextureTest, ResolvesNonNullViewAndSamplerWithNoPriorSameFrameDepthAttachmentWrite)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    // allowDepthSampledAccess=true gives this a real depth sampler - the
    // one real precondition resolveDepthTexture() requires.
    RenderTexture depthCarrying = fixture.GetRenderer().CreateRenderTexture(64, 64, VK_FORMAT_UNDEFINED,
        "UntestedTerritoryColor", "UntestedTerritoryDepth", /*allowStorageImageAccess=*/false,
        /*allowDepthSampledAccess=*/true);

    bool executeRan = false;
    VkImageView resolvedView = VK_NULL_HANDLE;
    VkSampler resolvedSampler = VK_NULL_HANDLE;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle handle = b.ImportTexture("UntestedTerritoryTarget", depthCarrying.Target(),
            VK_IMAGE_LAYOUT_UNDEFINED, depthCarrying.Sampler(), depthCarrying.DepthSampler());
        const TextureHandle output =
            b.CreateTexture("DummyOutput", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

        // Only a depth-aspect READ is declared against `handle` - no pass
        // anywhere in this graph ever calls WriteDepthStencilAttachment()
        // on it, the exact shape this test exists to prove is safe.
        b.AddRenderPass(
            "DepthResolveWithoutPriorWritePass", PassKind::Graphics,
            [&](RenderGraphBuilder::PassBuilder& pass) {
                pass.ReadTexture(handle, ResourceAccess::ShaderRead, /*isDepthResource=*/true);
                pass.WriteColorAttachment(output, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
            },
            [&](PassContext& ctx) {
                const PassContext::ResolvedDepthTexture resolved = ctx.resolveDepthTexture(handle);
                executeRan = true;
                resolvedView = resolved.view;
                resolvedSampler = resolved.sampler;
            });

        return { output };
    });

    ASSERT_TRUE(executeRan);
    EXPECT_NE(resolvedView, VK_NULL_HANDLE);
    EXPECT_NE(resolvedSampler, VK_NULL_HANDLE);
}

} // namespace
} // namespace gte::rg
