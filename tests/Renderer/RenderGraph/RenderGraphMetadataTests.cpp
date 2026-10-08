// Unit tests for editor-core-separation-7 campaign's PHASE2
// (task_manager/editor-core-separation-7/PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md)
// - the new, pure, engine-free RenderGraphMetadata data model + BuildRenderGraphMetadata()
// + its to_json() JSON shape. Entirely Tier 1 - no live RenderGraph/VkDevice involved
// anywhere in this file. Mirrors RenderGraphSnapshotTests.cpp's own hand-fabrication
// style, but goes one step further: since BuildRenderGraphMetadata() itself takes an
// already-built RenderGraphSnapshot (never a CompiledGraph/CompiledGraphInput), every
// fixture below constructs RenderGraphSnapshot/RenderGraphPassSnapshot/
// RenderGraphResourceSnapshot values DIRECTLY, by hand - no RenderGraphBuilder, no
// RenderGraphCompiler::Compile() call anywhere in this file.

#include "Renderer/RenderGraph/RenderGraphMetadata.h"

#include "Renderer/RenderGraph/RenderPassGroupRegistry.h"

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
    ResetPassGroupRegistryForTesting();

    const RenderGraphSnapshot offscreen;
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    EXPECT_EQ(metadata.schemaVersion, 1u);
    EXPECT_EQ(metadata.offscreenRegime.regimeName, "SynchronousImmediateReadback");
    EXPECT_TRUE(metadata.offscreenRegime.passes.empty());
    EXPECT_TRUE(metadata.offscreenRegime.resources.empty());
    EXPECT_FALSE(metadata.offscreenRegime.timingSlotBudgetExhausted);

    EXPECT_EQ(metadata.presentRegime.regimeName, "PipelinedDeferredReadback");
    EXPECT_TRUE(metadata.presentRegime.passes.empty());
    EXPECT_TRUE(metadata.presentRegime.resources.empty());
    EXPECT_FALSE(metadata.presentRegime.timingSlotBudgetExhausted);

    EXPECT_TRUE(metadata.gpuDrivenBatches.empty());
    EXPECT_TRUE(metadata.renderFeatures.empty());
}

// --- tagGroupLabel resolution ---------------------------------------------

TEST(RenderGraphMetadataTest, SurvivingPassWithRegisteredTagResolvesTagGroupLabel)
{
    ResetPassGroupRegistryForTesting();
    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");

    RenderGraphPassSnapshot pass = MakeSurvivingPass("AtmosphereTransmittanceLutPass");
    pass.tags = 0x1;
    pass.readNames = { "Input" };
    pass.readKinds = { ResourceKind::Texture };
    pass.readAccess = { ResourceAccess::ShaderRead };
    pass.writeNames = { "Output" };
    pass.writeKinds = { ResourceKind::Texture };
    pass.writeAccess = { ResourceAccess::ColorAttachmentWrite };

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];
    ASSERT_TRUE(built.tagGroupLabel.has_value());
    EXPECT_EQ(*built.tagGroupLabel, "Compute LUT");

    ASSERT_EQ(built.reads.size(), 1u);
    EXPECT_EQ(built.reads[0].name, "Input");
    EXPECT_EQ(built.reads[0].kind, "Texture");
    ASSERT_EQ(built.writes.size(), 1u);
    EXPECT_EQ(built.writes[0].name, "Output");
    EXPECT_EQ(built.writes[0].kind, "Texture");

    ResetPassGroupRegistryForTesting();
}

TEST(RenderGraphMetadataTest, PassWithNoRegisteredTagHasNulloptTagGroupLabel)
{
    ResetPassGroupRegistryForTesting();

    RenderGraphPassSnapshot pass = MakeSurvivingPass("RenderOpaque");
    pass.tags = 0; // no tags at all - the common case today.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    EXPECT_FALSE(metadata.offscreenRegime.passes[0].tagGroupLabel.has_value());
}

