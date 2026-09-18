// Unit tests for Phase 8's pure "flatten a compiled graph into something
// displayable" reshape (src/Renderer/RenderGraph/RenderGraphSnapshot.h). No
// live VkDevice/Renderer/RenderGraph involved at all - every fixture below is
// built through a real RenderGraphBuilder/RenderGraphCompiler::Compile()
// (mirroring RenderGraphCompilerTests.cpp's own convention), with
// `statsLookup` supplied as a plain, hand-fabricated lambda rather than a
// real RenderGraph::LastKnownStatsFor() - exactly what keeps
// BuildRenderGraphSnapshot() itself Tier-1-testable despite living
// alongside RenderGraph.h/.cpp (which are Tier 2 - see TESTING.md).

#include "Renderer/RenderGraph/RenderGraphSnapshot.h"

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

TextureDesc MakeTextureDesc()
{
    return TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
}

PassGpuStats MakeStats(std::uint32_t drawCalls, std::uint32_t triangles)
{
    PassGpuStats stats;
    stats.drawStats.drawCallCount = drawCalls;
    stats.drawStats.triangleCount = triangles;
    stats.timing.status = GpuTimingSample::Status::Present;
    stats.timing.milliseconds = 1.5;
    return stats;
}

// A statsLookup stand-in that returns a canned, name-keyed value AND
// records every name it was actually called with - so a test can assert
// BuildRenderGraphSnapshot() never calls it for a culled pass (see
// RenderGraphSnapshot.h's own doc comment on why).
class RecordingStatsLookup {
public:
    PassGpuStats operator()(const char* name)
    {
        calledWith.push_back(name != nullptr ? name : "");
        auto it = canned.find(name != nullptr ? name : "");
        return it != canned.end() ? it->second : PassGpuStats{};
    }

    std::map<std::string, PassGpuStats> canned;
    std::vector<std::string> calledWith;
};

// --- Empty graph -------------------------------------------------------------

TEST(RenderGraphSnapshotTest, EmptyGraphProducesEmptySnapshot)
{
    RenderGraphBuilder builder;
    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});

    EXPECT_TRUE(snapshot.passesInExecutionOrder.empty());
    EXPECT_TRUE(snapshot.resources.empty());
}

// --- Surviving passes: order, names, stats ------------------------------------

TEST(RenderGraphSnapshotTest, SurvivingPassesAppearInExecutionOrderWithResolvedNamesAndStats)
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

    RecordingStatsLookup lookup;
    lookup.canned["A"] = MakeStats(3, 100);
    lookup.canned["B"] = MakeStats(5, 250);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, std::ref(lookup));
    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 2u);

    const RenderGraphPassSnapshot& passA = snapshot.passesInExecutionOrder[0];
    EXPECT_EQ(passA.name, "A");
    EXPECT_FALSE(passA.isCulled);
    EXPECT_TRUE(passA.readNames.empty());
    ASSERT_EQ(passA.writeNames.size(), 1u);
    EXPECT_EQ(passA.writeNames[0], "T0");
    EXPECT_EQ(passA.stats.drawStats.drawCallCount, 3u);
    EXPECT_EQ(passA.stats.drawStats.triangleCount, 100u);

    const RenderGraphPassSnapshot& passB = snapshot.passesInExecutionOrder[1];
    EXPECT_EQ(passB.name, "B");
    EXPECT_FALSE(passB.isCulled);
    ASSERT_EQ(passB.readNames.size(), 1u);
    EXPECT_EQ(passB.readNames[0], "T0");
    ASSERT_EQ(passB.writeNames.size(), 1u);
    EXPECT_EQ(passB.writeNames[0], "T1");
    EXPECT_EQ(passB.stats.drawStats.drawCallCount, 5u);
    EXPECT_EQ(passB.stats.drawStats.triangleCount, 250u);

    // statsLookup was called exactly once per surviving pass, by name.
    EXPECT_EQ(lookup.calledWith.size(), 2u);
    EXPECT_EQ(lookup.calledWith[0], "A");
    EXPECT_EQ(lookup.calledWith[1], "B");
}

// --- Culled passes: still visible, appended after, stats never resolved ------

