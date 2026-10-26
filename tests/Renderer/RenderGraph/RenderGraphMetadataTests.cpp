// Tier 1 tests of the plain RenderGraphMetadata model and BuildRenderGraphMetadata().
// Snapshots are built by hand; no live graph or device is involved.

#include "Renderer/RenderGraph/RenderGraphMetadata.h"
#include <gtest/gtest.h>

namespace gte::rg {
namespace {

RenderGraphPassSnapshot MakeSurvivingPass(const char* name)
{
    RenderGraphPassSnapshot pass;
    pass.name = name;
    pass.isCulled = false;
    return pass;
}

// --- Empty snapshots -----------------------------------------------------

TEST(RenderGraphMetadataTest, EmptySnapshotsProduceEmptyMetadata)
{
    const RenderGraphSnapshot offscreen;
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    EXPECT_EQ(metadata.schemaVersion, 2u);
    EXPECT_EQ(metadata.offscreenRegime.regimeName, "SynchronousImmediateReadback");
    EXPECT_TRUE(metadata.offscreenRegime.passes.empty());
    EXPECT_TRUE(metadata.offscreenRegime.resources.empty());
    EXPECT_EQ(metadata.offscreenRegime.droppedGpuFrames, 0u);

    EXPECT_EQ(metadata.presentRegime.regimeName, "PipelinedDeferredReadback");
    EXPECT_TRUE(metadata.presentRegime.passes.empty());
    EXPECT_TRUE(metadata.presentRegime.resources.empty());
    EXPECT_EQ(metadata.presentRegime.gpuFrameStatus, "Absent");

    EXPECT_TRUE(metadata.renderFeatures.empty());
}

TEST(RenderGraphMetadataTest, PassesCarryNeutralViewLabelsAndHexInstanceIds)
{
    RenderGraphPassSnapshot primary = MakeSurvivingPass("Draw");
    primary.viewScope = ViewScope::Primary;
    primary.stats.gpu.bindingInstance = 0xABCDu;
    RenderGraphPassSnapshot secondary = MakeSurvivingPass("Draw");
    secondary.viewScope = ViewScope::Secondary;

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder = { primary, secondary };
    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, RenderGraphSnapshot{}, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 2u);
    EXPECT_EQ(metadata.offscreenRegime.passes[0].view, "primary");
    EXPECT_EQ(metadata.offscreenRegime.passes[0].instance, "0xabcd");
    EXPECT_EQ(metadata.offscreenRegime.passes[1].view, "secondary");
    EXPECT_EQ(metadata.offscreenRegime.passes[1].instance, "");
}

// --- owningFeatureName resolution ------------------------------------------

TEST(RenderGraphMetadataTest, SurvivingPassOwningFeatureNameIsCopiedThrough)
{
    RenderGraphPassSnapshot pass = MakeSurvivingPass("AtmosphereTransmittanceLutPass");
    pass.owningFeatureName = "Atmosphere / Sky";
    pass.readNames = { "Input" };
    pass.readKinds = { ResourceKind::Texture };
    pass.readAccess = { ResourceAccess::ShaderRead };
    pass.writeNames = { "Output" };
    pass.writeKinds = { ResourceKind::Texture };
    pass.writeAccess = { ResourceAccess::ColorAttachmentWrite };

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];
    EXPECT_EQ(built.owningFeatureName, "Atmosphere / Sky");

    ASSERT_EQ(built.reads.size(), 1u);
    EXPECT_EQ(built.reads[0].name, "Input");
    EXPECT_EQ(built.reads[0].kind, "Texture");
    ASSERT_EQ(built.writes.size(), 1u);
    EXPECT_EQ(built.writes[0].name, "Output");
    EXPECT_EQ(built.writes[0].kind, "Texture");
}

// --- BuildResourceRefs(): bindingStageLabel / barrierTransitionLabel ------
//
// Every existing IPassDebugMetadataProvider test fake elsewhere in this
// suite hardcodes QueryBarrierTransitionLabel() to return false, so the
// "a barrier label WAS found" path through BuildResourceRefs() (this
// file's own production code) is only exercised directly here, against a
// hand-built RenderGraphPassSnapshot whose writeBarrierLabels already
// carries a real, non-empty entry - mirroring exactly what
// RenderGraphSnapshot.cpp's own BuildRenderGraphSnapshot() would have
// written there had a real sink reported one.