TEST(RenderGraphMetadataTest, PassWithUnregisteredTagBitHasNulloptTagGroupLabel)
{
    ResetPassGroupRegistryForTesting();
    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");

    RenderGraphPassSnapshot pass = MakeSurvivingPass("GpuSkinning");
    pass.tags = 0x2; // a real bit, but nobody registered a label for it.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    EXPECT_FALSE(metadata.offscreenRegime.passes[0].tagGroupLabel.has_value());

    ResetPassGroupRegistryForTesting();
}

// --- Culled pass: still fully describable, stats/timing at their defaults --

TEST(RenderGraphMetadataTest, CulledPassIsStillFullyDescribedWithZeroedStatsAndNATiming)
{
    ResetPassGroupRegistryForTesting();

    RenderGraphPassSnapshot pass;
    pass.name = "UnusedPass";
    pass.isCulled = true;
    pass.kind = PassKind::Compute;
    pass.category = RenderPassCategory::Debug;
    pass.drawKind = RenderPassDrawKind::Blit;
    pass.viewScope = ViewScope::SceneView;
    pass.renderPassEvent = RenderPassEvent::AfterTransparents;
    pass.readNames = { "SomeInput" };
    pass.readKinds = { ResourceKind::Buffer };
    pass.readAccess = { ResourceAccess::ShaderRead };
    pass.writeNames = { "DeadEnd" };
    pass.writeKinds = { ResourceKind::VolumeTexture };
    pass.writeAccess = { ResourceAccess::ComputeShaderWrite };
    // pass.stats left at its default (empty DrawStats, Absent GpuTimingSample)
    // - exactly what BuildRenderGraphSnapshot() itself always leaves a culled
    // pass with.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];

    EXPECT_EQ(built.name, "UnusedPass");
    EXPECT_TRUE(built.isCulled);
    EXPECT_EQ(built.kind, "Compute");
    EXPECT_EQ(built.category, "Debug");
    EXPECT_EQ(built.drawKind, "Blit");
    EXPECT_EQ(built.viewScope, "SceneView");
    EXPECT_EQ(built.renderPassEvent, "AfterTransparents");

    ASSERT_EQ(built.reads.size(), 1u);
    EXPECT_EQ(built.reads[0].name, "SomeInput");
    EXPECT_EQ(built.reads[0].kind, "Buffer");
    ASSERT_EQ(built.writes.size(), 1u);
    EXPECT_EQ(built.writes[0].name, "DeadEnd");
    EXPECT_EQ(built.writes[0].kind, "VolumeTexture");

    EXPECT_EQ(built.drawCallCount, 0u);
    EXPECT_EQ(built.triangleCount, 0u);
    EXPECT_EQ(built.gpuTimingText, "N/A");
    EXPECT_FALSE(built.gpuTimingMilliseconds.has_value());
}

// --- Resource first/last-use pass NAME resolution --------------------------

TEST(RenderGraphMetadataTest, ResourceWithValidUseIndicesResolvesBothPassNames)
{
    ResetPassGroupRegistryForTesting();

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

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

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
    ResetPassGroupRegistryForTesting();

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(MakeSurvivingPass("SomePass"));

    RenderGraphResourceSnapshot resource;
    resource.name = "NeverUsed";
    resource.isImported = true;
    resource.firstUsePassIndex = -1;
    resource.lastUsePassIndex = -1;
    offscreen.resources.push_back(resource);

    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    ASSERT_EQ(metadata.offscreenRegime.resources.size(), 1u);
    const RenderGraphResourceMetadata& built = metadata.offscreenRegime.resources[0];
    EXPECT_EQ(built.name, "NeverUsed");
    EXPECT_TRUE(built.isImported);
    EXPECT_EQ(built.firstUsePassIndex, -1);
    EXPECT_EQ(built.lastUsePassIndex, -1);
    EXPECT_FALSE(built.firstUsePassName.has_value());
    EXPECT_FALSE(built.lastUsePassName.has_value());
}