TEST(RenderGraphSnapshotTest, CulledPassesAreAppendedAfterSurvivorsWithDefaultStatsAndNeverQueried)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());
    const TextureHandle deadEnd = builder.CreateTexture("DeadEnd", MakeTextureDesc());

    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    builder.AddPass(
        "B",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            pass.WriteColorAttachment(t1);
        },
        NoOpExecute);
    // Never read by anything - culled.
    builder.AddPass(
        "Unused", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(deadEnd); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t1 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    RecordingStatsLookup lookup;
    lookup.canned["Unused"] = MakeStats(99, 999); // Must never be reflected in the snapshot.

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, std::ref(lookup));

    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 3u);
    EXPECT_EQ(snapshot.passesInExecutionOrder[0].name, "A");
    EXPECT_EQ(snapshot.passesInExecutionOrder[1].name, "B");

    const RenderGraphPassSnapshot& culled = snapshot.passesInExecutionOrder[2];
    EXPECT_EQ(culled.name, "Unused");
    EXPECT_TRUE(culled.isCulled);
    EXPECT_EQ(culled.stats.drawStats.drawCallCount, 0u);
    EXPECT_EQ(culled.stats.drawStats.triangleCount, 0u);
    EXPECT_EQ(culled.stats.timing.status, GpuTimingSample::Status::Absent);

    // "Unused" must never have been passed to statsLookup at all.
    for (const std::string& calledName : lookup.calledWith) {
        EXPECT_NE(calledName, "Unused");
    }
}

// --- Resources: name/imported/lifetime carried over unchanged ----------------

TEST(RenderGraphSnapshotTest, ImportedAndTransientTextureResourcesReportCorrectKindAndLifetime)
{
    RenderGraphBuilder builder;
    const RenderTarget swapchainTarget{};
    const TextureHandle swapchain = builder.ImportTexture("Swapchain", swapchainTarget, VK_IMAGE_LAYOUT_UNDEFINED);
    const TextureHandle scratch = builder.CreateTexture("Scratch", MakeTextureDesc());

    builder.AddPass(
        "WriteScratch", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(scratch); },
        NoOpExecute);
    builder.AddPass(
        "CompositeToSwapchain",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(scratch);
            pass.WriteColorAttachment(swapchain);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { swapchain };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});

    ASSERT_EQ(snapshot.resources.size(), 2u);

    const RenderGraphResourceSnapshot& swapchainResource = snapshot.resources[swapchain.index];
    EXPECT_EQ(swapchainResource.name, "Swapchain");
    EXPECT_TRUE(swapchainResource.isImported);
    EXPECT_EQ(swapchainResource.firstUsePassIndex, compiled.textureLifetimes[swapchain.index].firstUsePassIndex);
    EXPECT_EQ(swapchainResource.lastUsePassIndex, compiled.textureLifetimes[swapchain.index].lastUsePassIndex);

    const RenderGraphResourceSnapshot& scratchResource = snapshot.resources[scratch.index];
    EXPECT_EQ(scratchResource.name, "Scratch");
    EXPECT_FALSE(scratchResource.isImported);
    EXPECT_EQ(scratchResource.firstUsePassIndex, 0);
    EXPECT_EQ(scratchResource.lastUsePassIndex, 1);
}

// A transient buffer (created via CreateBuffer(), never ImportBuffer()) is
// never reported as imported - renamed from this test's original name,
// "BufferResourceIsNeverReportedAsImported", once the GPU Vertex Skinning
// campaign's Phase 3
// (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md) added a
// real RenderGraphBuilder::ImportBuffer() - see
// ImportedBufferResourceIsReportedAsImported below for the now-possible
// opposite case.
TEST(RenderGraphSnapshotTest, TransientBufferResourceIsNotReportedAsImported)
{
    RenderGraphBuilder builder;
    const BufferHandle scratchBuffer = builder.CreateBuffer("ScratchBuffer", BufferDesc{ 256, 0 });
    const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    builder.AddPass(
        "WriteBuffer", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(scratchBuffer); }, NoOpExecute);
    builder.AddPass(
        "ReadBufferWriteOutput",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadBuffer(scratchBuffer);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { output };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});

    // One texture resource (Output) plus one buffer resource (ScratchBuffer).
    ASSERT_EQ(snapshot.resources.size(), 2u);
    const std::size_t bufferResourceIndex = 1u + scratchBuffer.index; // textures first, then buffers - see BuildRenderGraphSnapshot().
    ASSERT_LT(bufferResourceIndex, snapshot.resources.size());
    const RenderGraphResourceSnapshot& bufferResource = snapshot.resources[bufferResourceIndex];
    EXPECT_EQ(bufferResource.name, "ScratchBuffer");
    EXPECT_FALSE(bufferResource.isImported);
}

