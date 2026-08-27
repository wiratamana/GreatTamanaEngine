// Unit tests for Phase 2's declarative "describe a frame's drawing jobs"
// builder API (src/Renderer/RenderGraph/RenderGraphBuilder.h) - entirely
// Tier-1, since nothing in RenderGraphBuilder touches a live VkDevice. See
// RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, Step 3.4, for the exact
// coverage list this file follows.
//
// `setup`/`execute` lambdas in every test below are deliberately trivial -
// `setup` declares a read/write (or nothing at all); `execute` is typically
// an empty lambda, or one that increments a captured counter specifically
// to prove it is NEVER invoked by AddPass()/Finish() themselves.

#include "Renderer/RenderGraph/RenderGraphBuilder.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

// A trivial, reusable "no-op" execute callback - most tests below only
// care that setup ran and what the resulting PassRecord looks like, not
// about execute's own behavior.
void NoOpExecute(PassContext&) { }

RenderTarget MakeFakeSwapchainTarget()
{
    RenderTarget target;
    target.image = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x1111));
    target.imageView = reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x2222));
    target.extent = VkExtent2D{ 1920, 1080 };
    target.format = VK_FORMAT_B8G8R8A8_UNORM;
    // No depth counterpart - mirrors a swapchain-only target with nothing
    // depth-tested drawn into it (e.g. Editor chrome only).
    return target;
}

RenderTarget MakeFakeGameViewTarget()
{
    RenderTarget target;
    target.image = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x3333));
    target.imageView = reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x4444));
    target.extent = VkExtent2D{ 1280, 720 };
    target.format = VK_FORMAT_R8G8B8A8_UNORM;
    target.depthImage = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x5555));
    target.depthImageView = reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x6666));
    target.depthFormat = VK_FORMAT_D32_SFLOAT;
    return target;
}

// --- CreateTexture() / CreateBuffer() - handle vs. physical identity -----

TEST(RenderGraphBuilderTest, CreateTextureMintsDistinctHandlesEvenWithIdenticalDesc)
{
    RenderGraphBuilder builder;
    const TextureDesc desc{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };

    const TextureHandle a = builder.CreateTexture("First", desc);
    const TextureHandle b = builder.CreateTexture("Second", desc);

    EXPECT_TRUE(a.IsValid());
    EXPECT_TRUE(b.IsValid());
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphBuilderTest, CreateBufferMintsDistinctHandlesEvenWithIdenticalDesc)
{
    RenderGraphBuilder builder;
    const BufferDesc desc{ 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };

    const BufferHandle a = builder.CreateBuffer("First", desc);
    const BufferHandle b = builder.CreateBuffer("Second", desc);

    EXPECT_TRUE(a.IsValid());
    EXPECT_TRUE(b.IsValid());
    EXPECT_FALSE(a == b);
}

// v2 regression test: two CreateTexture() calls with DIFFERENT names but an
// otherwise IDENTICAL desc still mint handles whose underlying TextureDesc
// values compare EQUAL - proving a resource's name has no bearing on Phase
// 4's future pool-matching logic. Direct regression coverage for the bug
// fixed in RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md (removing
// debugName from TextureDesc/BufferDesc entirely).
TEST(RenderGraphBuilderTest, DifferentNamesSameDescStillCompareEqualDescs)
{
    RenderGraphBuilder builder;
    const TextureDesc desc{ 800, 600, VK_FORMAT_R8G8B8A8_UNORM, false };

    const TextureHandle a = builder.CreateTexture("GameView", desc);
    const TextureHandle b = builder.CreateTexture("SceneView", desc);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.textureDescs.size(), 2u);
    EXPECT_TRUE(input.textureDescs[a.index] == input.textureDescs[b.index]);
    // The names themselves genuinely differ, proving they were captured
    // independently rather than one clobbering the other.
    EXPECT_STREQ(input.textureNames[a.index], "GameView");
    EXPECT_STREQ(input.textureNames[b.index], "SceneView");
}