// --- GpuTimingSample::Status::Present carries a real numeric value too -----

TEST(RenderGraphMetadataTest, PresentGpuTimingProducesMatchingTextAndNumericMilliseconds)
{
    ResetPassGroupRegistryForTesting();

    RenderGraphPassSnapshot pass = MakeSurvivingPass("TimedPass");
    pass.stats.timing.status = GpuTimingSample::Status::Present;
    pass.stats.timing.milliseconds = 3.14;
    pass.stats.drawStats.drawCallCount = 7;
    pass.stats.drawStats.triangleCount = 42;

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    ASSERT_EQ(metadata.offscreenRegime.passes.size(), 1u);
    const RenderGraphPassMetadata& built = metadata.offscreenRegime.passes[0];
    EXPECT_EQ(built.drawCallCount, 7u);
    EXPECT_EQ(built.triangleCount, 42u);
    EXPECT_EQ(built.gpuTimingText, "3.14 ms");
    ASSERT_TRUE(built.gpuTimingMilliseconds.has_value());
    EXPECT_DOUBLE_EQ(*built.gpuTimingMilliseconds, 3.14);
}

// --- timingSlotBudgetExhausted passes straight through ----------------------

TEST(RenderGraphMetadataTest, TimingSlotBudgetExhaustedIsCopiedThroughPerRegime)
{
    ResetPassGroupRegistryForTesting();

    RenderGraphSnapshot offscreen;
    offscreen.timingSlotBudgetExhausted = true;
    RenderGraphSnapshot present;
    present.timingSlotBudgetExhausted = false;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, {}, {});

    EXPECT_TRUE(metadata.offscreenRegime.timingSlotBudgetExhausted);
    EXPECT_FALSE(metadata.presentRegime.timingSlotBudgetExhausted);
}

// --- GpuDrivenBatchDebugInfo / RenderFeatureDebugEntry are reused directly --

TEST(RenderGraphMetadataTest, GpuDrivenBatchesAndRenderFeaturesAreCopiedThroughUnchanged)
{
    ResetPassGroupRegistryForTesting();

    GpuDrivenBatchDebugInfo batch;
    batch.batchName = "GpuDrivenBatch0";
    batch.instanceCount = 10;
    batch.visibleCount = 6u;

    RenderFeatureDebugEntry feature;
    feature.name = "Bloom";
    feature.stage = "PostComposite";
    feature.priority = 5;
    feature.blendMode = "AlphaOver";
    // editor-core-separation-8 campaign, PHASE2 - the new host-side
    // enable/disable override field, defaults true.
    feature.enabled = false;

    const RenderGraphSnapshot offscreen;
    const RenderGraphSnapshot present;

    const RenderGraphMetadata metadata =
        BuildRenderGraphMetadata(offscreen, present, { batch }, { feature });

    ASSERT_EQ(metadata.gpuDrivenBatches.size(), 1u);
    EXPECT_EQ(metadata.gpuDrivenBatches[0].batchName, "GpuDrivenBatch0");
    EXPECT_EQ(metadata.gpuDrivenBatches[0].instanceCount, 10u);
    ASSERT_TRUE(metadata.gpuDrivenBatches[0].visibleCount.has_value());
    EXPECT_EQ(*metadata.gpuDrivenBatches[0].visibleCount, 6u);

    ASSERT_EQ(metadata.renderFeatures.size(), 1u);
    EXPECT_EQ(metadata.renderFeatures[0].name, "Bloom");
    EXPECT_EQ(metadata.renderFeatures[0].stage, "PostComposite");
    EXPECT_EQ(metadata.renderFeatures[0].priority, 5);
    EXPECT_EQ(metadata.renderFeatures[0].blendMode, "AlphaOver");
    EXPECT_FALSE(metadata.renderFeatures[0].enabled);
}

// --- to_json() round-trip: the actual, real, external JSON contract --------