TEST(RenderGraphMetadataTest, WriteWithNonEmptyBarrierLabelPopulatesBarrierTransitionLabel)
{

    RenderGraphPassSnapshot pass = MakeSurvivingPass("RenderOpaque");
    pass.readNames = { "Depth" };
    pass.readKinds = { ResourceKind::Texture };
    pass.readAccess = { ResourceAccess::ShaderRead };
    pass.writeNames = { "Color" };
    pass.writeKinds = { ResourceKind::Texture };
    pass.writeAccess = { ResourceAccess::ColorAttachmentWrite };
    pass.writeBarrierLabels = { "Undefined -> RT" };

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];

    ASSERT_EQ(built.reads.size(), 1u);
    EXPECT_EQ(built.reads[0].bindingStageLabel, "Pixel Shader"); // BindingStageLabel(ShaderRead).
    EXPECT_FALSE(built.reads[0].barrierTransitionLabel.has_value()); // reads never carry one.

    ASSERT_EQ(built.writes.size(), 1u);
    EXPECT_EQ(built.writes[0].bindingStageLabel, "Color Attachment"); // BindingStageLabel(ColorAttachmentWrite).
    ASSERT_TRUE(built.writes[0].barrierTransitionLabel.has_value());
    EXPECT_EQ(*built.writes[0].barrierTransitionLabel, "Undefined -> RT");

}

TEST(RenderGraphMetadataTest, WriteWithEmptyBarrierLabelStaysNullopt)
{

    RenderGraphPassSnapshot pass = MakeSurvivingPass("RenderOpaque");
    pass.writeNames = { "Color" };
    pass.writeKinds = { ResourceKind::Texture };
    pass.writeAccess = { ResourceAccess::ColorAttachmentWrite };
    pass.writeBarrierLabels = { "" }; // no barrier applied this frame for this write.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];

    ASSERT_EQ(built.writes.size(), 1u);
    EXPECT_FALSE(built.writes[0].barrierTransitionLabel.has_value());

}

TEST(RenderGraphMetadataTest, PassWithNoOwningFeatureNameStaysEmptyString)
{
    RenderGraphPassSnapshot pass = MakeSurvivingPass("RenderOpaque");
    // owningFeatureName never set - the struct's own default (empty string).

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    EXPECT_TRUE(metadata.offscreenRegime.passes[0].owningFeatureName.empty());
}

TEST(RenderGraphMetadataTest, PassWithEngineUnownedSentinelIsCopiedThroughVerbatim)
{
    RenderGraphPassSnapshot pass = MakeSurvivingPass("GpuSkinning");
    pass.owningFeatureName = "ENGINE_UNOWNED"; // declared with no active RenderFeatureScope.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    EXPECT_EQ(metadata.offscreenRegime.passes[0].owningFeatureName, "ENGINE_UNOWNED");
}

// --- Culled pass: still fully describable, stats/timing at their defaults --

TEST(RenderGraphMetadataTest, CulledPassIsStillFullyDescribedWithZeroedStatsAndNATiming)
{

    RenderGraphPassSnapshot pass;
    pass.name = "UnusedPass";
    pass.isCulled = true;
    pass.kind = PassKind::Compute;
    pass.category = RenderPassCategory::Debug;
    pass.drawKind = RenderPassDrawKind::Blit;
    pass.viewScope = ViewScope::Secondary;
    pass.renderPassEvent = RenderPassEvent::AfterTransparents;
    pass.readNames = { "SomeInput" };
    pass.readKinds = { ResourceKind::Buffer };
    pass.readAccess = { ResourceAccess::ShaderRead };
    pass.writeNames = { "DeadEnd" };
    pass.writeKinds = { ResourceKind::VolumeTexture };
    pass.writeAccess = { ResourceAccess::ComputeShaderWrite };
    // pass.stats stays at its default (empty DrawStats, Absent GPU record), as for any culled pass.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];

    EXPECT_EQ(built.name, "UnusedPass");
    EXPECT_TRUE(built.isCulled);
    EXPECT_EQ(built.kind, "Compute");
    EXPECT_EQ(built.category, "Debug");
    EXPECT_EQ(built.drawKind, "Blit");
    EXPECT_EQ(built.viewScope, "Secondary");
    EXPECT_EQ(built.renderPassEvent, "AfterTransparents");

    ASSERT_EQ(built.reads.size(), 1u);
    EXPECT_EQ(built.reads[0].name, "SomeInput");
    EXPECT_EQ(built.reads[0].kind, "Buffer");
    ASSERT_EQ(built.writes.size(), 1u);
    EXPECT_EQ(built.writes[0].name, "DeadEnd");
    EXPECT_EQ(built.writes[0].kind, "VolumeTexture");

    EXPECT_EQ(built.drawCallCount, 0u);
    EXPECT_EQ(built.triangleCount, 0u);
    EXPECT_EQ(built.gpuTimingStatus, "Absent");
    EXPECT_FALSE(built.gpuTimingMilliseconds.has_value());
}

