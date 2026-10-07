// Tier-2 (real, headless GPU, tests/Fakes/HeadlessRenderGraphFixture.h) test
// proving RenderGraph::RegisterDebugTextureSnapshots() actually copies a real
// VkSampler across into DebugTextureSnapshot::sampler - the one field
// EnsurePreviewDescriptor() (Panels/FrameDebuggerPanel.cpp) cannot build an
// ImGui descriptor without (ImGui_ImplVulkan_AddTexture() requires a real
// VkSampler, never just a VkImageView). RenderGraphDebugTextureRegistryTests.cpp
// stays pure Tier-1 (hand-fabricated snapshots, no live VkDevice) - this file
// is the one place a REAL PhysicalTexture's sampler is confirmed to survive
// the trip into a queryable DebugTextureSnapshot.

#include "Renderer/RenderGraph/RenderGraph.h"
#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte::rg {
namespace {

TextureDesc MakeColorDesc()
{
    TextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.hasDepth = false;
    return desc;
}

TEST(RenderGraphDebugTextureSnapshotSamplerTest, RegisterDebugTextureSnapshotsPopulatesANonNullSampler)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle h = b.CreateTexture("DebugTextureSnapshotSamplerTarget", desc);
        b.AddPass(
            "DebugTextureSnapshotSamplerWritePass",
            [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
            [](PassContext&) {});
        return { h }; // Must be a root - a write-only pass with no reader is culled otherwise.
    });

    const std::optional<DebugTextureSnapshot> snapshot =
        fixture.GetRenderGraph().DebugTextureSnapshotFor("DebugTextureSnapshotSamplerTarget");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NE(snapshot->sampler, static_cast<VkSampler>(VK_NULL_HANDLE));
    // No depth companion requested above - depthSampler stays null.
    EXPECT_EQ(snapshot->depthSampler, static_cast<VkSampler>(VK_NULL_HANDLE));
}

} // namespace
} // namespace gte::rg