TEST(RenderGraphMetadataTest, ToJsonProducesExpectedTopLevelShapeAndNullHandling)
{
    ResetPassGroupRegistryForTesting();

    RenderGraphPassSnapshot pass = MakeSurvivingPass("RenderOpaque");
    pass.readNames = { "Depth" };
    pass.readKinds = { ResourceKind::Texture };
    pass.readAccess = { ResourceAccess::ShaderRead };
    pass.writeNames = { "Color" };
    pass.writeKinds = { ResourceKind::Texture };
    pass.writeAccess = { ResourceAccess::ColorAttachmentWrite };
    pass.stats.drawStats.drawCallCount = 2;
    pass.stats.drawStats.triangleCount = 20;
    // tags == 0, no registered label -> tagGroupLabel stays nullopt -> JSON null.
    // stats.timing stays at its default (Absent) -> gpu_timing_milliseconds JSON null.

    RenderGraphSnapshot offscreen;
    offscreen.passesInExecutionOrder.push_back(pass);
    const RenderGraphSnapshot present;

    GpuDrivenBatchDebugInfo batch;
    batch.batchName = "Batch0";
    batch.instanceCount = 4;
    // visibleCount left at std::nullopt -> JSON null.

    RenderFeatureDebugEntry feature;
    feature.name = "Vignette";
    feature.stage = "PreUI";
    feature.priority = 1;
    feature.blendMode = "Replace";
    // editor-core-separation-8 campaign, PHASE2 - explicit true here (the
    // struct's own default), so this test also proves the "enabled" key
    // round-trips through to_json() for the true case too (the sibling test
    // above already covers the false case).
    feature.enabled = true;

    const RenderGraphMetadata metadata = BuildRenderGraphMetadata(offscreen, present, { batch }, { feature });

    nlohmann::json j = metadata;

    EXPECT_EQ(j["schema_version"].get<std::uint32_t>(), 1u);

    ASSERT_TRUE(j.contains("offscreen_regime"));
    EXPECT_EQ(j["offscreen_regime"]["regime_name"].get<std::string>(), "SynchronousImmediateReadback");
    ASSERT_EQ(j["offscreen_regime"]["passes"].size(), 1u);
    const nlohmann::json& jsonPass = j["offscreen_regime"]["passes"][0];
    EXPECT_EQ(jsonPass["name"].get<std::string>(), "RenderOpaque");
    EXPECT_FALSE(jsonPass["is_culled"].get<bool>());
    EXPECT_TRUE(jsonPass["tag_group_label"].is_null());
    EXPECT_TRUE(jsonPass["gpu_timing_milliseconds"].is_null());
    EXPECT_EQ(jsonPass["gpu_timing_text"].get<std::string>(), "N/A");
    EXPECT_EQ(jsonPass["draw_call_count"].get<std::uint32_t>(), 2u);
    EXPECT_EQ(jsonPass["triangle_count"].get<std::uint32_t>(), 20u);
    ASSERT_EQ(jsonPass["reads"].size(), 1u);
    EXPECT_EQ(jsonPass["reads"][0]["name"].get<std::string>(), "Depth");
    EXPECT_EQ(jsonPass["reads"][0]["kind"].get<std::string>(), "Texture");
    ASSERT_EQ(jsonPass["writes"].size(), 1u);
    EXPECT_EQ(jsonPass["writes"][0]["name"].get<std::string>(), "Color");

    EXPECT_EQ(j["present_regime"]["regime_name"].get<std::string>(), "PipelinedDeferredReadback");
    EXPECT_TRUE(j["present_regime"]["passes"].empty());

    ASSERT_EQ(j["gpu_driven_batches"].size(), 1u);
    EXPECT_EQ(j["gpu_driven_batches"][0]["batch_name"].get<std::string>(), "Batch0");
    EXPECT_EQ(j["gpu_driven_batches"][0]["instance_count"].get<std::uint32_t>(), 4u);
    EXPECT_TRUE(j["gpu_driven_batches"][0]["visible_count"].is_null());

    ASSERT_EQ(j["render_features"].size(), 1u);
    EXPECT_EQ(j["render_features"][0]["name"].get<std::string>(), "Vignette");
    EXPECT_EQ(j["render_features"][0]["stage"].get<std::string>(), "PreUI");
    EXPECT_EQ(j["render_features"][0]["priority"].get<std::int32_t>(), 1);
    EXPECT_EQ(j["render_features"][0]["blend_mode"].get<std::string>(), "Replace");
    EXPECT_TRUE(j["render_features"][0]["enabled"].get<bool>());
}