// A buffer resource created via ImportBuffer() IS reported as imported -
// mirrors ImportedAndTransientTextureResourcesReportCorrectKindAndLifetime
// above, now that a buffer counterpart of ImportTexture() exists.
TEST(RenderGraphSnapshotTest, ImportedBufferResourceIsReportedAsImported)
{
    RenderGraphBuilder builder;
    const BufferHandle importedBuffer = builder.ImportBuffer("SkinOutput", VK_NULL_HANDLE, 1024);
    const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    builder.AddPass(
        "SkinPass", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(importedBuffer, ResourceAccess::ComputeShaderWrite); },
        NoOpExecute);
    builder.AddPass(
        "DrawPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadBuffer(importedBuffer, ResourceAccess::VertexBufferRead);
            pass.WriteColorAttachment(output);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { output };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});

    ASSERT_EQ(snapshot.resources.size(), 2u);
    const std::size_t bufferResourceIndex = 1u + importedBuffer.index;
    ASSERT_LT(bufferResourceIndex, snapshot.resources.size());
    const RenderGraphResourceSnapshot& bufferResource = snapshot.resources[bufferResourceIndex];
    EXPECT_EQ(bufferResource.name, "SkinOutput");
    EXPECT_TRUE(bufferResource.isImported);
}

// --- A default-constructed (empty) statsLookup never crashes ------------------

TEST(RenderGraphSnapshotTest, EmptyStatsLookupLeavesEveryPassAtDefaultStats)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    builder.AddPass(
        "A", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t0 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});

    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 1u);
    EXPECT_EQ(snapshot.passesInExecutionOrder[0].stats.drawStats.drawCallCount, 0u);
    EXPECT_EQ(snapshot.passesInExecutionOrder[0].stats.timing.status, GpuTimingSample::Status::Absent);
}

// --- frame-debugger-5 campaign, PHASE1 - isComputePass (RENAMED to `kind` -----
// --- by the Render Pass campaign's own PHASE1, task_manager/render-pass-1) --
// --- / readKinds / writeKinds (PHASE1_RENDERGRAPH_COMPUTE_DISPATCH_CHOKEPOINT_INFRASTRUCTURE.md) ---

// `kind` is copied through correctly for a surviving graphics pass
// (AddPass() -> PassKind::Graphics) vs. a surviving compute pass
// (AddComputePass() -> PassKind::Compute), each keeping its own real
// name/execution-order position.
TEST(RenderGraphSnapshotTest, KindIsCopiedThroughForSurvivingGraphicsAndComputePasses)
{
    RenderGraphBuilder builder;
    const TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    TextureHandle t1;

    builder.AddPass(
        "GraphicsPass", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    builder.AddComputePass(
        "ComputePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(t0);
            t1 = builder.CreateTexture("T1", MakeTextureDesc());
            pass.WriteTexture(t1);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { t1 };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});
    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 2u);

    EXPECT_EQ(snapshot.passesInExecutionOrder[0].name, "GraphicsPass");
    EXPECT_EQ(snapshot.passesInExecutionOrder[0].kind, PassKind::Graphics);

    EXPECT_EQ(snapshot.passesInExecutionOrder[1].name, "ComputePass");
    EXPECT_EQ(snapshot.passesInExecutionOrder[1].kind, PassKind::Compute);
}

// A culled compute pass must still truthfully report kind == PassKind::Compute
// (and correct readKinds/writeKinds) - only `stats` stays defaulted for a
// culled pass, per this struct's own pre-existing convention.
TEST(RenderGraphSnapshotTest, CulledComputePassStillReportsComputeKindAndCorrectWriteKind)
{
    RenderGraphBuilder builder;
    const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());
    const TextureHandle deadEnd = builder.CreateTexture("DeadEnd", MakeTextureDesc());

    builder.AddPass(
        "Survivor", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(output); }, NoOpExecute);
    builder.AddComputePass(
        "CulledCompute", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(deadEnd); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { output };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});
    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 2u);

    const RenderGraphPassSnapshot& culled = snapshot.passesInExecutionOrder[1];
    EXPECT_EQ(culled.name, "CulledCompute");
    EXPECT_TRUE(culled.isCulled);
    EXPECT_EQ(culled.kind, PassKind::Compute);
    ASSERT_EQ(culled.writeKinds.size(), 1u);
    EXPECT_EQ(culled.writeKinds[0], ResourceKind::Texture);
    // stats still default for a culled pass - unchanged pre-existing rule.
    EXPECT_EQ(culled.stats.drawStats.drawCallCount, 0u);
    EXPECT_EQ(culled.stats.timing.status, GpuTimingSample::Status::Absent);
}