// --- Resource first/last-use pass NAME resolution --------------------------

TEST(RenderGraphMetadataTest, ResourceWithValidUseIndicesResolvesBothPassNames)
{

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(MakeSurvivingPass("WritePass"));
    offscreen.passesInExecutionOrder.push_back(MakeSurvivingPass("ReadPass"));

    RenderGraphResourceSnapshot resource;
    resource.name = "Scratch";
    resource.isImported = false;
    resource.firstUsePassIndex = 0;
    resource.lastUsePassIndex = 1;
    offscreen.resources.push_back(resource);

    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.resources.size(), 1u);
    const RenderGraphResourceMetadata& built = metadata.offscreenRegime.resources[0];
    EXPECT_EQ(built.name, "Scratch");
    EXPECT_FALSE(built.isImported);
    EXPECT_EQ(built.firstUsePassIndex, 0);
    EXPECT_EQ(built.lastUsePassIndex, 1);
    ASSERT_TRUE(built.firstUsePassName.has_value());
    EXPECT_EQ(*built.firstUsePassName, "WritePass");
    ASSERT_TRUE(built.lastUsePassName.has_value());
    EXPECT_EQ(*built.lastUsePassName, "ReadPass");
}

TEST(RenderGraphMetadataTest, NeverUsedResourceHasNulloptPassNamesForNegativeIndices)
{

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(MakeSurvivingPass("SomePass"));

    RenderGraphResourceSnapshot resource;
    resource.name = "NeverUsed";
    resource.isImported = true;
    resource.firstUsePassIndex = -1;
    resource.lastUsePassIndex = -1;
    offscreen.resources.push_back(resource);

    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.resources.size(), 1u);
    const RenderGraphResourceMetadata& built = metadata.offscreenRegime.resources[0];
    EXPECT_EQ(built.name, "NeverUsed");
    EXPECT_TRUE(built.isImported);
    EXPECT_EQ(built.firstUsePassIndex, -1);
    EXPECT_EQ(built.lastUsePassIndex, -1);
    EXPECT_FALSE(built.firstUsePassName.has_value());
    EXPECT_FALSE(built.lastUsePassName.has_value());
}

// --- A measured pass carries a real numeric value too ------------------------

TEST(RenderGraphMetadataTest, PresentGpuTimingProducesMatchingTextAndNumericMilliseconds)
{
    RenderGraphPassSnapshot pass = MakeSurvivingPass("TimedPass");
    pass.stats.gpu.state = GpuReadState::Present;
    pass.stats.gpu.barrierMs = 0.5f;
    pass.stats.gpu.workMs = 3.0f;
    pass.stats.drawStats.drawCallCount = 7;
    pass.stats.drawStats.triangleCount = 42;

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];
    EXPECT_EQ(built.drawCallCount, 7u);
    EXPECT_EQ(built.triangleCount, 42u);
    EXPECT_EQ(built.gpuTimingStatus, "Present");
    EXPECT_DOUBLE_EQ(built.gpuRecordedMilliseconds, 3.5);
    ASSERT_TRUE(built.gpuTimingMilliseconds.has_value());
    EXPECT_DOUBLE_EQ(*built.gpuTimingMilliseconds, 3.5);
}

