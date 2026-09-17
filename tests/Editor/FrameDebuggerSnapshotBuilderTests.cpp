// Unit tests for the Frame Debugger's real snapshot builder
// (src/Editor/FrameDebuggerData.h's BuildRealFrameDebuggerSnapshot()) - pure,
// ImGui-free, no-live-VkDevice-needed reshape of a hand-fabricated
// gte::rg::RenderGraphSnapshot + FrameDebuggerCaptureContext, mirroring
// tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp's own "hand-built
// snapshot in, real result out" precedent. Only built when GTE_ENABLE_EDITOR
// is ON, since FrameDebuggerData.h/.cpp are only compiled into gte_core then
// (see the root CMakeLists.txt's "Editor Module Structure") - the same
// "zero-touch when off" rule already applied to
// tests/Editor/FrameDebuggerDataTests.cpp/FrameDebuggerCaptureTests.cpp.
//
// task_manager/frame-debugger-3 campaign, PHASE2
// (PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md).
//
// frame-debugger-5 campaign, PHASE2
// (PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md) - every test
// below that used to feed BuildRealFrameDebuggerSnapshot() a caller-supplied
// `gpuSkinningPassNamesThisFrame` name list (REMOVED - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #2/#6) was rewritten to
// instead mark the relevant RenderGraphPassSnapshot's own `isComputePass`
// flag (PHASE1) - discovery is now purely generic. The old, single, always-
// after-"GameView" "GPU Skinning"/"AtmosphereAerialPerspectiveCompositePass"
// special-cased tree shape is also gone, replaced by the SPLIT
// "Compute Dispatches (Pre-GameView)"/"Compute Dispatches (Post-GameView)"
// group pair (Locked Design Decision #8) - see each rewritten test's own
// comment below for exactly what changed.

#include "Editor/FrameDebuggerData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

rg::RenderGraphPassSnapshot MakePass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass;
    pass.name = name;
    return pass;
}

// frame-debugger-5 campaign, PHASE2 - a plain graphics pass with
// isComputePass flipped true, exactly as RenderGraphBuilder::AddComputePass()
// (PHASE1) now stamps for every real compute dispatch in this engine.
rg::RenderGraphPassSnapshot MakeComputePass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass = MakePass(name);
    pass.isComputePass = true;
    return pass;
}

TEST(FrameDebuggerSnapshotBuilderTest, NoGameViewPassProducesEmptyResult)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SceneView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("Present"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    EXPECT_TRUE(snapshot.rootNodes.empty());
    EXPECT_EQ(snapshot.totalEventCount, 0);
}

// REWRITTEN (was GameViewWithNoGpuSkinningProducesExactlyOneLeaf) - only the
// call site (dropped the removed gpuSkinningPassNamesThisFrame argument) and
// the name/comment changed; every assertion is unchanged.
TEST(FrameDebuggerSnapshotBuilderTest, GameViewWithNoComputePassesProducesExactlyOneLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // SceneView/Present are real passes in the SAME underlying snapshot -
    // Locked Design Decision #7 (Game-View-only scope) must exclude them
    // even though they're right here alongside "GameView".
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SceneView"));
    rg::RenderGraphPassSnapshot gameView = MakePass("GameView");
    gameView.stats.drawStats.drawCallCount = 7;
    gameView.stats.drawStats.triangleCount = 250;
    graphSnapshot.passesInExecutionOrder.push_back(gameView);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("Present"));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", Mat4::Identity());

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    EXPECT_FALSE(root.isDrawCall);
    ASSERT_EQ(root.children.size(), 1u); // No compute-dispatch groups at all.
    EXPECT_EQ(snapshot.totalEventCount, 1);

    const FrameDebuggerEventNode& leaf = root.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    EXPECT_EQ(leaf.eventIndex, 0);
    ASSERT_TRUE(leaf.details.has_value());
    EXPECT_EQ(leaf.details->passName, "GameView");
    EXPECT_EQ(leaf.details->eventLabel, "Draw Mesh");
    EXPECT_EQ(leaf.details->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(leaf.details->textures.size(), 1u);
    EXPECT_EQ(leaf.details->textures[0].valueLabel, "MaterialTexture abc123");

    // vectors: clear color + real aggregate DrawStats from the graph
    // snapshot's own "GameView" entry (NOT from capture.DrawCallCount()).
    ASSERT_EQ(leaf.details->vectors.size(), 2u);
    bool foundDrawStats = false;
    for (const FrameDebuggerVectorProperty& vec : leaf.details->vectors) {
        if (vec.name == "Draw Stats (Calls, Tris)") {
            foundDrawStats = true;
            EXPECT_FLOAT_EQ(vec.x, 7.0f);
            EXPECT_FLOAT_EQ(vec.y, 250.0f);
        }
    }
    EXPECT_TRUE(foundDrawStats);

    // Real, constant Pipeline state (DescribeStandardPipelineState()).
    EXPECT_EQ(leaf.details->blendMode, "Opaque (no blend)");
    EXPECT_EQ(leaf.details->zTest, "Less");
}

