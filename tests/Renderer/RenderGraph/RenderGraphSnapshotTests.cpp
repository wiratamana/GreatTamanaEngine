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

TEST(RenderGraphSnapshotTest, BufferResourceIsNeverReportedAsImported)
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

} // namespace
} // namespace gte::rg