TEST(RenderGraphBuilderTest, CreateBufferDifferentNamesSameDescStillCompareEqualDescs)
{
    RenderGraphBuilder builder;
    const BufferDesc desc{ 2048, VK_BUFFER_USAGE_INDEX_BUFFER_BIT };

    const BufferHandle a = builder.CreateBuffer("IndexBufferA", desc);
    const BufferHandle b = builder.CreateBuffer("IndexBufferB", desc);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.bufferDescs.size(), 2u);
    EXPECT_TRUE(input.bufferDescs[a.index] == input.bufferDescs[b.index]);
    EXPECT_STREQ(input.bufferNames[a.index], "IndexBufferA");
    EXPECT_STREQ(input.bufferNames[b.index], "IndexBufferB");
}

// --- AddPass() - setup vs. execute timing --------------------------------

TEST(RenderGraphBuilderTest, AddPassSetupRunsSynchronouslyExactlyOnce)
{
    RenderGraphBuilder builder;
    int setupCallCount = 0;

    builder.AddPass(
        "TestPass",
        [&](RenderGraphBuilder::PassBuilder&) { ++setupCallCount; },
        NoOpExecute);

    EXPECT_EQ(setupCallCount, 1);
}

TEST(RenderGraphBuilderTest, AddPassExecuteIsNeverInvokedByAddPassOrFinish)
{
    RenderGraphBuilder builder;
    int executeCallCount = 0;

    builder.AddPass(
        "TestPass",
        [](RenderGraphBuilder::PassBuilder&) { },
        [&](PassContext&) { ++executeCallCount; });

    EXPECT_EQ(executeCallCount, 0);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_EQ(executeCallCount, 0);
    ASSERT_EQ(input.passes.size(), 1u);
    // The execute callback IS captured (a real, callable std::function),
    // it is just never invoked by anything in this file.
    EXPECT_TRUE(static_cast<bool>(input.passes[0].execute));
}

TEST(RenderGraphBuilderTest, AddPassRecordsPassNameAndDefaultsToNotCulled)
{
    RenderGraphBuilder builder;
    builder.AddPass(
        "MyPass",
        [](RenderGraphBuilder::PassBuilder&) { },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "MyPass");
    EXPECT_FALSE(input.passes[0].isCulled);
}

// --- PassBuilder - reads/writes append with the right ResourceAccess ----

TEST(RenderGraphBuilderTest, PassBuilderReadTextureAppendsWithGivenAccess)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Input", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "ReadPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(handle, ResourceAccess::ShaderRead); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].reads.size(), 1u);
    EXPECT_EQ(input.passes[0].reads[0].kind, ResourceKind::Texture);
    EXPECT_EQ(input.passes[0].reads[0].texture, handle);
    EXPECT_EQ(input.passes[0].reads[0].access, ResourceAccess::ShaderRead);
    EXPECT_TRUE(input.passes[0].writes.empty());
}

TEST(RenderGraphBuilderTest, PassBuilderReadTextureDefaultsToShaderRead)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Input", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "ReadPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(handle); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].reads.size(), 1u);
    EXPECT_EQ(input.passes[0].reads[0].access, ResourceAccess::ShaderRead);
}

TEST(RenderGraphBuilderTest, PassBuilderWriteColorAttachmentAppendsColorAttachmentWrite)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "WritePass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(handle); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].kind, ResourceKind::Texture);
    EXPECT_EQ(input.passes[0].writes[0].texture, handle);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::ColorAttachmentWrite);
}

TEST(RenderGraphBuilderTest, PassBuilderWriteDepthStencilAttachmentAppendsDepthStencilReadWrite)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Depth", TextureDesc{ 64, 64, VK_FORMAT_D32_SFLOAT, true });

    builder.AddPass(
        "DepthPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteDepthStencilAttachment(handle); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].kind, ResourceKind::Texture);
    EXPECT_EQ(input.passes[0].writes[0].texture, handle);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::DepthStencilAttachmentReadWrite);
}

// Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) - the
// default (no clear value supplied) leaves PassRecord::colorClearValue at
// std::nullopt, matching Phase 6's original, only behavior
// (VK_ATTACHMENT_LOAD_OP_LOAD).
TEST(RenderGraphBuilderTest, WriteColorAttachmentWithNoClearColorLeavesColorClearValueEmpty)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "WritePass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(handle); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_FALSE(input.passes[0].colorClearValue.has_value());
}