// REWRITTEN (was TwoGpuSkinningPassesProduceGroupPlusGameViewLeaf) - the OLD
// version fed a `gpuSkinningPassNamesThisFrame` name list; discovery is now
// purely generic via isComputePass, so this test marks the two passes
// isComputePass directly instead, and asserts the NEW
// "Compute Dispatches (Pre-GameView)" group name instead of the OLD
// "GPU Skinning" one (both passes appear BEFORE "GameView" in
// passesInExecutionOrder, so both land in the PRE-GameView group). Also
// doubles as PHASE2's own Step 3.6 item (a): one buffer-write pass
// (GPU-Skinning-style) AND one texture-write pass together, both landing
// under ONE group, each read/write row labeled by its own real
// ResourceKind (PHASE1).
TEST(FrameDebuggerSnapshotBuilderTest, TwoPreGameViewComputePassesProduceOnePreGameViewGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot bufferPass = MakeComputePass("SkinPass_A");
    bufferPass.writeNames.push_back("SkinnedVertexBuffer");
    bufferPass.writeKinds.push_back(rg::ResourceKind::Buffer);
    graphSnapshot.passesInExecutionOrder.push_back(bufferPass);

    rg::RenderGraphPassSnapshot texturePass = MakeComputePass("AtmosphereTransmittanceLutPass");
    texturePass.writeNames.push_back("TransmittanceLut");
    texturePass.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(texturePass);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute Dispatches (Pre-GameView)" group + "GameView" leaf, siblings.
    EXPECT_EQ(snapshot.totalEventCount, 3);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_FALSE(preGroup.isDrawCall);
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 2u);
    EXPECT_EQ(preGroup.children[0].name, "SkinPass_A");
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[1].name, "AtmosphereTransmittanceLutPass");
    EXPECT_EQ(preGroup.children[1].eventIndex, 1);

    ASSERT_TRUE(preGroup.children[0].details.has_value());
    EXPECT_EQ(preGroup.children[0].details->eventLabel, "Compute Dispatch");
    // Raw pass name now - no fabricated "GPU Skinning" friendly label anymore.
    EXPECT_EQ(preGroup.children[0].details->passName, "SkinPass_A");
    EXPECT_EQ(preGroup.children[0].details->blendMode, "n/a (compute pass)");
    ASSERT_EQ(preGroup.children[0].details->textures.size(), 1u);
    EXPECT_EQ(preGroup.children[0].details->textures[0].name, "Write Buffer");
    EXPECT_EQ(preGroup.children[0].details->textures[0].valueLabel, "SkinnedVertexBuffer");
    EXPECT_TRUE(preGroup.children[0].details->matrices.empty());

    ASSERT_TRUE(preGroup.children[1].details.has_value());
    ASSERT_EQ(preGroup.children[1].details->textures.size(), 1u);
    EXPECT_EQ(preGroup.children[1].details->textures[0].name, "Write Texture");
    EXPECT_EQ(preGroup.children[1].details->textures[0].valueLabel, "TransmittanceLut");

    const FrameDebuggerEventNode& gameViewLeaf = root.children[1];
    EXPECT_TRUE(gameViewLeaf.isDrawCall);
    EXPECT_EQ(gameViewLeaf.eventIndex, 2); // Sequential across the WHOLE tree.
}

