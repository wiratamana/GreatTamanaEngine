// Unit tests for RenderGraphCompiler: dependency resolution, culling, ordering and lifetimes.
// Fixtures are built through RenderGraphBuilder; no Vulkan device is involved.

#include "Renderer/RenderGraph/RenderGraphCompiler.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

TextureDesc MakeTextureDesc()
{
    return TextureDesc{ 64, 64, PixelFormat::RGBA8_UNorm, false };
}

bool ExecutionOrderEquals(const std::vector<PassHandle>& order, const std::vector<std::uint32_t>& expectedIndices)
{
    if (order.size() != expectedIndices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i].index != expectedIndices[i]) {
            return false;
        }
    }
    return true;
}

// --- Empty graph -----------------------------------------------------------

TEST(RenderGraphCompilerTest, EmptyGraphCompilesToEmptyResult)
{
    RenderGraphBuilder builder;
    CompiledGraphInput input = builder.Finish();

    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(compiled.textureLifetimes.empty());
    EXPECT_TRUE(compiled.bufferLifetimes.empty());
}

// --- Linear chain ------------------------------------------------------------

TEST(RenderGraphCompilerTest, LinearChainOrdersWriterBeforeReaderAndCullsNothing)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    TextureHandle t1;

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    builder.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            t1 = builder.CreateTexture("T1", MakeTextureDesc());
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t1 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// --- Dead branch culled ------------------------------------------------------

TEST(RenderGraphCompilerTest, DeadBranchIsCulledAndItsResourceHasNoLifetime)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle t2 = builder.CreateTexture("T2", MakeTextureDesc());

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    builder.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute);
    // C writes T2, but nothing ever reads T2 - dead code.
    builder.AddPass(
        "C", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t2); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t1 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_TRUE(input.passes[2].isCulled);

    ASSERT_EQ(compiled.textureLifetimes.size(), 3u);
    EXPECT_EQ(compiled.textureLifetimes[t2.index].firstUsePassIndex, -1);
    EXPECT_EQ(compiled.textureLifetimes[t2.index].lastUsePassIndex, -1);
}

// --- Diamond dependency -------------------------------------------------------

TEST(RenderGraphCompilerTest, DiamondDependencyOrdersCorrectlyWithDeterministicSiblingOrder)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle t2 = builder.CreateTexture("T2", MakeTextureDesc());
    const TextureHandle tFinal = builder.CreateTexture("TFinal", MakeTextureDesc());

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute); // index 0
    builder.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute); // index 1
    builder.AddPass(
        "C",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t2);
        },
        NoOpExecute); // index 2
    builder.AddPass(
        "D",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t1);
            pass.ReadTexture(t2);
            pass.WriteColorAttachment(tFinal);
        },
        NoOpExecute); // index 3

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { tFinal };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    // A precedes B and C, both precede D, and B/C keep declaration order.
    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2, 3 }));
    for (const auto& pass : input.passes) {
        EXPECT_FALSE(pass.isCulled);
    }
}

// --- Multiple writers to the same (imported) resource -------------------------

TEST(RenderGraphCompilerTest, MultipleWritersToSameResourcePreserveWriteAfterWriteOrder)
{
    RenderGraphBuilder builder;
    const RenderTarget swapchainTarget{};
    const TextureHandle swapchain = builder.ImportTexture("Swapchain", swapchainTarget, ImageLayout::Undefined);

    builder.AddPass(
        "ClearPass", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(swapchain); }, NoOpExecute);
    builder.AddPass(
        "OverlayPass", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(swapchain); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { swapchain };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
}

// --- Lifetime bounds -----------------------------------------------------------

TEST(RenderGraphCompilerTest, LifetimeBoundsSpanFromFirstWriterThroughLaterOfTwoReaders)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle t2 = builder.CreateTexture("T2", MakeTextureDesc());
    const TextureHandle tFinal = builder.CreateTexture("TFinal", MakeTextureDesc());

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    builder.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute);
    builder.AddPass(
        "C",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t2);
        },
        NoOpExecute);
    builder.AddPass(
        "D",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t1);
            pass.ReadTexture(t2);
            pass.WriteColorAttachment(tFinal);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { tFinal };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    // T0 is written at position 0 and read at 1 and 2, so its lifetime is [0, 2] (the later reader).
    ASSERT_EQ(compiled.textureLifetimes.size(), 4u);
    const ResourceLifetime& t0Lifetime = compiled.textureLifetimes[t0.index];
    EXPECT_EQ(t0Lifetime.firstUsePassIndex, 0);
    EXPECT_EQ(t0Lifetime.lastUsePassIndex, 2);

    // TFinal is only touched by the last pass, so its last use is that pass's position (3), not one past it.
    const ResourceLifetime& finalLifetime = compiled.textureLifetimes[tFinal.index];
    EXPECT_EQ(finalLifetime.firstUsePassIndex, 3);
    EXPECT_EQ(finalLifetime.lastUsePassIndex, 3);
}