// --- frame-debugger-6 campaign, PHASE1 - viewScope --------------------------
// --- (PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_INFRASTRUCTURE.md) ------------

// viewScope is copied through correctly for BOTH a surviving pass (declared
// via the new 4-argument AddPass()/AddComputePass() overloads) AND a culled
// one - both loops in BuildRenderGraphSnapshot() route through the SAME
// shared BuildPassSnapshot() helper, so one assertion covering each case is
// enough to prove that one shared line runs correctly regardless of which
// loop invoked it (see this phase's own strategy document, Step 4).
TEST(RenderGraphSnapshotTest, ViewScopeIsCopiedThroughForSurvivingAndCulledPasses)
{
    RenderGraphBuilder builder;
    const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());
    const TextureHandle deadEnd = builder.CreateTexture("DeadEnd", MakeTextureDesc());
    TextureHandle skyView;

    // A plain AddPass() call with no ViewScope at all still defaults to
    // Shared - unaffected by this phase's own additive overloads.
    builder.AddPass(
        "GameView", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(output); }, NoOpExecute);

    // A surviving compute pass explicitly tagged GameView via the new
    // 4-argument AddComputePass() overload.
    builder.AddComputePass(
        "AtmosphereSkyViewLutPass", ViewScope::GameView,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            skyView = builder.CreateTexture("SkyView", MakeTextureDesc());
            pass.WriteTexture(skyView);
        },
        NoOpExecute);

    // A CULLED compute pass explicitly tagged SceneView - must still
    // truthfully report its own viewScope even though it never survives.
    builder.AddComputePass(
        "ComputeBlurValidation", ViewScope::SceneView,
        [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteTexture(deadEnd); }, NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { output, skyView };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});
    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 3u);

    const RenderGraphPassSnapshot& gameViewPass = snapshot.passesInExecutionOrder[0];
    EXPECT_EQ(gameViewPass.name, "GameView");
    EXPECT_FALSE(gameViewPass.isCulled);
    EXPECT_EQ(gameViewPass.viewScope, ViewScope::Shared);

    const RenderGraphPassSnapshot& skyViewPass = snapshot.passesInExecutionOrder[1];
    EXPECT_EQ(skyViewPass.name, "AtmosphereSkyViewLutPass");
    EXPECT_FALSE(skyViewPass.isCulled);
    EXPECT_EQ(skyViewPass.viewScope, ViewScope::GameView);

    const RenderGraphPassSnapshot& culledPass = snapshot.passesInExecutionOrder[2];
    EXPECT_EQ(culledPass.name, "ComputeBlurValidation");
    EXPECT_TRUE(culledPass.isCulled);
    EXPECT_EQ(culledPass.viewScope, ViewScope::SceneView);
}