// REWRITTEN (was GpuSkinningNameWithNoMatchingRealPassAddsNoGroup) - the OLD
// scenario ("a caller-supplied name that matches no real pass") can no
// longer occur at all now that the name-list parameter is gone entirely;
// this replaces it with the analogous NEW regression: an ordinary pass
// declared via plain AddPass() (isComputePass defaults to false) must never
// be mistaken for a compute dispatch, no matter what it's named.
TEST(FrameDebuggerSnapshotBuilderTest, NonComputePassIsNeverTreatedAsComputeDispatch)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("SomeOrdinaryGraphicsPass"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 1u); // Only "GameView" - the non-compute pass adds no group.
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

TEST(FrameDebuggerSnapshotBuilderTest, DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("PipelineA", "TextureA", Mat4::Identity());
    capture.RecordDraw("PipelineB", "TextureB", Mat4::Identity());
    capture.RecordDraw("PipelineA", "TextureA", Mat4::Identity()); // Repeat - must not duplicate.

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 1u);
    const FrameDebuggerEventDetails& details = *snapshot.rootNodes[0].children[0].details;
    EXPECT_EQ(details.shaderName, "PipelineA, PipelineB");
    ASSERT_EQ(details.textures.size(), 2u);
    EXPECT_EQ(details.textures[0].valueLabel, "TextureA");
    EXPECT_EQ(details.textures[1].valueLabel, "TextureB");
}

TEST(FrameDebuggerSnapshotBuilderTest, ViewProjectionMatrixRoundTripsWithoutTransposing)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    // A hand-picked, deliberately NON-symmetric matrix: column c holds
    // values {4c+1, 4c+2, 4c+3, 4c+4} - so matrix(row, col) is always
    // DISTINCT from matrix(col, row) whenever row != col, guaranteeing a
    // row/column-major mixup produces visibly wrong values, not a
    // by-coincidence-correct symmetric result.
    const Mat4 matrix(Vec4(1.0f, 2.0f, 3.0f, 4.0f), Vec4(5.0f, 6.0f, 7.0f, 8.0f), Vec4(9.0f, 10.0f, 11.0f, 12.0f),
        Vec4(13.0f, 14.0f, 15.0f, 16.0f));

    FrameDebuggerCaptureContext capture;
    capture.RecordDraw("Pipeline", "", matrix);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventDetails& details = *snapshot.rootNodes[0].children[0].details;
    ASSERT_EQ(details.matrices.size(), 1u);
    EXPECT_EQ(details.matrices[0].name, "ViewProjection");
    const std::array<float, 16>& values = details.matrices[0].values;

    // Row-major expectation: values[row*4 + col] == matrix(row, col).
    // matrix(row, col) == columns[col][row], so row 0 reads {1, 5, 9, 13}
    // (the FIRST element of every column), not {1, 2, 3, 4} (column 0's own
    // elements - what a mistaken raw Data() memcpy would produce instead).
    EXPECT_FLOAT_EQ(values[0], 1.0f);
    EXPECT_FLOAT_EQ(values[1], 5.0f);
    EXPECT_FLOAT_EQ(values[2], 9.0f);
    EXPECT_FLOAT_EQ(values[3], 13.0f);
    EXPECT_FLOAT_EQ(values[4], 2.0f);
    EXPECT_FLOAT_EQ(values[5], 6.0f);
    EXPECT_FLOAT_EQ(values[15], 16.0f);
}

TEST(FrameDebuggerSnapshotBuilderTest, RenderTargetInfoIsRealAndNamedGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    FrameDebuggerRenderTargetInfo info;
    info.name = "ThisShouldBeOverwritten";
    info.width = 1920;
    info.height = 1080;
    info.format = "Texture (RGBA8 UNORM)";

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, info);

    EXPECT_EQ(snapshot.renderTarget.name, "GameView");
    EXPECT_EQ(snapshot.renderTarget.width, 1920);
    EXPECT_EQ(snapshot.renderTarget.height, 1080);
    EXPECT_EQ(snapshot.renderTarget.format, "Texture (RGBA8 UNORM)");
}

