// Unit tests for Phase 3's pure "Smart Planner" compilation algorithm
// (src/Renderer/RenderGraph/RenderGraphCompiler.h) - dependency resolution,
// culling, deterministic topological ordering, and resource lifetimes. No
// live VkDevice/Renderer involved at all - see
// RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md, Step 3.4, for the coverage
// list this file follows, and RENDERGRAPH_PHASE3_COMPLETION_REPORT.md for a
// documented finding on why a literal "cycle among declared passes" case
// cannot actually be constructed through RenderGraphBuilder (see the
// "Cycle detection" section below).
//
// Every fixture below is built through a real RenderGraphBuilder (readable,
// and proves this compiler works against exactly what Phase 2 actually
// hands off) rather than hand-poking CompiledGraphInput's fields directly -
// this phase's own strategy document explicitly allows either approach.

#include "Renderer/RenderGraph/RenderGraphCompiler.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

TextureDesc MakeTextureDesc()
{
    return TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
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

    // A must precede both B and C; both B and C must precede D; B/C's own
    // relative order matches their declaration order (1 before 2) - the
    // exact determinism this phase's Step 3.3 requires.
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
    const TextureHandle swapchain = builder.ImportTexture("Swapchain", swapchainTarget, VK_IMAGE_LAYOUT_UNDEFINED);

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

    // executionOrder is {A=0, B=1, C=2, D=3} (positions == declaration
    // index here, see the diamond test above) - T0 is written at position
    // 0 and read by both B (position 1) and C (position 2), so its
    // lifetime spans [0, 2] (the LATER of the two readers), not [0, 1].
    ASSERT_EQ(compiled.textureLifetimes.size(), 4u);
    const ResourceLifetime& t0Lifetime = compiled.textureLifetimes[t0.index];
    EXPECT_EQ(t0Lifetime.firstUsePassIndex, 0);
    EXPECT_EQ(t0Lifetime.lastUsePassIndex, 2);

    // TFinal is only ever touched by D, the LAST pass in executionOrder -
    // its lastUsePassIndex must equal that pass's own position (3), never
    // one-past-the-end (an off-by-one this test exists to pin down).
    const ResourceLifetime& finalLifetime = compiled.textureLifetimes[tFinal.index];
    EXPECT_EQ(finalLifetime.firstUsePassIndex, 3);
    EXPECT_EQ(finalLifetime.lastUsePassIndex, 3);
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

    // An independently, identically-constructed second copy - proves
    // Compile() itself is deterministic, not merely that re-running it on
    // the exact same object is a no-op.
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

// --- Edge cases (Step 5's own "every edge case a compilers/graph-
// algorithms course would flag" bar) -------------------------------------

TEST(RenderGraphCompilerTest, SelfLoopReadAndWriteOfSameResourceDoesNotCrashOrCycle)
{
    RenderGraphBuilder builder;
    const TextureHandle depth = builder.CreateTexture("Depth", TextureDesc{ 64, 64, VK_FORMAT_D32_SFLOAT, true });

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
    // A pass that declares nothing at all - can never be a root, can
    // never be reached backwards from one either.
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

    // Nothing writes `neverWritten`, so there is no root at all - the one
    // declared pass simply has no path to any real final output and is
    // culled, exactly as if finalOutputs had been empty.
    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

// --- Compute-shader campaign Phase 5's own buffer-reachability caveat -----
// (COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md, Step 6) -------------------
//
// RenderGraphCompiler::Compile()'s `finalOutputs` root set is TextureHandle-
// only - there is no BufferHandle equivalent, and RenderGraphBuilder has no
// ImportBuffer() counterpart to ImportTexture() at all. A compute pass whose
// ONLY declared write is a buffer can therefore be silently culled unless
// some OTHER, ALSO-kept pass reads that buffer and itself has a path (direct
// or transitive) to a real texture write in `finalOutputs` - exactly the
// buffer-side sibling of every existing texture-culling test above. Three
// passes: PassA writes buffer B (no texture write of its own) - PassB reads
// B and writes texture T, a real finalOutputs root - PassC ALSO writes
// buffer B but has no reader and no path to finalOutputs at all.
TEST(RenderGraphCompilerTest, BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput)
{
    RenderGraphBuilder builder;
    const BufferHandle bufferB = builder.CreateBuffer("B", BufferDesc{ 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
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

    // PassA/PassB both survive (PassA's buffer write is kept alive
    // transitively through PassB's own real texture output), in that
    // execution order; PassC is correctly culled.
    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1 }));
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
    EXPECT_TRUE(input.passes[2].isCulled);
}

// --- Atmosphere Scattering campaign, Phase 6's own VolumeTextureHandle
// root-set fix (ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md) -
//
// Before this phase, `finalOutputs` was the ONLY root set Compile() ever
// consulted, and it is TextureHandle-only - a pass whose ONLY declared
// write was a VolumeTextureHandle had structurally NO way to survive
// culling, no matter what it wrote (this was a genuine Phase 2
// infrastructure gap, discovered by Phase 6's own real workload - see
// ATMOSPHERE_PHASE6_COMPLETION_REPORT.md). RenderGraphBuilder::
// KeepVolumeTextureOutput() is the fix: a SECOND, independent root set
// (CompiledGraphInput::finalVolumeTextureOutputs) Compile() now also
// checks. VolumeA is explicitly kept via KeepVolumeTextureOutput() and
// must survive; VolumeB is written but never kept/read by anything and
// must still be culled, proving this fix is correctly OPT-IN, not a
// blanket "every volume texture write always survives" regression.
TEST(RenderGraphCompilerTest, VolumeTextureOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput)
{
    RenderGraphBuilder builder;
    const VolumeTarget dummyVolumeTarget{};
    const VolumeTextureHandle volumeA =
        builder.ImportVolumeTexture("VolA", dummyVolumeTarget, VK_IMAGE_LAYOUT_UNDEFINED);
    const VolumeTextureHandle volumeB =
        builder.ImportVolumeTexture("VolB", dummyVolumeTarget, VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "WritesKeptVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteVolumeTexture(volumeA, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 0 - its only write is explicitly kept as a root below.
    builder.AddComputePass(
        "WritesUnkeptVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteVolumeTexture(volumeB, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute); // index 1 - writes a DIFFERENT volume texture, never kept/read - dead code.

    builder.KeepVolumeTextureOutput(volumeA);

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
    const VolumeTextureHandle volume = builder.ImportVolumeTexture("Vol", dummyVolumeTarget, VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "WritesVolume",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteVolumeTexture(volume, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);

    // Deliberately never call KeepVolumeTextureOutput() - this is the exact
    // pre-fix behavior (and remains correct post-fix): a volume texture
    // write nobody ever marks as a root is dead code, same as any other
    // never-read/never-kept resource.
    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

// --- GPU Vertex Skinning campaign, Phase 3's own WAW-hazard mitigation -----
// (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md, Step 3.6) -
//
// Two "SkinModel" compute passes sharing ONE imported output buffer (the
// two-SkeletalAnimators-sharing-one-Mesh scenario this campaign's own
// documented limitation describes), followed by a graphics pass that reads
// that same buffer as a vertex buffer. The SECOND skinning pass applies the
// Step 3.6 mitigation (a phantom ComputeShaderRead declared immediately
// before its real ComputeShaderWrite) - this test proves the COMPILER
// (independent of the barrier planner's own field-level tests in
// RenderGraphBarrierPlannerTests.cpp) still produces the correct, fully
// serialized execution order: first writer, then second writer/reader,
// then the draw - and that none of the three passes are ever culled, since
// all three have a real path to the final color output.
TEST(RenderGraphCompilerTest, GpuSkinningReadBeforeWriteMitigationPreservesOrderForSharedOutputBuffer)
{
    RenderGraphBuilder builder;
    const BufferHandle output = builder.ImportBuffer("SkinOutput", VK_NULL_HANDLE, 1024);
    const TextureHandle color = builder.CreateTexture("Color", MakeTextureDesc());

    builder.AddComputePass(
        "SkinModel:InstanceA",
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(output, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute); // index 0 - first animator's own skinning dispatch.
    builder.AddComputePass(
        "SkinModel:InstanceB",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            // The Step 3.6 mitigation - a phantom read declared BEFORE the
            // real write, forcing a dependency edge/barrier against
            // whichever pass most recently wrote this same handle, even
            // though this pass's own compute shader never actually reads
            // the buffer's prior contents.
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

// --- render-pass-4 campaign, PHASE1 -----------------------------------------
// (task_manager/render-pass-4/PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md)
//
// DetectRenderPassEventContradictions() is exercised DIRECTLY here (never
// through Compile()) - it is a pure, side-effect-free function that never
// prints or asserts, so it is completely safe to call with a deliberately
// contradicting graph, unlike Compile() itself (which turns a non-empty
// result into an unconditional stderr report plus a debug-build assert()).

TEST(RenderGraphCompilerTest, NoReadsOrWritesProducesNoContradictions)
{
    RenderGraphBuilder builder;
    CompiledGraphInput input = builder.Finish();

    const std::vector<RenderPassEventContradiction> contradictions =
        DetectRenderPassEventContradictions(input, {});

    EXPECT_TRUE(contradictions.empty());
}

TEST(RenderGraphCompilerTest, NormalWriterBeforeReaderProducesNoContradictions)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddRenderPass(
        "Opaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 0
    builder.AddRenderPass(
        "AfterOpaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterOpaques); // index 1

    CompiledGraphInput input = builder.Finish();
    const std::int32_t identity[] = { 0, 1 };

    const std::vector<RenderPassEventContradiction> contradictions =
        DetectRenderPassEventContradictions(input, identity);

    EXPECT_TRUE(contradictions.empty());
}

// The permanent regression test for the exact historical bug (PHASE0's
// Locked Design Decision 5) - a reader ("Composite") declared BEFORE its
// writer ("Opaque"), exactly reproducing AtmosphereComposite/RenderOpaque's
// real, historical shape (see PHASE0_MASTER_STRATEGY.md, Step 2).
TEST(RenderGraphCompilerTest, OrphanReadWithLaterWriterIsDetected)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddRenderPass(
        "Composite", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::AfterTransparents); // index 0 - reader, declared FIRST.
    builder.AddRenderPass(
        "Opaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1 - writer, declared SECOND.

    CompiledGraphInput input = builder.Finish();
    const std::int32_t identity[] = { 0, 1 };

    const std::vector<RenderPassEventContradiction> contradictions =
        DetectRenderPassEventContradictions(input, identity);

    ASSERT_EQ(contradictions.size(), 1u);
    EXPECT_EQ(contradictions[0].kind, RenderPassEventContradictionKind::OrphanReadWithLaterWriter);
    EXPECT_EQ(contradictions[0].readerPassIndex, 0);
    EXPECT_EQ(contradictions[0].writerPassIndex, 1);
}

TEST(RenderGraphCompilerTest, EdgeContradictingDeclaredEventOrderIsDetected)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddRenderPass(
        "Opaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh,
        RenderPassEvent::AfterTransparents); // index 0 - writer, DELIBERATELY mistagged late.
    builder.AddRenderPass(
        "Composite", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh,
        RenderPassEvent::Opaques); // index 1 - reader, DELIBERATELY mistagged early.

    CompiledGraphInput input = builder.Finish();
    const std::int32_t identity[] = { 0, 1 };

    const std::vector<RenderPassEventContradiction> contradictions =
        DetectRenderPassEventContradictions(input, identity);

    ASSERT_EQ(contradictions.size(), 1u);
    EXPECT_EQ(contradictions[0].kind, RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency);
    EXPECT_EQ(contradictions[0].readerPassIndex, 1);
    EXPECT_EQ(contradictions[0].writerPassIndex, 0);
}

TEST(RenderGraphCompilerTest, SameEventTierNeverProducesAContradiction)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddRenderPass(
        "Opaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 0
    builder.AddRenderPass(
        "AlsoOpaque", PassKind::Graphics, [&](RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(t0); },
        NoOpExecute, RenderPassDrawKind::DrawMesh, RenderPassEvent::Opaques); // index 1 - same tier as the writer.

    CompiledGraphInput input = builder.Finish();
    const std::int32_t identity[] = { 0, 1 };

    const std::vector<RenderPassEventContradiction> contradictions =
        DetectRenderPassEventContradictions(input, identity);

    EXPECT_TRUE(contradictions.empty());
}

// Confirms 3.2's new Compile() wiring doesn't fire a false positive against
// a graph that was always fine, and that executionOrder/isCulled are
// unchanged from what DiamondDependencyOrdersCorrectlyWithDeterministicSiblingOrder
// (above) already asserts - a consistent graph must never abort here.
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

// --- render-pass-4 campaign, PHASE2 -----------------------------------------
// (task_manager/render-pass-4/PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md)
//
// The one genuine, deliberate BEHAVIOR CHANGE in this whole campaign: Compile()
// now processes passes in an "effective order" (every pass stable-sorted by
// RenderPassEvent, ties broken by original declaration index) for both its
// RAW/WAW edge scan and Kahn's-algorithm ready-set tie-break, instead of raw
// declaration order.

// The permanent regression test for the REAL fix - mirrors PHASE1's own
// OrphanReadWithLaterWriterIsDetected, but calls the REAL Compile() end-to-end
// instead of just the detector. Reproduces the exact historical
// AtmosphereComposite/RenderOpaque bug shape: "Composite" (a reader, tagged
// AfterTransparents) declared BEFORE "Opaque" (its writer, tagged Opaques).
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

    // Before this phase: "Opaque" (index 1) had no RAW edge linking it to
    // "Composite" at all (its write was never "declared so far" when the
    // reader was processed), and no other path to a final output either - it
    // was silently CULLED, leaving executionOrder with only "Composite" in
    // it. After this phase: effective order walks "Opaque" (Opaques) before
    // "Composite" (AfterTransparents) regardless of declaration order, so the
    // RAW edge forms correctly and both passes survive, in the correct order.
    ASSERT_EQ(compiled.executionOrder.size(), 2u);
    EXPECT_EQ(compiled.executionOrder[0].index, 1u); // "Opaque" (writer) first.
    EXPECT_EQ(compiled.executionOrder[1].index, 0u); // "Composite" (reader) second.
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// Two passes with NO shared resource at all - proves RenderPassEvent now
// decides relative order even between passes with zero real dependency on
// each other (Kahn's-algorithm ready-set tie-break), overriding declaration
// order, exactly this phase's own stated goal.
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

// A second, WAW-shaped regression test for this phase's own reorder - the RAW
// test above only exercises the read-side "orphan" fix; this one exercises
// the write-side WAW edge-construction rewire directly.
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

    // Before this phase: raw declaration order processes "Late" (0) then
    // "Early" (1), forming a WAW edge 0->1 ("Late" before "Early"). After
    // this phase: effective order processes "Early" (Opaques) before "Late"
    // (AfterTransparents), so the edge direction REVERSES (1->0).
    ASSERT_EQ(compiled.executionOrder.size(), 2u);
    EXPECT_EQ(compiled.executionOrder[0].index, 1u); // "Early" first.
    EXPECT_EQ(compiled.executionOrder[1].index, 0u); // "Late" second.
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

// Confirms the stable-sort tie-break: two passes sharing the SAME
// RenderPassEvent tier, with a real WAW dependency between them, must still
// execute in their original declaration order, byte-identical to before this
// phase (mirrors MultipleWritersToSameResourcePreserveWriteAfterWriteOrder's
// own graph shape above, using the AddRenderPass() overload instead).
TEST(RenderGraphCompilerTest, PassesSharingTheSameRenderPassEventTierPreserveTheirOriginalDeclarationOrder)
{
    RenderGraphBuilder builder;
    const RenderTarget swapchainTarget{};
    const TextureHandle swapchain = builder.ImportTexture("Swapchain", swapchainTarget, VK_IMAGE_LAYOUT_UNDEFINED);

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

// --- Cycle detection --------------------------------------------------------
//
// RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md's own Step 3.4 asks for a
// "pass A reads what B writes AND writes what B reads -> throws" test.
// This is deliberately NOT implemented as a literal test here - see
// RenderGraphCompiler.cpp's own header comment and
// RENDERGRAPH_PHASE3_COMPLETION_REPORT.md for the full reasoning: this
// compiler's edge-construction rule (an edge only ever goes from a
// strictly-lower declaration index to a strictly-higher one) makes the
// produced graph a DAG BY CONSTRUCTION, so no CompiledGraphInput any
// RenderGraphBuilder caller can actually produce is capable of forming a
// real cycle. The throw path itself is still implemented (see
// RenderGraphCompiler.cpp) as correct defensive code, exactly as this
// phase's own strategy document asks for ("Kahn's algorithm... naturally
// DETECTS a cycle... without a separate... pass") - it simply has no
// reachable unit test given today's edge-construction rule, the same way
// a `default:` branch of a switch already proven exhaustive by the
// compiler would have no reachable test either.

// --- Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 -------
//
// Directly proves PHASE0_MASTER_STRATEGY.md's own confirmed claim that
// RenderGraphCompiler::Compile() needs ZERO changes to correctly handle a
// REAL 3-color-write G-buffer-shaped pass (rather than trusting that claim
// blindly): a single pass declares three WriteColorAttachment() calls
// against three distinct handles, all three kept reachable via a later pass
// that reads them all and itself reaches a real finalOutputs root - the
// G-buffer pass must survive culling entirely (not just partially).
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

// --- Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE5 -------
//
// Step 3.2's own required regression guard: PHASE1's `ColorAttachmentDesc`
// doc comment flags a real risk - "writes order doesn't necessarily match
// colorAttachments order" - since `PassRecord::writes` (the generic vector
// every ResourceAccess, of any kind, is pushed onto) and
// `PassRecord::colorAttachments` (the new, PHASE1-added, color-attachment-
// only ordered list) are two SEPARATE vectors populated by two SEPARATE
// code paths. This test proves a pass carrying a real, ordered,
// multi-entry `colorAttachments` list (the MRT case) coexists correctly,
// in the SAME graph, with a completely unrelated pass declaring an
// ordinary non-attachment buffer write (`WriteBuffer()`,
// `ResourceAccess::ComputeShaderWrite` - never pushed onto
// `colorAttachments` at all) - both halves must independently cull/order
// correctly, with neither one's presence perturbing the other's.
TEST(RenderGraphCompilerTest, MrtPassAndSeparateNonAttachmentBufferWritePassBothCullAndOrderCorrectly)
{
    RenderGraphBuilder builder;
    const TextureHandle albedo = builder.CreateTexture("Albedo", MakeTextureDesc());
    const TextureHandle normal = builder.CreateTexture("Normal", MakeTextureDesc());
    const TextureHandle composited = builder.CreateTexture("Composited", MakeTextureDesc());
    const BufferHandle bufferB = builder.CreateBuffer("B", BufferDesc{ 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
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

} // namespace
} // namespace gte::rg