// --- editor-core-separation-22 campaign, PHASE5
// (PHASE5_UNIFY_DUPLICATE_PASS_ROWS_RENDER_GRAPH_PANEL.md) - GroupPassMetadataByName()
// coverage. Unlike every fixture above (which hand-builds a RenderGraphSnapshot
// and calls BuildRenderGraphMetadata()), GroupPassMetadataByName() itself takes
// an already-built std::vector<RenderGraphPassMetadata> directly - so these
// fixtures construct RenderGraphPassMetadata values BY HAND, exactly like the
// grouping function's own real caller (RenderGraphPanel.cpp's BuildPassTable())
// does with a RenderGraphRegimeMetadata::passes vector. ------------------------

RenderGraphPassMetadata MakeGroupInputPass(const std::string& name, const std::string& viewScope, bool isCulled = false)
{
    RenderGraphPassMetadata pass;
    pass.name = name;
    pass.viewScope = viewScope;
    pass.isCulled = isCulled;
    pass.gpuTimingText = "N/A";
    return pass;
}

TEST(RenderGraphGroupedPassMetadataTest, EmptyInputProducesEmptyResult)
{
    const std::vector<RenderGraphPassMetadata> ungrouped;
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);
    EXPECT_TRUE(grouped.empty());
}

// (a) two same-named instances (Game+Scene), both non-culled, group into
// one row with summed stats and a "both views" label.
TEST(RenderGraphGroupedPassMetadataTest, TwoNonCulledInstancesAcrossGameAndSceneGroupIntoOneRowWithSummedStatsAndBothViewsLabel)
{
    RenderGraphPassMetadata gameInstance = MakeGroupInputPass("RenderOpaque", "GameView");
    gameInstance.drawCallCount = 10;
    gameInstance.triangleCount = 100;
    gameInstance.gpuTimingText = "0.12 ms";

    RenderGraphPassMetadata sceneInstance = MakeGroupInputPass("RenderOpaque", "SceneView");
    sceneInstance.drawCallCount = 5;
    sceneInstance.triangleCount = 50;
    sceneInstance.gpuTimingText = "0.08 ms";

    const std::vector<RenderGraphPassMetadata> ungrouped{ gameInstance, sceneInstance };
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);

    ASSERT_EQ(grouped.size(), 1u);
    const RenderGraphGroupedPassMetadata& group = grouped[0];
    EXPECT_EQ(group.name, "RenderOpaque");
    EXPECT_FALSE(group.isCulled);
    EXPECT_EQ(group.viewLabel, "Game+Scene");
    EXPECT_EQ(group.drawCallCount, 15u);
    EXPECT_EQ(group.triangleCount, 150u);
    // Locked decision (via ask_questions, see PHASE5_COMPLETION_REPORT.md) -
    // per-view breakdown, never summed/maxed.
    EXPECT_EQ(group.gpuTimingText, "Game: 0.12 ms, Scene: 0.08 ms");
    ASSERT_EQ(group.instances.size(), 2u);
}