// REWRITTEN (was AerialPerspectiveCompositePassProducesThirdLeafAfterGameView,
// frame-debugger-4 campaign PHASE2) - the OLD version asserted the hardcoded
// "Aerial Perspective Composite" friendly passName/"Compute Composite"
// eventLabel/".comp" shaderName special case (BuildAerialPerspectiveCompositeLeaf(),
// now DELETED). The pass is now discovered purely via isComputePass, and its
// leaf carries only real, generic, never-fabricated facts - the raw pass
// name for passName/shaderName, and the generic "Compute Dispatch"
// eventLabel every compute leaf now shares. It also now lands under the NEW
// "Compute Dispatches (Post-GameView)" group (since it runs strictly AFTER
// "GameView") rather than being appended directly as a third top-level leaf.
TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewComputePassProducesLeafUnderPostGameViewGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.readNames.push_back("GameView");
    composite.readKinds.push_back(rg::ResourceKind::Texture);
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    composite.stats.timing.status = GpuTimingSample::Status::Present;
    composite.stats.timing.milliseconds = 0.25;
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "GameView" leaf + new "Compute Dispatches (Post-GameView)" group.
    EXPECT_EQ(snapshot.totalEventCount, 2);

    const FrameDebuggerEventNode& gameViewLeaf = root.children[0];
    EXPECT_TRUE(gameViewLeaf.isDrawCall);
    EXPECT_EQ(gameViewLeaf.name, "GameView");
    EXPECT_EQ(gameViewLeaf.eventIndex, 0);

    const FrameDebuggerEventNode& postGroup = root.children[1];
    EXPECT_FALSE(postGroup.isDrawCall);
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);

    const FrameDebuggerEventNode& leaf = postGroup.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    EXPECT_EQ(leaf.name, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.eventIndex, 1);
    ASSERT_TRUE(leaf.details.has_value());
    // Raw pass name now - no fabricated "Aerial Perspective Composite" label.
    EXPECT_EQ(leaf.details->passName, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.details->eventLabel, "Compute Dispatch"); // Generic label, not the old "Compute Composite".
    // Raw pass name - no hardcoded ".comp" shader filename fabricated anymore.
    EXPECT_EQ(leaf.details->shaderName, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.details->blendMode, "n/a (compute pass)");
    ASSERT_EQ(leaf.details->textures.size(), 2u);
    EXPECT_EQ(leaf.details->textures[0].name, "Read Texture");
    EXPECT_EQ(leaf.details->textures[0].valueLabel, "GameView");
    EXPECT_EQ(leaf.details->textures[1].name, "Write Texture");
    EXPECT_EQ(leaf.details->textures[1].valueLabel, "GameViewComposited");
    ASSERT_EQ(leaf.details->vectors.size(), 1u);
    EXPECT_EQ(leaf.details->vectors[0].name, "GPU Time (ms)");
    EXPECT_FLOAT_EQ(leaf.details->vectors[0].x, 0.25f);
}