// Supplying a clear color records it verbatim onto the pass's own
// PassRecord::colorClearValue - what RenderGraph::Execute() reads to decide
// VK_ATTACHMENT_LOAD_OP_CLEAR vs. LOAD (see RenderGraph.cpp).
TEST(RenderGraphBuilderTest, WriteColorAttachmentWithClearColorRecordsItOnThePass)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });
    const std::array<float, 4> clearColor{ 0.1f, 0.2f, 0.3f, 1.0f };

    builder.AddPass(
        "WritePass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(handle, clearColor); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_TRUE(input.passes[0].colorClearValue.has_value());
    EXPECT_EQ(*input.passes[0].colorClearValue, clearColor);
}

// Same idea, depth side: no clear value supplied leaves depthClearValue at
// std::nullopt.
TEST(RenderGraphBuilderTest, WriteDepthStencilAttachmentWithNoClearDepthLeavesDepthClearValueEmpty)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Depth", TextureDesc{ 64, 64, VK_FORMAT_D32_SFLOAT, true });

    builder.AddPass(
        "DepthPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteDepthStencilAttachment(handle); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_FALSE(input.passes[0].depthClearValue.has_value());
}

// Supplying a clear depth records it verbatim onto the pass's own
// PassRecord::depthClearValue.
TEST(RenderGraphBuilderTest, WriteDepthStencilAttachmentWithClearDepthRecordsItOnThePass)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Depth", TextureDesc{ 64, 64, VK_FORMAT_D32_SFLOAT, true });

    builder.AddPass(
        "DepthPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteDepthStencilAttachment(handle, 1.0f); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_TRUE(input.passes[0].depthClearValue.has_value());
    EXPECT_FLOAT_EQ(*input.passes[0].depthClearValue, 1.0f);
}

// --- WriteTexture() (Phase 6 of the compute-shader campaign) ---------------

TEST(RenderGraphBuilderTest, PassBuilderWriteTextureDefaultsToComputeShaderWrite)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "ComputeWritePass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(handle); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].kind, ResourceKind::Texture);
    EXPECT_EQ(input.passes[0].writes[0].texture, handle);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::ComputeShaderWrite);
}

TEST(RenderGraphBuilderTest, PassBuilderWriteTextureAppendsWithGivenAccess)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "ComputeWritePass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(handle, ResourceAccess::TransferDst); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::TransferDst);
}

// A read-modify-write RWTexture declares BOTH a ReadTexture(ComputeShaderRead)
// AND a WriteTexture(ComputeShaderWrite) usage on the SAME handle - mirroring
// ReadBuffer()/WriteBuffer()'s own existing two-calls-combined convention.
TEST(RenderGraphBuilderTest, PassBuilderCanDeclareBothReadAndWriteOfSameTextureForReadModifyWrite)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Scratch", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    builder.AddPass(
        "ReadModifyWritePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(handle, ResourceAccess::ComputeShaderRead);
            pass.WriteTexture(handle, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].reads.size(), 1u);
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].reads[0].texture, handle);
    EXPECT_EQ(input.passes[0].reads[0].access, ResourceAccess::ComputeShaderRead);
    EXPECT_EQ(input.passes[0].writes[0].texture, handle);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::ComputeShaderWrite);
}

// --- AddComputePass() - a thin, purely cosmetic alias of AddPass() ---------

TEST(RenderGraphBuilderTest, AddComputePassBehavesIdenticallyToAddPass)
{
    RenderGraphBuilder builder;
    const TextureHandle handle = builder.CreateTexture("Output", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });
    int setupCallCount = 0;

    builder.AddComputePass(
        "ComputePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            ++setupCallCount;
            pass.WriteTexture(handle);
        },
        NoOpExecute);

    EXPECT_EQ(setupCallCount, 1);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "ComputePass");
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::ComputeShaderWrite);
}