// --- Array lifetime ----------------------------------------------------------
// An array written by one pass and read by a later pass (which writes the final output) spans both passes.
TEST(RenderGraphCompilerTest, TextureArrayLifetimeTracksFirstAndLastUsePassIndex)
{
    RenderGraphBuilder builder;
    TextureDesc desc;
    desc.width = 1024;
    desc.height = 1024;
    desc.hasDepth = true;
    desc.dimension = TextureDimension::Array2D;
    desc.arrayLayers = 4;
    const TextureHandle cascades = builder.CreateTexture("Cascades", desc);
    const TextureHandle output = builder.CreateTexture("CascadesPreview", MakeTextureDesc());

    builder.AddPass(
        "WriteCascades",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(cascades, ResourceAccess::DepthStencilAttachmentReadWrite);
        },
        NoOpExecute); // index 0
    builder.AddPass(
        "ReadCascades",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(cascades, ResourceAccess::ShaderRead);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute); // index 1 - its own finalOutputs-reaching write keeps both passes alive.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { output };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);

    ASSERT_EQ(compiled.textureLifetimes.size(), 2u);
    EXPECT_EQ(compiled.textureLifetimes[cascades.index].firstUsePassIndex, 0);
    EXPECT_EQ(compiled.textureLifetimes[cascades.index].lastUsePassIndex, 1);
}

// --- Determinism ---------------------------------------------------------------

TEST(RenderGraphCompilerTest, CompilingTheSameGraphTwiceProducesByteIdenticalExecutionOrder)
{
    RenderGraphBuilder builderA;
    const TextureHandle aT0 = builderA.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle aT1 = builderA.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle aT2 = builderA.CreateTexture("T2", MakeTextureDesc());
    const TextureHandle aFinal = builderA.CreateTexture("TFinal", MakeTextureDesc());
    builderA.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(aT0); }, NoOpExecute);
    builderA.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(aT0);
            pass.WriteColorAttachment(aT1);
        },
        NoOpExecute);
    builderA.AddPass(
        "C",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(aT0);
            pass.WriteColorAttachment(aT2);
        },
        NoOpExecute);
    builderA.AddPass(
        "D",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(aT1);
            pass.ReadTexture(aT2);
            pass.WriteColorAttachment(aFinal);
        },
        NoOpExecute);
    CompiledGraphInput inputA = builderA.Finish();
    const TextureHandle finalOutputsA[] = { aFinal };

    // An independently built identical copy: Compile() is deterministic, not just idempotent.
    RenderGraphBuilder builderB;
    const TextureHandle bT0 = builderB.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle bT1 = builderB.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle bT2 = builderB.CreateTexture("T2", MakeTextureDesc());
    const TextureHandle bFinal = builderB.CreateTexture("TFinal", MakeTextureDesc());
    builderB.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(bT0); }, NoOpExecute);
    builderB.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(bT0);
            pass.WriteColorAttachment(bT1);
        },
        NoOpExecute);
    builderB.AddPass(
        "C",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(bT0);
            pass.WriteColorAttachment(bT2);
        },
        NoOpExecute);
    builderB.AddPass(
        "D",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(bT1);
            pass.ReadTexture(bT2);
            pass.WriteColorAttachment(bFinal);
        },
        NoOpExecute);
    CompiledGraphInput inputB = builderB.Finish();
    const TextureHandle finalOutputsB[] = { bFinal };

    const CompiledGraph compiledA = Compile(inputA, finalOutputsA);
    const CompiledGraph compiledB = Compile(inputB, finalOutputsB);

    ASSERT_EQ(compiledA.executionOrder.size(), compiledB.executionOrder.size());
    for (std::size_t i = 0; i < compiledA.executionOrder.size(); ++i) {
        EXPECT_EQ(compiledA.executionOrder[i].index, compiledB.executionOrder[i].index);
        EXPECT_EQ(compiledA.executionOrder[i].generation, compiledB.executionOrder[i].generation);
    }
}