// REWRITTEN (was NoAerialPerspectiveCompositePassAddsNoThirdLeaf) - covers
// PHASE2's own Step 3.6 item (b): zero real compute passes at all produces
// NEITHER "Compute Dispatches (...)" group, not an empty one on either side.
TEST(FrameDebuggerSnapshotBuilderTest, NoComputePassesProduceNeitherGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    EXPECT_EQ(snapshot.rootNodes[0].children.size(), 1u); // Only "GameView" - neither compute group appears.
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// REWRITTEN (was AllThreeGroupsAppearTogetherInRealExecutionOrder) - the OLD
// version fed a gpuSkinningPassNamesThisFrame name list and asserted the
// single, always-after-"GameView" "GPU Skinning"/"AtmosphereAerial..." shape.
// NEW version marks both passes isComputePass directly and asserts the SPLIT
// "Compute Dispatches (Pre-GameView)" -> "GameView" -> "Compute Dispatches
// (Post-GameView)" three-top-level-sibling shape (Locked Design Decision #8
// - PHASE2's own Step 3.6 item (a3)).
TEST(FrameDebuggerSnapshotBuilderTest, PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("AtmosphereAerialPerspectiveCompositePass"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_FALSE(preGroup.isDrawCall);
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");

    const FrameDebuggerEventNode& gameViewLeaf = root.children[1];
    EXPECT_TRUE(gameViewLeaf.isDrawCall);
    EXPECT_EQ(gameViewLeaf.name, "GameView");

    const FrameDebuggerEventNode& postGroup = root.children[2];
    EXPECT_FALSE(postGroup.isDrawCall);
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");

    // eventIndex values strictly increasing left-to-right across the whole
    // tree: 0 (SkinPass_A), 1 (GameView), 2 (AtmosphereAerialPerspectiveCompositePass).
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_EQ(postGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(gameViewLeaf.eventIndex, 1);
    EXPECT_EQ(postGroup.children[0].eventIndex, 2);
    EXPECT_EQ(snapshot.totalEventCount, 3);
}

// frame-debugger-6 campaign, PHASE2
// (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md, Step 4) - the
// actual regression proof for the real-world bug this whole campaign started
// from (PHASE0_MASTER_STRATEGY.md Section 0): TWO real compute passes, ONE
// literal shared name ("AtmosphereSkyViewLutPass"), one genuinely GameView-
// scoped and one genuinely SceneView-scoped - only the GameView one may ever
// appear in this Game-View-scoped tree.
TEST(FrameDebuggerSnapshotBuilderTest, SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot gameViewScoped = MakeComputePass("AtmosphereSkyViewLutPass");
    gameViewScoped.viewScope = rg::ViewScope::GameView;
    graphSnapshot.passesInExecutionOrder.push_back(gameViewScoped);

    rg::RenderGraphPassSnapshot sceneViewScoped = MakeComputePass("AtmosphereSkyViewLutPass");
    sceneViewScoped.viewScope = rg::ViewScope::SceneView;
    graphSnapshot.passesInExecutionOrder.push_back(sceneViewScoped);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute Dispatches (Pre-GameView)" group + "GameView" leaf.

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u); // NOT two - the SceneView-scoped duplicate must be gone.
    EXPECT_EQ(preGroup.children[0].name, "AtmosphereSkyViewLutPass");
    EXPECT_EQ(snapshot.totalEventCount, 2);
}

// Symmetric coverage for the POST-GameView group - models the real
// "AtmosphereAerialPerspectiveCompositePass" duplicate scenario from
// PHASE0_MASTER_STRATEGY.md Section 0.
TEST(FrameDebuggerSnapshotBuilderTest, SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    rg::RenderGraphPassSnapshot gameViewScoped = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    gameViewScoped.viewScope = rg::ViewScope::GameView;
    graphSnapshot.passesInExecutionOrder.push_back(gameViewScoped);

    rg::RenderGraphPassSnapshot sceneViewScoped = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    sceneViewScoped.viewScope = rg::ViewScope::SceneView;
    graphSnapshot.passesInExecutionOrder.push_back(sceneViewScoped);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "GameView" leaf + "Compute Dispatches (Post-GameView)" group.

    const FrameDebuggerEventNode& postGroup = root.children[1];
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u); // NOT two - the SceneView-scoped duplicate must be gone.
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(snapshot.totalEventCount, 2);
}

// Guards against an over-eager fix that accidentally also excludes
// ViewScope::Shared passes (e.g. the real Transmittance/Multi-Scattering LUT
// passes, genuinely computed once per frame, not once per view) - these must
// still appear exactly as before this campaign.
TEST(FrameDebuggerSnapshotBuilderTest, SharedViewScopedPassStillAppearsNormally)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot shared = MakeComputePass("AtmosphereTransmittanceLutPass");
    shared.viewScope = rg::ViewScope::Shared; // Explicit, though this is also the default.
    graphSnapshot.passesInExecutionOrder.push_back(shared);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u);
    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].name, "AtmosphereTransmittanceLutPass");
    EXPECT_EQ(snapshot.totalEventCount, 2);
}