TEST(RenderGraphBuilderTest, PassBuilderReadBufferAppendsWithGivenAccess)
{
    RenderGraphBuilder builder;
    const BufferHandle handle = builder.CreateBuffer("VertexData", BufferDesc{ 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT });

    builder.AddPass(
        "ReadBufferPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadBuffer(handle, ResourceAccess::TransferSrc); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].reads.size(), 1u);
    EXPECT_EQ(input.passes[0].reads[0].kind, ResourceKind::Buffer);
    EXPECT_EQ(input.passes[0].reads[0].buffer, handle);
    EXPECT_EQ(input.passes[0].reads[0].access, ResourceAccess::TransferSrc);
}

TEST(RenderGraphBuilderTest, PassBuilderWriteBufferAppendsWithGivenAccess)
{
    RenderGraphBuilder builder;
    const BufferHandle handle = builder.CreateBuffer("StagingData", BufferDesc{ 1024, VK_BUFFER_USAGE_TRANSFER_DST_BIT });

    builder.AddPass(
        "WriteBufferPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(handle, ResourceAccess::TransferDst); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].kind, ResourceKind::Buffer);
    EXPECT_EQ(input.passes[0].writes[0].buffer, handle);
    EXPECT_EQ(input.passes[0].writes[0].access, ResourceAccess::TransferDst);
}

// --- ImportTexture() -------------------------------------------------------

TEST(RenderGraphBuilderTest, ImportTextureProducesHandleUsableLikeCreateTextureHandle)
{
    RenderGraphBuilder builder;
    const RenderTarget swapchainTarget = MakeFakeSwapchainTarget();
    const TextureHandle imported = builder.ImportTexture("Swapchain", swapchainTarget, VK_IMAGE_LAYOUT_UNDEFINED);

    ASSERT_TRUE(imported.IsValid());

    // Usable in ReadTexture()/WriteColorAttachment() exactly like a
    // CreateTexture()-minted handle - the pass author cannot tell the
    // difference from the handle alone.
    builder.AddPass(
        "PresentPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(imported); },
        NoOpExecute);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].texture, imported);
}

TEST(RenderGraphBuilderTest, ImportTextureIsTaggedAsImportedInCompiledGraphInput)
{
    RenderGraphBuilder builder;
    const RenderTarget swapchainTarget = MakeFakeSwapchainTarget();
    const TextureHandle imported = builder.ImportTexture("Swapchain", swapchainTarget, VK_IMAGE_LAYOUT_UNDEFINED);
    const TextureHandle transient = builder.CreateTexture("Scratch", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.textureImportInfo.size(), 2u);
    EXPECT_TRUE(input.textureImportInfo[imported.index].isImported);
    EXPECT_FALSE(input.textureImportInfo[transient.index].isImported);
}