TEST(RenderGraphCompilerTest, CompilingTheSameInputObjectTwiceIsIdempotent)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t0 };

    const CompiledGraph first = Compile(input, finalOutputs);
    const CompiledGraph second = Compile(input, finalOutputs);

    ASSERT_EQ(first.executionOrder.size(), second.executionOrder.size());
    EXPECT_EQ(first.executionOrder[0].index, second.executionOrder[0].index);
    EXPECT_FALSE(input.passes[0].isCulled);
}

// --- Edge cases --------------------------------------------------------------

TEST(RenderGraphCompilerTest, SelfLoopReadAndWriteOfSameResourceDoesNotCrashOrCycle)
{
    RenderGraphBuilder builder;
    const TextureHandle depth = builder.CreateTexture("Depth", TextureDesc{ 64, 64, PixelFormat::D32_Float, true });

    builder.AddPass(
        "DepthPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(depth, ResourceAccess::DepthStencilAttachmentReadWrite);
            pass.WriteDepthStencilAttachment(depth);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { depth };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    ASSERT_EQ(compiled.textureLifetimes.size(), 1u);
    EXPECT_EQ(compiled.textureLifetimes[depth.index].firstUsePassIndex, 0);
    EXPECT_EQ(compiled.textureLifetimes[depth.index].lastUsePassIndex, 0);
}

TEST(RenderGraphCompilerTest, DisconnectedPassWithNoReadsOrWritesIsCulled)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    // Declares nothing: never a root and never reached from one.
    builder.AddPass("Disconnected", [](RenderGraphBuilder::PassBuilder&) { }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t0 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_TRUE(input.passes[1].isCulled);
}

TEST(RenderGraphCompilerTest, FinalOutputHandleNeverProducedByAnythingDoesNotCrash)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle neverWritten = builder.CreateTexture("NeverWritten", MakeTextureDesc());

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { neverWritten };

    const CompiledGraph compiled = Compile(input, finalOutputs);

    // Nothing writes `neverWritten`, so there is no root and the pass is culled.
    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

// --- Buffer reachability -----------------------------------------------------
// A buffer-only write survives only if a kept pass reads it and reaches a finalOutputs texture.
TEST(RenderGraphCompilerTest, BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput)
{
    RenderGraphBuilder builder;
    const BufferHandle bufferB = builder.CreateBuffer("B", BufferDesc{ 1024, BufferUsage::StorageBuffer });
    const TextureHandle textureT = builder.CreateTexture("T", MakeTextureDesc());

    builder.AddPass(
        "PassA",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(bufferB, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute); // index 0 - buffer-only write, no texture output of its own.
    builder.AddPass(
        "PassB",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadBuffer(bufferB, ResourceAccess::ComputeShaderRead);
            pass.WriteColorAttachment(textureT);
        },
        NoOpExecute); // index 1 - reads B, and its own texture write IS a finalOutputs root.
    builder.AddPass(
        "PassC",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(bufferB, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute); // index 2 - ALSO writes B, but nothing ever reads it - dead code.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { textureT };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    // PassA survives through PassB's texture output; PassC is culled.
    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_TRUE(input.passes[2].isCulled);
}

// --- Volume roots ------------------------------------------------------------
// A volume-only write survives only when kept via KeepTextureOutput(); VolumeA is kept, VolumeB is culled.
TEST(RenderGraphCompilerTest, VolumeTextureOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput)
{
    RenderGraphBuilder builder;
    const VolumeTarget dummyVolumeTarget{};
    const TextureHandle volumeA = builder.ImportVolumeTexture("VolA", dummyVolumeTarget, ImageLayout::Undefined);
    const TextureHandle volumeB = builder.ImportVolumeTexture("VolB", dummyVolumeTarget, ImageLayout::Undefined);

    builder.AddComputePass(
        "WritesKeptVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(volumeA, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 0 - its only write is explicitly kept as a root below.
    builder.AddComputePass(
        "WritesUnkeptVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(volumeB, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 1 - writes a DIFFERENT volume, never kept/read - dead code.

    builder.KeepTextureOutput(volumeA);

    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_TRUE(input.passes[1].isCulled);
}

TEST(RenderGraphCompilerTest, VolumeTextureWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll)
{
    RenderGraphBuilder builder;
    const VolumeTarget dummyVolumeTarget{};
    const TextureHandle volume = builder.ImportVolumeTexture("Vol", dummyVolumeTarget, ImageLayout::Undefined);

    builder.AddComputePass(
        "WritesVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(volume, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);

    // Never kept: a write nobody marks as a root is dead code, same as any other never-read resource.
    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

// A kept 3D volume, a kept array and a kept plain 2D texture share ONE root list.
TEST(RenderGraphCompilerTest, KeepTextureOutputRootsEveryTextureDimension)
{
    RenderGraphBuilder builder;
    const TextureHandle volume = builder.ImportVolumeTexture("Vol", VolumeTarget{}, ImageLayout::Undefined);
    TextureDesc arrayDesc;
    arrayDesc.dimension = TextureDimension::Array2D;
    arrayDesc.arrayLayers = 2;
    const TextureHandle array = builder.CreateTexture("Array", arrayDesc);
    const TextureHandle plain = builder.CreateTexture("Plain", MakeTextureDesc());

    builder.AddComputePass("WritesVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(volume, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute);
    builder.AddComputePass("WritesArray",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(array, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute);
    builder.AddPass("WritesPlain",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(plain); }, NoOpExecute);

    builder.KeepTextureOutput(volume);
    builder.KeepTextureOutput(array);
    builder.KeepTextureOutput(plain);

    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_FALSE(input.passes[2].isCulled);
}

// --- Kept buffer roots -------------------------------------------------------
// KeepBufferOutput() is an opt-in root: bufferA (kept) survives, bufferB (never kept or read) is culled.
TEST(RenderGraphCompilerTest, BufferOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput)
{
    RenderGraphBuilder builder;
    const BufferHandle bufferA =
        builder.ImportBuffer("BufA", BufferObjectHandle::Invalid, 1024);
    const BufferHandle bufferB =
        builder.ImportBuffer("BufB", BufferObjectHandle::Invalid, 1024);

    builder.AddComputePass(
        "WritesKeptBuffer",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteBuffer(bufferA, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 0 - its only write is explicitly kept as a root below.
    builder.AddComputePass(
        "WritesUnkeptBuffer",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteBuffer(bufferB, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 1 - writes a DIFFERENT buffer, never kept/read - dead code.

    builder.KeepBufferOutput(bufferA);

    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_TRUE(input.passes[1].isCulled);
}

TEST(RenderGraphCompilerTest, BufferWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll)
{
    RenderGraphBuilder builder;
    const BufferHandle buffer =
        builder.ImportBuffer("Buf", BufferObjectHandle::Invalid, 1024);

    builder.AddComputePass(
        "WritesBuffer",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteBuffer(buffer, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);

    // Never kept: a buffer write nobody marks as a root is dead code.
    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

// --- Two compute writers sharing one buffer ----------------------------------
// The second writer declares a read first; a draw then reads the buffer. Order is kept and nothing is culled.
TEST(RenderGraphCompilerTest, GpuSkinningReadBeforeWriteMitigationPreservesOrderForSharedOutputBuffer)
{
    RenderGraphBuilder builder;
    const BufferHandle output = builder.ImportBuffer("SkinOutput", BufferObjectHandle::Invalid, 1024);
    const TextureHandle color = builder.CreateTexture("Color", MakeTextureDesc());

    builder.AddComputePass(
        "SkinModel:InstanceA",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(output, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute); // index 0 - first animator's own skinning dispatch.
    builder.AddComputePass(
        "SkinModel:InstanceB",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            // Read declared before the write forces a dependency on the previous writer of this buffer.
            pass.ReadBuffer(output, ResourceAccess::ComputeShaderRead);
            pass.WriteBuffer(output, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 1 - second animator sharing the same Mesh.
    builder.AddPass(
        "DrawModel",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadBuffer(output, ResourceAccess::VertexBufferRead);
            pass.WriteColorAttachment(color);
        },
        NoOpExecute); // index 2 - the graphics pass drawing from the result.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { color };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_FALSE(input.passes[2].isCulled);
}

// --- Persistent-cache roots --------------------------------------------------
// Entries are pushed onto persistentCacheTextures directly; Compile() only reads that list.

TEST(RenderGraphCompilerTest, PersistentCacheTextureWriteSurvivesCullingWithNoFinalOutputsAtAll)
{
    RenderGraphBuilder builder;
    const TextureHandle history = builder.CreateTexture("History", MakeTextureDesc());

    builder.AddComputePass(
        "WritesHistory",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(history); },
        NoOpExecute); // index 0 - its only write is a persistent-cache texture, no in-frame reader.

    CompiledGraphInput input = builder.Finish();
    input.persistentCacheTextures.push_back(PersistentTextureUse{ history, PersistentSlotRef{} });

    // Empty finalOutputs: the persistent-cache entry alone keeps the pass alive.
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
}

TEST(RenderGraphCompilerTest, TextureWriteNotInPersistentCacheOrFinalOutputsIsStillCulled)
{
    RenderGraphBuilder builder;
    const TextureHandle history = builder.CreateTexture("History", MakeTextureDesc());

    builder.AddComputePass(
        "WritesHistory",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(history); },
        NoOpExecute);

    // No persistent-cache entry and no finalOutputs: a texture write is not a root by itself.
    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

TEST(RenderGraphCompilerTest, OrdinaryFinalOutputsRootStillWorksWithEmptyPersistentCacheTextures)
{
    RenderGraphBuilder builder;
    const TextureHandle color = builder.CreateTexture("Color", MakeTextureDesc());

    builder.AddPass(
        "Opaque", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(color); }, NoOpExecute);

    // persistentCacheTextures stays empty: the plain finalOutputs path is unaffected.
    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { color };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
}

TEST(RenderGraphCompilerTest, PersistentCacheRootDoesNotCrossContaminateAnUnrelatedPass)
{
    RenderGraphBuilder builder;
    const TextureHandle history = builder.CreateTexture("History", MakeTextureDesc());
    const TextureHandle otherColor = builder.CreateTexture("OtherColor", MakeTextureDesc());
    const TextureHandle unrelatedDeadTexture = builder.CreateTexture("UnrelatedDead", MakeTextureDesc());

    builder.AddComputePass(
        "WritesHistory",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(history); },
        NoOpExecute); // index 0 - kept via persistentCacheTextures below.
    builder.AddPass(
        "WritesOtherColor",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(otherColor); },
        NoOpExecute); // index 1 - kept via finalOutputs below, unrelated to index 0.
    builder.AddPass(
        "WritesUnrelatedDead",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(unrelatedDeadTexture); },
        NoOpExecute); // index 2 - never kept/read by anything, must stay culled.

    CompiledGraphInput input = builder.Finish();
    input.persistentCacheTextures.push_back(PersistentTextureUse{ history, PersistentSlotRef{} });
    const TextureHandle finalOutputs[] = { otherColor };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_TRUE(input.passes[2].isCulled);
}

// A consistent graph must never abort Compile(), and keeps the same order as the diamond test.
TEST(RenderGraphCompilerTest, CallingCompileWithAConsistentGraphNeverAborts)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle t2 = builder.CreateTexture("T2", MakeTextureDesc());
    const TextureHandle tFinal = builder.CreateTexture("TFinal", MakeTextureDesc());

    builder.AddRenderPass(
        "A", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 0
    builder.AddRenderPass(
        "B", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1
    builder.AddRenderPass(
        "C", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t2);
        },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 2
    builder.AddRenderPass(
        "D", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t1);
            pass.ReadTexture(t2);
            pass.WriteColorAttachment(tFinal);
        },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterOpaques); // index 3

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { tFinal };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2, 3 }));
    for (const auto& pass : input.passes) {
        EXPECT_FALSE(pass.isCulled);
    }
}

// --- RenderPassEvent ordering ------------------------------------------------
// Compile() walks passes in event order (ties by declaration index), so a late-tagged reader still follows its writer.
TEST(RenderGraphCompilerTest, ReaderTaggedEarlierThanItsTextuallyLaterWriterStillResolvesCorrectly)
{
    RenderGraphBuilder builder;
    const TextureHandle shared = builder.CreateTexture("Shared", MakeTextureDesc());
    const TextureHandle compositeOutput = builder.CreateTexture("CompositeOutput", MakeTextureDesc());

    builder.AddRenderPass(
        "Composite", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(shared);
            pass.WriteColorAttachment(compositeOutput);
        },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterTransparents); // index 0 - reader, declared FIRST.
    builder.AddRenderPass(
        "Opaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(shared); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1 - writer, declared SECOND.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { compositeOutput };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    // Event order walks "Opaque" before "Composite", so the RAW edge forms and neither pass is culled.
    ASSERT_EQ(compiled.executionOrder.size(), 2u);
    EXPECT_EQ(compiled.executionOrder[0].index, 1u); // "Opaque" (writer) first.
    EXPECT_EQ(compiled.executionOrder[1].index, 0u); // "Composite" (reader) second.
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// Two independent passes: event tier alone decides their order, overriding declaration order.
TEST(RenderGraphCompilerTest, TwoIndependentPassesWithNoSharedResourceExecuteInRenderPassEventOrderRegardlessOfDeclarationOrder)
{
    RenderGraphBuilder builder;
    const TextureHandle a = builder.CreateTexture("A", MakeTextureDesc());
    const TextureHandle b = builder.CreateTexture("B", MakeTextureDesc());

    builder.AddRenderPass(
        "First", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(a); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterOpaques); // index 0 - declared FIRST, tagged LATER.
    builder.AddRenderPass(
        "Second", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(b); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::PreOpaques); // index 1 - declared SECOND, tagged EARLIER.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { a, b };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    ASSERT_EQ(compiled.executionOrder.size(), 2u);
    EXPECT_EQ(compiled.executionOrder[0].index, 1u); // "Second" (PreOpaques) first, despite being declared second.
    EXPECT_EQ(compiled.executionOrder[1].index, 0u); // "First" (AfterOpaques) second, despite being declared first.
}

// Write-after-write edges follow event order, not declaration order.
TEST(RenderGraphCompilerTest, WriteAfterWriteAcrossDifferentRenderPassEventTiersResolvesByEffectiveOrderNotDeclarationOrder)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddRenderPass(
        "Late", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterTransparents); // index 0 - declared FIRST.
    builder.AddRenderPass(
        "Early", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1 - declared SECOND, writes the SAME texture.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t0 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    // Event order puts "Early" (Opaques) before "Late" (AfterTransparents), so the WAW edge points Early -> Late.
    ASSERT_EQ(compiled.executionOrder.size(), 2u);
    EXPECT_EQ(compiled.executionOrder[0].index, 1u); // "Early" first.
    EXPECT_EQ(compiled.executionOrder[1].index, 0u); // "Late" second.
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// Two passes in the same event tier with a WAW dependency keep their declaration order.
TEST(RenderGraphCompilerTest, PassesSharingTheSameRenderPassEventTierPreserveTheirOriginalDeclarationOrder)
{
    RenderGraphBuilder builder;
    const RenderTarget swapchainTarget{};
    const TextureHandle swapchain = builder.ImportTexture("Swapchain", swapchainTarget, ImageLayout::Undefined);

    builder.AddRenderPass(
        "ClearPass", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(swapchain); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 0
    builder.AddRenderPass(
        "OverlayPass", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(swapchain); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1 - same tier as ClearPass.

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { swapchain };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
}

// --- Cycle detection ---------------------------------------------------------
// No test: edges only go from a lower to a higher declaration index, so no builder input can form a cycle.

// --- Multiple render targets -------------------------------------------------
// A pass with three color writes survives culling entirely when a later pass reads all three.
TEST(RenderGraphCompilerTest, PassWithThreeColorWritesSurvivesCompileAndIsNotCulled)
{
    RenderGraphBuilder builder;
    const TextureHandle albedo = builder.CreateTexture("Albedo", MakeTextureDesc());
    const TextureHandle normal = builder.CreateTexture("Normal", MakeTextureDesc());
    const TextureHandle motion = builder.CreateTexture("Motion", MakeTextureDesc());
    const TextureHandle composited = builder.CreateTexture("Composited", MakeTextureDesc());

    builder.AddPass(
        "GBufferPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(albedo);
            pass.WriteColorAttachment(normal);
            pass.WriteColorAttachment(motion);
        },
        NoOpExecute); // index 0 - the G-buffer-shaped 3-write pass under test.
    builder.AddPass(
        "GBufferVisualizePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(albedo);
            pass.ReadTexture(normal);
            pass.ReadTexture(motion);
            pass.WriteColorAttachment(composited);
        },
        NoOpExecute); // index 1 - keeps all three G-buffer writes reachable.

    CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].colorAttachments.size(), 3u);
    const TextureHandle finalOutputs[] = { composited };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// --- MRT pass next to an unrelated buffer-write pass -------------------------
// colorAttachments and writes are separate lists: an MRT pass and a plain buffer write must cull and order independently.
TEST(RenderGraphCompilerTest, MrtPassAndSeparateNonAttachmentBufferWritePassBothCullAndOrderCorrectly)
{
    RenderGraphBuilder builder;
    const TextureHandle albedo = builder.CreateTexture("Albedo", MakeTextureDesc());
    const TextureHandle normal = builder.CreateTexture("Normal", MakeTextureDesc());
    const TextureHandle composited = builder.CreateTexture("Composited", MakeTextureDesc());
    const BufferHandle bufferB = builder.CreateBuffer("B", BufferDesc{ 1024, BufferUsage::StorageBuffer });
    const TextureHandle bufferConsumerOutput = builder.CreateTexture("BufferConsumerOutput", MakeTextureDesc());

    builder.AddPass(
        "GBufferPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(albedo);
            pass.WriteColorAttachment(normal);
        },
        NoOpExecute); // index 0 - the MRT pass under test: a real, ordered 2-entry colorAttachments list.
    builder.AddPass(
        "GBufferVisualizePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(albedo);
            pass.ReadTexture(normal);
            pass.WriteColorAttachment(composited);
        },
        NoOpExecute); // index 1 - keeps both MRT writes reachable.
    builder.AddPass(
        "BufferWriterPass",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(bufferB, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute); // index 2 - an ordinary, non-attachment write; never touches colorAttachments at all.
    builder.AddPass(
        "BufferReaderPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadBuffer(bufferB, ResourceAccess::ComputeShaderRead);
            pass.WriteColorAttachment(bufferConsumerOutput);
        },
        NoOpExecute); // index 3 - keeps the buffer-write chain reachable, entirely independent of the MRT chain above.

    CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes[0].colorAttachments.size(), 2u);
    ASSERT_TRUE(input.passes[2].colorAttachments.empty());
    const TextureHandle finalOutputs[] = { composited, bufferConsumerOutput };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2, 3 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_FALSE(input.passes[2].isCulled);
    EXPECT_FALSE(input.passes[3].isCulled);
}

// --- Duplicate edges, long chains, large graphs, self-exclusion --------------
// Two reads of the same resource resolving to one earlier writer: the writer precedes the reader once.
TEST(RenderGraphCompilerTest, PassWithTwoReadsOfSameResourceResolvingToSameEarlierWriterOrdersCorrectlyWithNoDuplicateEdgeIssues)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());

    builder.AddPass(
        "Writer", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute); // index 0
    builder.AddPass(
        "ReaderTwice",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            // Two reads of the same resource resolve to the same earlier writer.
            pass.ReadTexture(t0);
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute); // index 1

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t1 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// Writes to two resources that share the same earlier writer produce one ordering, not a duplicate-edge problem.
TEST(RenderGraphCompilerTest, PassWithTwoWritesToDifferentResourcesSharingTheSameEarlierWriterOrdersCorrectlyWithNoDuplicateEdgeIssues)
{
    RenderGraphBuilder builder;
    const TextureHandle r1 = builder.CreateTexture("R1", MakeTextureDesc());
    const TextureHandle r2 = builder.CreateTexture("R2", MakeTextureDesc());

    builder.AddPass(
        "FirstWriterOfBoth",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(r1);
            pass.WriteColorAttachment(r2);
        },
        NoOpExecute); // index 0
    builder.AddPass(
        "SecondWriterOfBoth",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            // Two WAW hazards against the same earlier writer.
            pass.WriteColorAttachment(r1);
            pass.WriteColorAttachment(r2);
        },
        NoOpExecute); // index 1

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { r1, r2 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// A chain of 3 writers then a reader runs in declaration order.
TEST(RenderGraphCompilerTest, LongWriteAfterWriteChainOrdersAllWritersThenTheFinalReaderInSequence)
{
    RenderGraphBuilder builder;
    const TextureHandle shared = builder.CreateTexture("Shared", MakeTextureDesc());
    const TextureHandle finalOutput = builder.CreateTexture("Final", MakeTextureDesc());

    builder.AddPass(
        "Writer1", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(shared); }, NoOpExecute); // index 0
    builder.AddPass(
        "Writer2", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(shared); }, NoOpExecute); // index 1
    builder.AddPass(
        "Writer3", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(shared); }, NoOpExecute); // index 2
    builder.AddPass(
        "Reader",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(shared);
            pass.WriteColorAttachment(finalOutput);
        },
        NoOpExecute); // index 3

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { finalOutput };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2, 3 }));
    for (const auto& pass : input.passes) {
        EXPECT_FALSE(pass.isCulled);
    }
}