// --- Frame-level GPU timing passes straight through -------------------------

TEST(RenderGraphMetadataTest, GpuFrameFieldsAreCopiedThroughPerRegime)
{
    RenderGraphSnapshot offscreen;
    offscreen.gpuReport.state = GpuReadState::Partial;
    offscreen.gpuReport.spanValid = true;
    offscreen.gpuReport.spanMs = 4.5;
    offscreen.gpuReport.foldedPasses = 3;
    offscreen.droppedGpuFrames = 2;
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    EXPECT_EQ(metadata.offscreenRegime.gpuFrameStatus, "Partial");
    ASSERT_TRUE(metadata.offscreenRegime.gpuSpanMilliseconds.has_value());
    EXPECT_DOUBLE_EQ(*metadata.offscreenRegime.gpuSpanMilliseconds, 4.5);
    EXPECT_EQ(metadata.offscreenRegime.gpuFoldedPasses, 3u);
    EXPECT_EQ(metadata.offscreenRegime.droppedGpuFrames, 2u);
    EXPECT_EQ(metadata.presentRegime.gpuFrameStatus, "Absent");
    EXPECT_FALSE(metadata.presentRegime.gpuSpanMilliseconds.has_value());
}

TEST(RenderGraphMetadataTest, UnmeasuredPassCarriesItsStatusWordAndNoNumber)
{
    RenderGraphPassSnapshot pass = MakeSurvivingPass("Idle");
    pass.stats.gpu.state = GpuReadState::Inactive;

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    EXPECT_EQ(metadata.offscreenRegime.passes[0].gpuTimingStatus, "Inactive");
    EXPECT_FALSE(metadata.offscreenRegime.passes[0].gpuTimingMilliseconds.has_value());
}

// --- RenderFeatureDebugEntry is reused directly -------------------------------

TEST(RenderGraphMetadataTest, RenderFeaturesAreCopiedThroughUnchanged)
{
    RenderFeatureDebugEntry feature;
    feature.name = "Bloom";
    feature.stage = "PostComposite";
    feature.priority = 5;
    feature.blendMode = "AlphaOver";
    feature.enabled = false;

    const RenderGraphSnapshot offscreen;
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, { feature });

    ASSERT_EQ(metadata.renderFeatures.size(), 1u);
    EXPECT_EQ(metadata.renderFeatures[0].name, "Bloom");
    EXPECT_EQ(metadata.renderFeatures[0].stage, "PostComposite");
    EXPECT_EQ(metadata.renderFeatures[0].priority, 5);
    EXPECT_EQ(metadata.renderFeatures[0].blendMode, "AlphaOver");
    EXPECT_FALSE(metadata.renderFeatures[0].enabled);
}

// --- gpu_memory counts ---

TEST(RenderGraphGpuMemoryMetadataTest, SumsBytesPerNameAndGroupsUnnamedAllocations)
{
    const std::vector<GpuAllocationSample> samples{
        { "MeshClusterTable", 64 }, { "", 10 }, { "MeshClusterTable", 128 }, { "Scene.Color", 1000 }, { "", 5 } };

    const RenderGraphGpuMemoryMetadata memory = BuildGpuMemoryMetadata(samples, 3);

    EXPECT_EQ(memory.totalBytes, 1207u);
    EXPECT_EQ(memory.meshCount, 3u);
    ASSERT_EQ(memory.byName.size(), 3u);
    EXPECT_EQ(memory.byName.at("MeshClusterTable"), 192u);
    EXPECT_EQ(memory.byName.at("Scene.Color"), 1000u);
    EXPECT_EQ(memory.byName.at(kUnnamedGpuAllocation), 15u);
}

TEST(RenderGraphGpuMemoryMetadataTest, TotalsDoNotOverflowThirtyTwoBits)
{
    const std::vector<GpuAllocationSample> samples{ { "A", 3'000'000'000ull }, { "A", 3'000'000'000ull } };
    EXPECT_EQ(BuildGpuMemoryMetadata(samples, 0).totalBytes, 6'000'000'000ull);
}

} // namespace
} // namespace gte::rg