// v2 regression coverage: this parameter did not exist at all in v1's own
// code sketch - now verify it is exactly what's recorded and later
// retrievable, never silently dropped or defaulted.
TEST(RenderGraphBuilderTest, ImportTextureRecordsExactCurrentLayoutSupplied)
{
    RenderGraphBuilder builder;
    const RenderTarget gameViewTarget = MakeFakeGameViewTarget();
    const TextureHandle imported =
        builder.ImportTexture("GameView", gameViewTarget, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.textureImportInfo.size(), 1u);
    EXPECT_TRUE(input.textureImportInfo[imported.index].isImported);
    EXPECT_EQ(input.textureImportInfo[imported.index].currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TEST(RenderGraphBuilderTest, ImportTextureStoresExternalTargetVerbatim)
{
    RenderGraphBuilder builder;
    const RenderTarget gameViewTarget = MakeFakeGameViewTarget();
    const TextureHandle imported = builder.ImportTexture("GameView", gameViewTarget, VK_IMAGE_LAYOUT_UNDEFINED);

    const CompiledGraphInput input = builder.Finish();
    const TextureImportInfo& info = input.textureImportInfo[imported.index];
    EXPECT_EQ(info.externalTarget.image, gameViewTarget.image);
    EXPECT_EQ(info.externalTarget.imageView, gameViewTarget.imageView);
    EXPECT_EQ(info.externalTarget.extent.width, gameViewTarget.extent.width);
    EXPECT_EQ(info.externalTarget.extent.height, gameViewTarget.extent.height);
    EXPECT_EQ(info.externalTarget.format, gameViewTarget.format);
    EXPECT_EQ(info.externalTarget.depthImage, gameViewTarget.depthImage);
}

TEST(RenderGraphBuilderTest, ImportTextureMirrorsExternalTargetShapeIntoTextureDesc)
{
    RenderGraphBuilder builder;
    const RenderTarget gameViewTarget = MakeFakeGameViewTarget();
    const TextureHandle imported = builder.ImportTexture("GameView", gameViewTarget, VK_IMAGE_LAYOUT_UNDEFINED);

    const CompiledGraphInput input = builder.Finish();
    const TextureDesc& desc = input.textureDescs[imported.index];
    EXPECT_EQ(desc.width, gameViewTarget.extent.width);
    EXPECT_EQ(desc.height, gameViewTarget.extent.height);
    EXPECT_EQ(desc.format, gameViewTarget.format);
    EXPECT_TRUE(desc.hasDepth); // MakeFakeGameViewTarget() carries a depth image.
}

TEST(RenderGraphBuilderTest, ImportedAndTransientTexturesShareOneContiguousHandleSpace)
{
    RenderGraphBuilder builder;
    const TextureHandle transientA = builder.CreateTexture("A", TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false });
    const TextureHandle imported = builder.ImportTexture("Swapchain", MakeFakeSwapchainTarget(), VK_IMAGE_LAYOUT_UNDEFINED);
    const TextureHandle transientB = builder.CreateTexture("B", TextureDesc{ 32, 32, VK_FORMAT_R8G8B8A8_UNORM, false });

    EXPECT_FALSE(transientA == imported);
    EXPECT_FALSE(imported == transientB);
    EXPECT_FALSE(transientA == transientB);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_EQ(input.textureDescs.size(), 3u);
    EXPECT_EQ(input.textureNames.size(), 3u);
    EXPECT_EQ(input.textureImportInfo.size(), 3u);
}

// --- Name validation guard -------------------------------------------------
//
// A pass/resource name that is nullptr or empty is rejected via an
// assertion (debug builds only - see RenderGraphBuilder.h's own comment,
// mirroring GTE_PROFILE_SCOPE's own static-storage-duration discipline).
// Guarded by NDEBUG since a release build compiles assert() down to a
// no-op entirely - these death tests would otherwise not actually die.
#ifndef NDEBUG

TEST(RenderGraphBuilderDeathTest, CreateTextureRejectsNullName)
{
    RenderGraphBuilder builder;
    EXPECT_DEATH(
        { builder.CreateTexture(nullptr, TextureDesc{}); },
        "");
}

TEST(RenderGraphBuilderDeathTest, CreateTextureRejectsEmptyName)
{
    RenderGraphBuilder builder;
    EXPECT_DEATH(
        { builder.CreateTexture("", TextureDesc{}); },
        "");
}

TEST(RenderGraphBuilderDeathTest, CreateBufferRejectsNullName)
{
    RenderGraphBuilder builder;
    EXPECT_DEATH(
        { builder.CreateBuffer(nullptr, BufferDesc{}); },
        "");
}

TEST(RenderGraphBuilderDeathTest, ImportTextureRejectsNullName)
{
    RenderGraphBuilder builder;
    const RenderTarget target = MakeFakeSwapchainTarget();
    EXPECT_DEATH(
        { builder.ImportTexture(nullptr, target, VK_IMAGE_LAYOUT_UNDEFINED); },
        "");
}

TEST(RenderGraphBuilderDeathTest, AddPassRejectsNullName)
{
    RenderGraphBuilder builder;
    EXPECT_DEATH(
        {
            builder.AddPass(
                nullptr,
                [](RenderGraphBuilder::PassBuilder&) { },
                NoOpExecute);
        },
        "");
}

TEST(RenderGraphBuilderDeathTest, AddPassRejectsEmptyName)
{
    RenderGraphBuilder builder;
    EXPECT_DEATH(
        {
            builder.AddPass(
                "",
                [](RenderGraphBuilder::PassBuilder&) { },
                NoOpExecute);
        },
        "");
}

#endif // !NDEBUG

} // namespace
} // namespace gte::rg
