// better-render-pass-3 campaign, BLOCK2 PHASE1
// (task_manager/better-render-pass-4/PHASE1_RENDERTEXTURE_COLORLESS_EXTENSION.md)
// - Tier-2 (real, headless GPU, tests/Fakes/HeadlessRenderGraphFixture.h)
// tests for RenderTexture's new createColorImage constructor parameter -
// mirrors RenderGraphPersistentResourceCacheTests.cpp's own
// HeadlessRenderGraphFixture-based structure, GTEST_SKIP()-guarded exactly
// the same way.

#include "Renderer/RenderTexture.h"
#include "../Fakes/HeadlessRenderGraphFixture.h"
#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(RenderTextureColorlessTest, ColorOnlyByDefaultBuildsBothHalves)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderTexture tex = fixture.GetRenderer().CreateRenderTexture(64, 64);
    EXPECT_NE(tex.Image(), static_cast<VkImage>(VK_NULL_HANDLE));
    EXPECT_NE(tex.View(), static_cast<VkImageView>(VK_NULL_HANDLE));
    // DepthBuffer presence is already covered by existing tests - unchanged.
}

TEST(RenderTextureColorlessTest, CreateColorImageFalseProducesNullColorHalfAndRealDepthHalf)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }

    const GpuMemoryTracker::Totals before = fixture.GetRenderer().GetMemoryTotals();
    RenderTexture tex = fixture.GetRenderer().CreateRenderTexture(
        64, 64, VK_FORMAT_UNDEFINED, "ColorlessTest", /*depthDebugName=*/nullptr,
        /*allowStorageImageAccess=*/false, /*allowDepthSampledAccess=*/false,
        /*createDepthCompanion=*/true, /*createColorImage=*/false);

    EXPECT_EQ(tex.Image(), static_cast<VkImage>(VK_NULL_HANDLE));
    EXPECT_EQ(tex.View(), static_cast<VkImageView>(VK_NULL_HANDLE));
    EXPECT_EQ(tex.Sampler(), static_cast<VkSampler>(VK_NULL_HANDLE));

    const RenderTarget target = tex.Target();
    EXPECT_EQ(target.image, static_cast<VkImage>(VK_NULL_HANDLE));
    EXPECT_NE(target.depthImage, static_cast<VkImage>(VK_NULL_HANDLE));

    // Exactly ONE tracked allocation (the depth image), never two.
    const GpuMemoryTracker::Totals after = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(after.textureCount, before.textureCount + 1);
}

TEST(RenderTextureColorlessTest, CreateDepthCompanionFalseIsTheExactMirrorImage)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderTexture tex = fixture.GetRenderer().CreateRenderTexture(
        64, 64, VK_FORMAT_UNDEFINED, nullptr, nullptr, false, false,
        /*createDepthCompanion=*/false, /*createColorImage=*/true);
    EXPECT_NE(tex.Image(), static_cast<VkImage>(VK_NULL_HANDLE));
    const RenderTarget target = tex.Target();
    EXPECT_EQ(target.depthImage, static_cast<VkImage>(VK_NULL_HANDLE));
}

#ifndef NDEBUG
TEST(RenderTextureColorlessDeathTest, NeitherColorNorDepthAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            (void)fixture.GetRenderer().CreateRenderTexture(
                64, 64, VK_FORMAT_UNDEFINED, nullptr, nullptr, false, false,
                /*createDepthCompanion=*/false, /*createColorImage=*/false);
        },
        "");
}
#endif

} // namespace
} // namespace gte