// Correctness at scale: one root, 50 independent fan passes, one final pass reading all fan outputs.
TEST(RenderGraphCompilerTest, LargeFanOutFanInGraphCompilesCorrectlyExercisingTheAdjacencyListPathAtScale)
{
    constexpr std::uint32_t kFanCount = 50;

    RenderGraphBuilder builder;
    const TextureHandle root = builder.CreateTexture("Root", MakeTextureDesc());
    std::vector<TextureHandle> middle;
    middle.reserve(kFanCount);
    for (std::uint32_t i = 0; i < kFanCount; ++i) {
        middle.push_back(builder.CreateTexture("Middle", MakeTextureDesc()));
    }
    const TextureHandle finalOutput = builder.CreateTexture("Final", MakeTextureDesc());

    builder.AddPass(
        "Root", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(root); }, NoOpExecute); // index 0

    for (std::uint32_t i = 0; i < kFanCount; ++i) {
        const TextureHandle handle = middle[i];
        builder.AddPass(
            "Middle",
            [&, handle](RenderGraphBuilder::PassBuilder& pass) {
                pass.ReadTexture(root);
                pass.WriteColorAttachment(handle);
            },
            NoOpExecute); // index i + 1
    }

    builder.AddPass(
        "FinalPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            for (const TextureHandle& handle : middle) {
                pass.ReadTexture(handle);
            }
            pass.WriteColorAttachment(finalOutput);
        },
        NoOpExecute); // index kFanCount + 1

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { finalOutput };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    // Same event tier for all fan passes, so the order is the identity permutation [0 .. kFanCount + 1].
    std::vector<std::uint32_t> expected(kFanCount + 2);
    for (std::uint32_t i = 0; i < expected.size(); ++i) {
        expected[i] = i;
    }
    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, expected));
    for (const auto& pass : input.passes) {
        EXPECT_FALSE(pass.isCulled);
    }
}