// frame-debugger-6 campaign, PHASE4
// (PHASE4_GAMEVIEW_PER_ENTITY_DRAW_TREE_LEAVES.md, Step 4) - the actual
// user-facing feature: the "GameView" node now gets one real child leaf per
// real FrameDebuggerDrawRecord captured this frame.
TEST(FrameDebuggerSnapshotBuilderTest, GameViewNodeGetsOneChildLeafPerDrawRecord)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 1u); // Only "GameView" - no compute-dispatch groups.

    const FrameDebuggerEventNode& gameViewLeaf = root.children[0];
    EXPECT_TRUE(gameViewLeaf.isDrawCall);
    EXPECT_EQ(gameViewLeaf.eventIndex, 0);
    ASSERT_EQ(gameViewLeaf.children.size(), 2u); // (a) exactly that many children.

    // (b) each child's name/eventIndex is correct.
    const FrameDebuggerEventNode& terrainLeaf = gameViewLeaf.children[0];
    EXPECT_EQ(terrainLeaf.name, "terrain (Entity 2)");
    EXPECT_TRUE(terrainLeaf.isDrawCall);
    EXPECT_EQ(terrainLeaf.eventIndex, 1);
    ASSERT_TRUE(terrainLeaf.details.has_value());
    EXPECT_EQ(terrainLeaf.details->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
    EXPECT_TRUE(terrainLeaf.details->textures.empty()); // Untextured draw - no Material Texture row.
    bool foundTerrainTriangleCount = false;
    for (const FrameDebuggerVectorProperty& vec : terrainLeaf.details->vectors) {
        if (vec.name == "Triangle Count") {
            foundTerrainTriangleCount = true;
            EXPECT_FLOAT_EQ(vec.x, 1045458.0f); // This ONE draw's own count, never the whole-pass aggregate.
        }
    }
    EXPECT_TRUE(foundTerrainTriangleCount);

    const FrameDebuggerEventNode& cubeLeaf = gameViewLeaf.children[1];
    EXPECT_EQ(cubeLeaf.name, "SmokeTestCube (Entity 3)");
    EXPECT_EQ(cubeLeaf.eventIndex, 2);
    ASSERT_TRUE(cubeLeaf.details.has_value());
    ASSERT_EQ(cubeLeaf.details->textures.size(), 1u);
    EXPECT_EQ(cubeLeaf.details->textures[0].name, "Material Texture");
    EXPECT_EQ(cubeLeaf.details->textures[0].valueLabel, "MaterialTexture abc123");

    // (c) eventIndex stays strictly monotonic increasing: GameView(0) < terrain(1) < cube(2).
    EXPECT_LT(gameViewLeaf.eventIndex, terrainLeaf.eventIndex);
    EXPECT_LT(terrainLeaf.eventIndex, cubeLeaf.eventIndex);

    // (d) totalEventCount correctly includes the new children.
    EXPECT_EQ(snapshot.totalEventCount, 3);
}

// eventIndex stays strictly monotonic across pre-group -> GameView ->
// GameView's children -> post-group, extending
// EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView one level
// deeper.
TEST(FrameDebuggerSnapshotBuilderTest, EventIndexIsMonotonicAcrossPreGameViewGameViewChildrenAndPostGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& gameViewLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_EQ(gameViewLeaf.children.size(), 2u);
    ASSERT_EQ(postGroup.children.size(), 1u);

    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(gameViewLeaf.eventIndex, 1);
    EXPECT_EQ(gameViewLeaf.children[0].eventIndex, 2);
    EXPECT_EQ(gameViewLeaf.children[1].eventIndex, 3);
    EXPECT_EQ(postGroup.children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// No draw records captured this frame -> "GameView" keeps zero children,
// exactly like every earlier campaign's behavior (an honestly empty Game
// View, e.g. before the very first mesh entity is spawned).
TEST(FrameDebuggerSnapshotBuilderTest, GameViewNodeHasNoChildrenWhenNoDrawRecordsCaptured)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    EXPECT_TRUE(gameViewLeaf.children.empty());
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// REQUIRED regression test (found by this campaign's own double-check pass,
// not a shipped bug, PHASE4's own Step 4) - a per-entity draw-record child
// leaf's own `details->passName` must NEVER be the literal string "GameView"
// - only the real "GameView" pass leaf itself may ever carry that exact
// value. FrameDebuggerPanel::EnsurePreviewDescriptor() (UNCHANGED by this
// phase) decides `isViewingGameViewLeaf` via `details->passName == "GameView"`
// - an EXACT string compare - so a collision here would silently force every
// per-entity leaf to show the pre-atmosphere-composite-only image, directly
// contradicting this phase's own Step 5 "falls back to the existing
// whole-frame compositedPreview/preview image" scope statement.
TEST(FrameDebuggerSnapshotBuilderTest, GameViewDrawRecordLeafPassNameIsNeverLiterallyGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 1u);
    ASSERT_TRUE(gameViewLeaf.children[0].details.has_value());
    EXPECT_NE(gameViewLeaf.children[0].details->passName, "GameView");
    // Also confirm the real "GameView" pass leaf itself is unaffected - it
    // must still carry the literal value, unchanged.
    ASSERT_TRUE(gameViewLeaf.details.has_value());
    EXPECT_EQ(gameViewLeaf.details->passName, "GameView");
}



// NEW - PHASE2's own Step 3.6 item (c): a culled compute pass must never
// appear in either group, on EITHER side of "GameView" - a culled pass did
// not really run this frame, so showing it as if it did would be dishonest.
TEST(FrameDebuggerSnapshotBuilderTest, CulledComputePassIsExcludedFromEitherComputeDispatchGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot culledPre = MakeComputePass("CulledPreComputePass");
    culledPre.isCulled = true;
    graphSnapshot.passesInExecutionOrder.push_back(culledPre);

    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SurvivingPreComputePass"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SurvivingPostComputePass"));

    rg::RenderGraphPassSnapshot culledPost = MakeComputePass("CulledPostComputePass");
    culledPost.isCulled = true;
    graphSnapshot.passesInExecutionOrder.push_back(culledPost);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    ASSERT_EQ(preGroup.children.size(), 1u); // The culled pre pass never appears.
    EXPECT_EQ(preGroup.children[0].name, "SurvivingPreComputePass");

    const FrameDebuggerEventNode& postGroup = root.children[2];
    ASSERT_EQ(postGroup.children.size(), 1u); // The culled post pass never appears.
    EXPECT_EQ(postGroup.children[0].name, "SurvivingPostComputePass");

    EXPECT_EQ(snapshot.totalEventCount, 3); // Only 3 real events: one pre leaf, GameView, one post leaf.
}