// (b) one instance culled, the other not, produces isCulled == false for the
// GROUP with a label reflecting only the surviving view.
TEST(RenderGraphGroupedPassMetadataTest, OneCulledOneSurvivingInstanceGroupsToNonCulledWithSurvivingViewLabel)
{
    RenderGraphPassMetadata gameInstance = MakeGroupInputPass("AtmosphereSkyViewLutPass", "GameView", /*isCulled=*/true);
    RenderGraphPassMetadata sceneInstance = MakeGroupInputPass("AtmosphereSkyViewLutPass", "SceneView", /*isCulled=*/false);
    sceneInstance.drawCallCount = 3;

    const std::vector<RenderGraphPassMetadata> ungrouped{ gameInstance, sceneInstance };
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);

    ASSERT_EQ(grouped.size(), 1u);
    const RenderGraphGroupedPassMetadata& group = grouped[0];
    EXPECT_FALSE(group.isCulled); // a pass surviving in ONE view must never read as fully culled.
    EXPECT_EQ(group.viewLabel, "Scene only"); // reflects ONLY the surviving contributor - the culled Game View instance is not counted here.
    EXPECT_EQ(group.drawCallCount, 3u); // the culled instance contributes 0, by construction.
}

// (c) both instances culled groups to isCulled == true, with the viewLabel
// fallback listing every instance (there is no "real" contribution to
// prefer once the whole group is culled).
TEST(RenderGraphGroupedPassMetadataTest, BothInstancesCulledGroupsToCulledTrue)
{
    RenderGraphPassMetadata gameInstance = MakeGroupInputPass("UnusedFeaturePass", "GameView", /*isCulled=*/true);
    RenderGraphPassMetadata sceneInstance = MakeGroupInputPass("UnusedFeaturePass", "SceneView", /*isCulled=*/true);

    const std::vector<RenderGraphPassMetadata> ungrouped{ gameInstance, sceneInstance };
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);

    ASSERT_EQ(grouped.size(), 1u);
    const RenderGraphGroupedPassMetadata& group = grouped[0];
    EXPECT_TRUE(group.isCulled);
    EXPECT_EQ(group.viewLabel, "Game+Scene"); // fallback: every instance's own viewScope, since none "really" contributed.
    EXPECT_EQ(group.drawCallCount, 0u);
}

// (d) a name appearing in only one view groups correctly with a
// single-view label, and its gpuTimingText is the one instance's own text
// verbatim (no redundant view-name prefix).
TEST(RenderGraphGroupedPassMetadataTest, SingleViewInstanceGroupsWithSingleViewLabelAndVerbatimGpuTiming)
{
    RenderGraphPassMetadata gameOnlyInstance = MakeGroupInputPass("DemoRenderFeaturePlugin_Clear", "GameView");
    gameOnlyInstance.gpuTimingText = "0.05 ms";

    const std::vector<RenderGraphPassMetadata> ungrouped{ gameOnlyInstance };
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);

    ASSERT_EQ(grouped.size(), 1u);
    const RenderGraphGroupedPassMetadata& group = grouped[0];
    EXPECT_EQ(group.viewLabel, "Game only");
    ASSERT_EQ(group.instances.size(), 1u);
    EXPECT_EQ(group.gpuTimingText, "0.05 ms"); // verbatim - no "Game: " prefix for a single-instance group.
}

// A lone Shared-scope instance (ViewScope::Shared - "not duplicated per
// view") gets a bare "Shared" label, never "Shared only" - Shared is
// already inherently singular.
TEST(RenderGraphGroupedPassMetadataTest, SharedViewScopeSingleInstanceLabelHasNoOnlySuffix)
{
    const std::vector<RenderGraphPassMetadata> ungrouped{ MakeGroupInputPass("GpuSkinning", "Shared") };
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);

    ASSERT_EQ(grouped.size(), 1u);
    EXPECT_EQ(grouped[0].viewLabel, "Shared");
}