// A pass that reads and writes a resource nothing else writes must not be reported as contradicting itself.
// Without the self-exclusion guard Compile() would assert and take down the test process.
TEST(RenderGraphCompilerTest, SelfExclusionGuardPreventsFalsePositiveContradictionForSelfLoopReadWriteOfNeverOtherwiseWrittenResource)
{
    RenderGraphBuilder builder;
    const TextureHandle depth = builder.CreateTexture("Depth", TextureDesc{ 64, 64, PixelFormat::D32_Float, true });

    builder.AddRenderPass(
        "DepthPass", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(depth, ResourceAccess::DepthStencilAttachmentReadWrite);
            pass.WriteDepthStencilAttachment(depth);
        },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { depth };

    // Must not crash/abort.
    const CompiledGraph compiled = Compile(input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
}

// A read of a never-written resource is no contradiction for Compile().
TEST(RenderGraphCompilerTest, ReadOfNeverWrittenResourceProducesNoContradiction)
{
    RenderGraphBuilder builder;
    const TextureHandle neverWritten = builder.CreateTexture("NeverWritten", MakeTextureDesc());
    const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    builder.AddRenderPass(
        "ReaderOfNeverWritten", PassKind::Graphics,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(neverWritten);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques);

    CompiledGraphInput input = builder.Finish();

    const TextureHandle finalOutputs[] = { output };
    // Must not crash/abort: a read with no writer anywhere is no contradiction.
    const CompiledGraph compiled = Compile(input, finalOutputs);
    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0 }));
    EXPECT_FALSE(input.passes[0].isCulled);
}

#ifndef NDEBUG

// Compile() aborts on an orphan read with a later writer.
// Only this contradiction kind is reachable through Compile().
TEST(RenderGraphCompilerDeathTest, FastPathDetectsOrphanReadWithLaterWriterAndAbortsJustLikeTheStandaloneFunctionWould)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    // Same event tier keeps declaration order, so the reader is walked before its writer (an orphan read).
    builder.AddRenderPass(
        "Composite", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 0 - reader, declared FIRST.
    builder.AddRenderPass(
        "Opaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1 - writer, declared SECOND, SAME tier.

    CompiledGraphInput input = builder.Finish();

    EXPECT_DEATH({ Compile(input, {}); }, "");
}

#endif // !NDEBUG

} // namespace
} // namespace gte::rg