// NEW - PHASE2's own Step 3.6 item (d): every read/write row is labeled by
// its own real ResourceKind (PHASE1's readKinds/writeKinds), covering all
// three kinds for BOTH reads and writes on one pass.
TEST(FrameDebuggerSnapshotBuilderTest, ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot pass = MakeComputePass("MixedKindComputePass");
    pass.readNames = { "SomeTexture", "SomeBuffer", "SomeVolume" };
    pass.readKinds
        = { rg::ResourceKind::Texture, rg::ResourceKind::Buffer, rg::ResourceKind::VolumeTexture };
    pass.writeNames = { "OutputTexture", "OutputBuffer", "OutputVolume" };
    pass.writeKinds
        = { rg::ResourceKind::Texture, rg::ResourceKind::Buffer, rg::ResourceKind::VolumeTexture };
    graphSnapshot.passesInExecutionOrder.push_back(pass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
    const FrameDebuggerEventNode& preGroup = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_TRUE(preGroup.children[0].details.has_value());
    const FrameDebuggerEventDetails& details = *preGroup.children[0].details;

    ASSERT_EQ(details.textures.size(), 6u);
    EXPECT_EQ(details.textures[0].name, "Read Texture");
    EXPECT_EQ(details.textures[0].valueLabel, "SomeTexture");
    EXPECT_EQ(details.textures[1].name, "Read Buffer");
    EXPECT_EQ(details.textures[1].valueLabel, "SomeBuffer");
    EXPECT_EQ(details.textures[2].name, "Read Volume Texture");
    EXPECT_EQ(details.textures[2].valueLabel, "SomeVolume");
    EXPECT_EQ(details.textures[3].name, "Write Texture");
    EXPECT_EQ(details.textures[3].valueLabel, "OutputTexture");
    EXPECT_EQ(details.textures[4].name, "Write Buffer");
    EXPECT_EQ(details.textures[4].valueLabel, "OutputBuffer");
    EXPECT_EQ(details.textures[5].name, "Write Volume Texture");
    EXPECT_EQ(details.textures[5].valueLabel, "OutputVolume");
}

// NEW - PHASE2's own Step 3.6 item (e): eventIndex values across the WHOLE
// tree are monotonically increasing in true chronological order - every
// pre-GameView leaf's index < "GameView"'s own index < every post-GameView
// leaf's index. Regression coverage for BuildRealFrameDebuggerSnapshot()'s
// own documented "do not reorder these three blocks" ordering caveat.
TEST(FrameDebuggerSnapshotBuilderTest, EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreB"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostB"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& gameViewLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    ASSERT_EQ(preGroup.children.size(), 2u);
    ASSERT_EQ(postGroup.children.size(), 2u);

    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[1].eventIndex, 1);
    EXPECT_EQ(gameViewLeaf.eventIndex, 2);
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    EXPECT_EQ(postGroup.children[1].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.5) - the old
// `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`
// tests (frame-debugger-5 campaign) were DELETED along with the functions
// they covered (PHASE0's Locked Design Decision #3). Replaced below with
// coverage proving `BuildRealFrameDebuggerSnapshot()` assigns the right
// `stepPreviewKind`/`stepPreviewIndex` to a Pre-GameView leaf, the
// "GameView" leaf, each per-object child (in order), a Post-GameView-
// before-composite leaf, and a Post-GameView-at/after-composite leaf.

TEST(FrameDebuggerSnapshotBuilderTest, PreGameViewLeafGetsNotYetDrawnStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& preGroup = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_TRUE(preGroup.children[0].details.has_value());
    EXPECT_EQ(preGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::NotYetDrawn);
}

TEST(FrameDebuggerSnapshotBuilderTest, GameViewLeafGetsPreCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_TRUE(gameViewLeaf.details.has_value());
    EXPECT_EQ(gameViewLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);
}

TEST(FrameDebuggerSnapshotBuilderTest, PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 2u);

    ASSERT_TRUE(gameViewLeaf.children[0].details.has_value());
    EXPECT_EQ(gameViewLeaf.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PerObjectStep);
    EXPECT_EQ(gameViewLeaf.children[0].details->stepPreviewIndex, 0);

    ASSERT_TRUE(gameViewLeaf.children[1].details.has_value());
    EXPECT_EQ(gameViewLeaf.children[1].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PerObjectStep);
    EXPECT_EQ(gameViewLeaf.children[1].details->stepPreviewIndex, 1);
}

TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomeOtherPostGameViewPass"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 2u);
    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].name, "SomeOtherPostGameViewPass");
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);
}

TEST(FrameDebuggerSnapshotBuilderTest, CompositePassLeafItselfGetsPostCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomeLaterPostCompositePass"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 2u);

    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);

    // A leaf strictly AFTER the composite pass is ALSO PostComposite.
    ASSERT_TRUE(postGroup.children[1].details.has_value());
    EXPECT_EQ(postGroup.children[1].name, "SomeLaterPostCompositePass");
    EXPECT_EQ(postGroup.children[1].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);
}

TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives)
{
    // No pass anywhere writes "GameViewComposited" this frame (e.g. a
    // capture taken before the atmosphere composite pass has ever run this
    // session) - FindPostGameViewCompositePassExecutionIndex() returns -1,
    // and every Post-GameView leaf falls into the PostComposite default
    // catch-all bucket (Step 2's own "or nothing selected" wording).
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomePostGameViewPassWithNoCompositeWrite"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 1u);
    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);
}

// frame-debugger-8 campaign, PHASE3
// (PHASE3_SNAPSHOT_TREE_LEAF_AND_TESTS.md, Step 3.4) - proves PHASE1 through
// PHASE3 compose correctly with the PRE-EXISTING pre/post-GameView
// compute-dispatch split (frame-debugger-5/frame-debugger-6 campaigns) with
// zero regressions: one Pre-GameView compute leaf, "GameView" itself with
// two children (entity, then sky), and one Post-GameView compute leaf - all
// five leaves' eventIndex values strictly monotonic increasing in that
// exact left-to-right, top-to-bottom order.
TEST(FrameDebuggerSnapshotBuilderTest, SkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("GameView"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordSkyBackgroundDraw("AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag");

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& gameViewLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);

    EXPECT_EQ(gameViewLeaf.name, "GameView");
    ASSERT_EQ(gameViewLeaf.children.size(), 2u); // entity, then sky.
    const FrameDebuggerEventNode& entityLeaf = gameViewLeaf.children[0];
    const FrameDebuggerEventNode& skyLeaf = gameViewLeaf.children[1];
    EXPECT_EQ(entityLeaf.name, "terrain (Entity 2)");
    EXPECT_EQ(skyLeaf.name, "AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag");
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_EQ(skyLeaf.details->passName, "GameView (Sky Draw)");

    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);

    // eventIndex strictly monotonic across all 5 leaves, left-to-right,
    // top-to-bottom.
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(gameViewLeaf.eventIndex, 1);
    EXPECT_EQ(entityLeaf.eventIndex, 2);
    EXPECT_EQ(skyLeaf.eventIndex, 3);
    EXPECT_EQ(postGroup.children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

} // namespace
} // namespace gte