// (e) grouping is stable/deterministic regardless of input order - mirrors
// RenderPassToggleRegistry::ListAll()'s own "sorted, deterministic
// iteration order" discipline.
TEST(RenderGraphGroupedPassMetadataTest, GroupingIsStableAndDeterministicRegardlessOfInputOrder)
{
    RenderGraphPassMetadata alphaGame = MakeGroupInputPass("Alpha", "GameView");
    alphaGame.drawCallCount = 1;
    RenderGraphPassMetadata alphaScene = MakeGroupInputPass("Alpha", "SceneView");
    alphaScene.drawCallCount = 2;
    RenderGraphPassMetadata betaGame = MakeGroupInputPass("Beta", "GameView");
    betaGame.drawCallCount = 4;
    RenderGraphPassMetadata betaScene = MakeGroupInputPass("Beta", "SceneView");
    betaScene.drawCallCount = 8;

    const std::vector<RenderGraphPassMetadata> orderOne{ betaGame, alphaScene, alphaGame, betaScene };
    const std::vector<RenderGraphPassMetadata> orderTwo{ alphaGame, betaScene, alphaScene, betaGame };

    const std::vector<RenderGraphGroupedPassMetadata> groupedOne = GroupPassMetadataByName(orderOne);
    const std::vector<RenderGraphGroupedPassMetadata> groupedTwo = GroupPassMetadataByName(orderTwo);

    ASSERT_EQ(groupedOne.size(), 2u);
    ASSERT_EQ(groupedTwo.size(), 2u);
    // Sorted-by-name order ("Alpha" before "Beta"), regardless of either
    // input vector's own insertion order.
    EXPECT_EQ(groupedOne[0].name, "Alpha");
    EXPECT_EQ(groupedOne[1].name, "Beta");
    EXPECT_EQ(groupedTwo[0].name, "Alpha");
    EXPECT_EQ(groupedTwo[1].name, "Beta");

    EXPECT_EQ(groupedOne[0].drawCallCount, groupedTwo[0].drawCallCount);
    EXPECT_EQ(groupedOne[1].drawCallCount, groupedTwo[1].drawCallCount);
    EXPECT_EQ(groupedOne[0].viewLabel, groupedTwo[0].viewLabel);
    EXPECT_EQ(groupedOne[1].viewLabel, groupedTwo[1].viewLabel);
}

// Reads/writes are combined and de-duplicated (order-preserving, first-seen)
// across every instance sharing a group's own name.
TEST(RenderGraphGroupedPassMetadataTest, ReadsAndWritesAreCombinedAndDeduplicatedAcrossInstances)
{
    RenderGraphPassMetadata gameInstance = MakeGroupInputPass("RenderOpaque", "GameView");
    gameInstance.reads = { RenderGraphResourceRefMetadata{ "Depth", "Texture" },
        RenderGraphResourceRefMetadata{ "Shared", "Buffer" } };
    gameInstance.writes = { RenderGraphResourceRefMetadata{ "ColorGame", "Texture" } };

    RenderGraphPassMetadata sceneInstance = MakeGroupInputPass("RenderOpaque", "SceneView");
    sceneInstance.reads = { RenderGraphResourceRefMetadata{ "Shared", "Buffer" },
        RenderGraphResourceRefMetadata{ "DepthScene", "Texture" } };
    sceneInstance.writes = { RenderGraphResourceRefMetadata{ "ColorScene", "Texture" } };

    const std::vector<RenderGraphPassMetadata> ungrouped{ gameInstance, sceneInstance };
    const std::vector<RenderGraphGroupedPassMetadata> grouped = GroupPassMetadataByName(ungrouped);

    ASSERT_EQ(grouped.size(), 1u);
    const RenderGraphGroupedPassMetadata& group = grouped[0];
    ASSERT_EQ(group.reads.size(), 3u); // "Shared" appears in both instances - de-duplicated to one entry.
    EXPECT_EQ(group.reads[0], "Depth");
    EXPECT_EQ(group.reads[1], "Shared");
    EXPECT_EQ(group.reads[2], "DepthScene");
    ASSERT_EQ(group.writes.size(), 2u);
    EXPECT_EQ(group.writes[0], "ColorGame");
    EXPECT_EQ(group.writes[1], "ColorScene");
}

} // namespace
} // namespace gte::rg