// A single pass declaring a mix of texture/buffer/volume-texture reads AND
// writes ends up with readKinds/writeKinds exactly parallel to
// readNames/writeNames, each entry carrying the correct ResourceKind - and
// (regression coverage for the pre-existing ResourceUsageName() gap fixed
// by this same phase, see 3.4b) the volume-texture entries resolve to their
// REAL name, never an empty string.
TEST(RenderGraphSnapshotTest, ReadKindsAndWriteKindsMatchDeclaredResourceKindsIncludingVolumeTextureNames)
{
    RenderGraphBuilder builder;
    const TextureHandle textureHandle = builder.CreateTexture("Tex", MakeTextureDesc());
    const BufferHandle bufferHandle = builder.CreateBuffer("Buf", BufferDesc{ 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    const VolumeTarget volumeTarget{};
    const VolumeTextureHandle volumeHandle =
        builder.ImportVolumeTexture("Vol", volumeTarget, VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "MixedPass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(textureHandle, ResourceAccess::ComputeShaderRead);
            pass.ReadBuffer(bufferHandle, ResourceAccess::ComputeShaderRead);
            pass.ReadVolumeTexture(volumeHandle, ResourceAccess::ComputeShaderRead);
            pass.WriteTexture(textureHandle, ResourceAccess::ComputeShaderWrite);
            pass.WriteBuffer(bufferHandle, ResourceAccess::ComputeShaderWrite);
            pass.WriteVolumeTexture(volumeHandle, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);

    builder.KeepVolumeTextureOutput(volumeHandle);
    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { textureHandle };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});
    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 1u);

    const RenderGraphPassSnapshot& pass = snapshot.passesInExecutionOrder[0];
    EXPECT_EQ(pass.kind, PassKind::Compute);
    EXPECT_FALSE(pass.isCulled);

    ASSERT_EQ(pass.readNames.size(), 3u);
    ASSERT_EQ(pass.readKinds.size(), 3u);
    EXPECT_EQ(pass.readKinds[0], ResourceKind::Texture);
    EXPECT_EQ(pass.readNames[0], "Tex");
    EXPECT_EQ(pass.readKinds[1], ResourceKind::Buffer);
    EXPECT_EQ(pass.readNames[1], "Buf");
    EXPECT_EQ(pass.readKinds[2], ResourceKind::VolumeTexture);
    EXPECT_EQ(pass.readNames[2], "Vol");

    ASSERT_EQ(pass.writeNames.size(), 3u);
    ASSERT_EQ(pass.writeKinds.size(), 3u);
    EXPECT_EQ(pass.writeKinds[0], ResourceKind::Texture);
    EXPECT_EQ(pass.writeNames[0], "Tex");
    EXPECT_EQ(pass.writeKinds[1], ResourceKind::Buffer);
    EXPECT_EQ(pass.writeNames[1], "Buf");
    EXPECT_EQ(pass.writeKinds[2], ResourceKind::VolumeTexture);
    EXPECT_EQ(pass.writeNames[2], "Vol");
}

// Dedicated, minimal regression test for 3.4b's own ResourceUsageName() fix,
// asked for explicitly by the phase document alongside the broader mixed-
// kind test above: a WriteVolumeTexture() (in one pass) and a
// ReadVolumeTexture() (in a separate, later pass) each resolve to the
// volume's real, non-empty name - never the pre-fix "" this exact case used
// to silently produce.
TEST(RenderGraphSnapshotTest, WriteVolumeTextureAndReadVolumeTextureProduceNonEmptyRealNames)
{
    RenderGraphBuilder builder;
    const VolumeTarget volumeTarget{};
    const VolumeTextureHandle volumeHandle =
        builder.ImportVolumeTexture("AtmosphereAerialPerspectiveVolume", volumeTarget, VK_IMAGE_LAYOUT_UNDEFINED);
    const TextureHandle output = builder.CreateTexture("Output", MakeTextureDesc());

    builder.AddComputePass(
        "WriteVolumePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteVolumeTexture(volumeHandle, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);
    builder.AddComputePass(
        "ReadVolumePass",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadVolumeTexture(volumeHandle, ResourceAccess::ComputeShaderRead);
            pass.WriteTexture(output, ResourceAccess::ComputeShaderWrite);
        },
        NoOpExecute);

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { output };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    const RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input, {});
    ASSERT_EQ(snapshot.passesInExecutionOrder.size(), 2u);

    const RenderGraphPassSnapshot& writePass = snapshot.passesInExecutionOrder[0];
    EXPECT_EQ(writePass.name, "WriteVolumePass");
    EXPECT_FALSE(writePass.isCulled);
    ASSERT_EQ(writePass.writeNames.size(), 1u);
    EXPECT_EQ(writePass.writeKinds[0], ResourceKind::VolumeTexture);
    EXPECT_EQ(writePass.writeNames[0], "AtmosphereAerialPerspectiveVolume");
    EXPECT_FALSE(writePass.writeNames[0].empty());

    const RenderGraphPassSnapshot& readPass = snapshot.passesInExecutionOrder[1];
    EXPECT_EQ(readPass.name, "ReadVolumePass");
    ASSERT_EQ(readPass.readNames.size(), 1u);
    EXPECT_EQ(readPass.readKinds[0], ResourceKind::VolumeTexture);
    EXPECT_EQ(readPass.readNames[0], "AtmosphereAerialPerspectiveVolume");
    EXPECT_FALSE(readPass.readNames[0].empty());
}

} // namespace
} // namespace gte::rg
