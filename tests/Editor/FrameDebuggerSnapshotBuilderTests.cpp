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
// instead mark the relevant RenderGraphPassSnapshot's own `kind`
// flag (PHASE1) - discovery is now purely generic. The old, single, always-
// after-"GameView" "GPU Skinning"/"AtmosphereAerialPerspectiveCompositePass"
// special-cased tree shape is also gone, replaced by the SPLIT
// "Compute Dispatches (Pre-GameView)"/"Compute Dispatches (Post-GameView)"
// group pair (Locked Design Decision #8) - see each rewritten test's own
// comment below for exactly what changed.
//
// Render Pass campaign (task_manager/render-pass-1), PHASE4
// (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md) - the old monolithic
// "GameView" PASS was already split by PHASE2 of this campaign into
// "RenderOpaque"/"DrawSkyBackground"/"RenderTransparent"; every fixture below
// that used to name its pivot pass "GameView" now names it "RenderOpaque"
// instead (the pivot lookup itself was retargeted - see
// BuildRealFrameDebuggerSnapshot()'s own updated doc comment), every
// "GameView"/"GameView (Entity Draw)" literal assertion is now
// "RenderOpaque"/"RenderOpaque (Entity Draw)", and several NEW tests were
// added covering the "Compute LUT" vs. "Compute Dispatches (Pre-GameView)"
// category split (3.2), the new "DrawSkyBackground" leaf (3.3), the
// `RenderTransparent`-never-appears-when-absent behavior (3.5), and the
// Debug-category-pass exclusion regression this phase's own pre-check found
// (3.3b) - see each test's own comment.
//
// Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE3 (PHASE3_TEST_SUITE_MIGRATION_AND_DOCS_UPDATE.md) - PHASE2 turned
// EVERY real pass leaf this file builds (except "RenderOpaque" itself, which
// keeps its own untouched per-entity mechanism) into a real "v PassName"
// parent OWNING exactly one real, independently-selectable child event row
// (WrapPassWithOwnedChildEvent()) - every wrapped pass now consumes TWO
// consecutive `nextEventIndex` values instead of one. This phase re-verified
// all 31 pre-existing tests below against that new shape (updating the ones
// that hardcoded an old flat shape/old eventIndex/totalEventCount number),
// and added 4 new tests covering the fix itself (dual-selectability for both
// a wrapped Graphics-kind AND a wrapped Compute-kind pass,
// RenderPassDrawKind-driven child labeling including the Blit scaffold
// value, and the Compute-LUT-sub-pass case) - see PHASE3_COMPLETION_REPORT.md
// for the full, independently-re-verified per-test checklist.

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
// kind == rg::PassKind::Compute, exactly as RenderGraphBuilder::AddComputePass()
// (PHASE1) now stamps for every real compute dispatch in this engine.
//
// render-pass-3 campaign, PHASE4 - ALSO now defaults `renderPassEvent` to
// `RenderPassEvent::PreOpaques`, mirroring how every REAL pre-view compute
// pass in this engine is actually tagged in production (GpuSkinning/every
// Atmosphere LUT pass - see PHASE0_MASTER_STRATEGY.md's own intended
// RenderPassEvent mapping, and PHASE2/PHASE3's own completion reports
// confirming those real passes carry exactly this value). This is required
// for BuildRealFrameDebuggerSnapshot()'s new FindViewRegionPivot() lookup
// (which finds the first pass whose renderPassEvent >= Opaques) to keep
// treating every fixture's "RenderOpaque" pass (built via the plain,
// unmodified MakePass() - default renderPassEvent is Opaques) as the pivot,
// exactly like the OLD name-based FindPassByName(..., "RenderOpaque") always
// did - without this, a synthetic pre-view compute pass built via this
// helper would share "RenderOpaque"'s own default Opaques value and
// incorrectly become the pivot itself purely by sitting earlier in the
// fixture's own passesInExecutionOrder vector.
rg::RenderGraphPassSnapshot MakeComputePass(const std::string& name)
{
    rg::RenderGraphPassSnapshot pass = MakePass(name);
    pass.kind = rg::PassKind::Compute;
    pass.renderPassEvent = rg::RenderPassEvent::PreOpaques;
    return pass;
}

// Render Pass campaign (task_manager/render-pass-1), PHASE4 - a graphics
// pass with a given RenderPassCategory, mirroring how
// AddFrameDebuggerReplayPasses() (RenderPasses.cpp) now tags its own N
// replay passes rg::RenderPassCategory::Debug (PHASE4's own 3.3b migration).
rg::RenderGraphPassSnapshot MakeGraphicsPassWithCategory(const std::string& name, rg::RenderPassCategory category)
{
    rg::RenderGraphPassSnapshot pass = MakePass(name);
    pass.category = category;
    return pass;
}

TEST(FrameDebuggerSnapshotBuilderTest, NoRenderOpaquePassProducesEmptyResult)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // render-pass-3 campaign, PHASE4 - both passes here must be explicitly
    // tagged BELOW RenderPassEvent::Opaques (MakePass()'s own default IS
    // Opaques - see MakePass() above), or the new FindViewRegionPivot()
    // lookup would incorrectly treat one of them as a genuine pivot, since
    // it never checks a pass's NAME at all. Neither of these two names
    // corresponds to any REAL pass in production anymore (the old
    // monolithic "SceneView"/"GameView" passes were split apart back in the
    // render-pass-1 campaign) - this fixture only needs "some pass, some
    // other pass, definitely no Opaques-or-later pass" to prove the "empty
    // result" contract.
    rg::RenderGraphPassSnapshot sceneView = MakePass("SceneView");
    sceneView.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(sceneView);
    rg::RenderGraphPassSnapshot present = MakePass("Present");
    present.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(present);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    EXPECT_TRUE(snapshot.rootNodes.empty());
    EXPECT_EQ(snapshot.totalEventCount, 0);
}

// REWRITTEN (was GameViewWithNoComputePassesProducesExactlyOneLeaf) - the
// pivot pass literal ("GameView" -> "RenderOpaque") and the passName
// assertion changed. Render Pass campaign (task_manager/render-pass-1),
// PHASE4 - the original fixture also included a trailing "Present" pass
// AFTER the pivot to prove Game-View-only scope excludes it - this is no
// longer a safe inclusion under the NEW "view region" walk (Step 3.3), which
// (correctly, per its own spec) treats ANY surviving, non-SceneView-scoped,
// non-Debug-category Graphics pass positioned after the pivot as a real view-
// region leaf - in the REAL engine this scenario never actually arises
// ("Present" is declared in a completely SEPARATE RenderGraph::Execute() call
// from "RenderOpaque" - see Application.cpp - so the two are never part of
// the same RenderGraphSnapshot at all), so artificially combining them in one
// fixture no longer reflects reality. Dropped from this fixture; "SceneView"
// stays (BEFORE the pivot, still exercising exclusion the same way it always
// has - the pre-view loop only ever inspects Compute-kind passes).
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: only "SceneView" +
// "RenderOpaque", zero compute passes, no "DrawSkyBackground" - nothing here
// is ever wrapped by PHASE2's WrapPassWithOwnedChildEvent().
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueWithNoComputePassesProducesExactlyOneLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // SceneView is a real pass in the SAME underlying snapshot - Locked
    // Design Decision #7 (Game-View-only scope) must exclude it even though
    // it's right here alongside "RenderOpaque".
    // render-pass-3 campaign, PHASE4 - explicitly tagged BELOW
    // RenderPassEvent::Opaques (see MakeComputePass()'s own updated doc
    // comment above for why this is now required - MakePass()'s own default
    // renderPassEvent IS Opaques, same as "RenderOpaque" itself).
    rg::RenderGraphPassSnapshot sceneView = MakePass("SceneView");
    sceneView.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(sceneView);
    rg::RenderGraphPassSnapshot renderOpaque = MakePass("RenderOpaque");
    renderOpaque.stats.drawStats.drawCallCount = 7;
    renderOpaque.stats.drawStats.triangleCount = 250;
    graphSnapshot.passesInExecutionOrder.push_back(renderOpaque);

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
    EXPECT_EQ(leaf.details->passName, "RenderOpaque");
    EXPECT_EQ(leaf.details->eventLabel, "Draw Mesh");
    EXPECT_EQ(leaf.details->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
    ASSERT_EQ(leaf.details->textures.size(), 1u);
    EXPECT_EQ(leaf.details->textures[0].valueLabel, "MaterialTexture abc123");

    // vectors: clear color + real aggregate DrawStats from the graph
    // snapshot's own "RenderOpaque" entry (NOT from capture.DrawCallCount()).
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

// REWRITTEN (was TwoPreGameViewComputePassesProduceOnePreGameViewGroup) -
// Render Pass campaign, PHASE4 - this is now the "Compute LUT" vs. "Compute
// Dispatches (Pre-GameView)" category-split regression test (Step 3.6's own
// required "mixed" fixture): one AtmosphereLut-category pass ("SkyLutPass")
// and one GpuSkinning-category pass ("SkinPass_A") in the SAME fixture,
// declared in an order that DELIBERATELY puts the non-LUT pass BEFORE the
// LUT pass in real execution order - proving the tree's PRESENTATION order
// ("Compute LUT" always first) is a fixed rule, never tied to real
// interleaved execution order.
//
// REWRITTEN AGAIN (Frame Debugger Pass-Ownership campaign,
// task_manager/render-pass-2, PHASE3) - each wrapped compute pass now also
// owns a real "Compute Dispatch" child event, consuming one extra
// nextEventIndex value - totalEventCount and every eventIndex from
// "SkyLutPass" onward shift accordingly; child-event assertions added for
// both compute leaves.
TEST(FrameDebuggerSnapshotBuilderTest, MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot bufferPass = MakeComputePass("SkinPass_A");
    bufferPass.category = rg::RenderPassCategory::GpuSkinning;
    bufferPass.writeNames.push_back("SkinnedVertexBuffer");
    bufferPass.writeKinds.push_back(rg::ResourceKind::Buffer);
    graphSnapshot.passesInExecutionOrder.push_back(bufferPass);

    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("SkyLutPass");
    lutPass.category = rg::RenderPassCategory::AtmosphereLut;
    lutPass.writeNames.push_back("TransmittanceLut");
    lutPass.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "Compute LUT" group + "Compute Dispatches (Pre-GameView)" group +
    // "RenderOpaque" leaf, siblings, IN THAT ORDER regardless of real
    // execution order.
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(snapshot.totalEventCount, 5); // Each wrapped pass now consumes 2 indices (parent + child).

    const FrameDebuggerEventNode& lutGroup = root.children[0];
    EXPECT_FALSE(lutGroup.isDrawCall);
    EXPECT_EQ(lutGroup.name, "Compute LUT");
    ASSERT_EQ(lutGroup.children.size(), 1u);
    EXPECT_EQ(lutGroup.children[0].name, "SkyLutPass");
    // Real execution order was index 1 (after SkinPass_A's own parent+child
    // pair) - eventIndex reflects that TRUE execution order even though the
    // tree lists this group first. SkyLutPass now also owns one real child
    // event ("Compute Dispatch"), consuming indices 2 (parent) and 3 (child).
    EXPECT_EQ(lutGroup.children[0].eventIndex, 2);
    ASSERT_EQ(lutGroup.children[0].children.size(), 1u);
    EXPECT_EQ(lutGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(lutGroup.children[0].children[0].eventIndex, 3);

    const FrameDebuggerEventNode& preGroup = root.children[1];
    EXPECT_FALSE(preGroup.isDrawCall);
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].name, "SkinPass_A");
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[2];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 4); // Sequential across the WHOLE tree.
}

// NEW - Render Pass campaign, PHASE4 - proves a pre-view compute pass tagged
// ONLY AtmosphereLut produces ONLY the "Compute LUT" group, never an empty
// "Compute Dispatches (Pre-GameView)" sibling.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED as written: only
// checks root.children.size()/names, no index/count-on-wrapped-node
// assertions. See ComputeLutSubPassAlsoOwnsAComputeDispatchChild (below) for
// the new, dedicated test proving the wrapped-child fact for this exact
// fixture shape.
TEST(FrameDebuggerSnapshotBuilderTest, OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("AtmosphereTransmittanceLutPass");
    lutPass.category = rg::RenderPassCategory::AtmosphereLut;
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute LUT" group + "RenderOpaque" leaf, nothing else.
    EXPECT_EQ(root.children[0].name, "Compute LUT");
    EXPECT_EQ(root.children[1].name, "RenderOpaque");
}

// REWRITTEN (was GpuSkinningNameWithNoMatchingRealPassAddsNoGroup) - the OLD
// scenario ("a caller-supplied name that matches no real pass") can no
// longer occur at all now that the name-list parameter is gone entirely;
// this replaces it with the analogous NEW regression: an ordinary pass
// declared via plain AddPass() (kind defaults to PassKind::Graphics) must never
// be mistaken for a compute dispatch, no matter what it's named.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: the extra pass is
// Graphics-kind and sits BEFORE the pivot, so it is never visited by either
// loop at all - never wrapped, zero index consumption.
TEST(FrameDebuggerSnapshotBuilderTest, NonComputePassIsNeverTreatedAsComputeDispatch)
{
    rg::RenderGraphSnapshot graphSnapshot;
    // render-pass-3 campaign, PHASE4 - explicitly tagged BELOW
    // RenderPassEvent::Opaques (see MakeComputePass()'s own updated doc
    // comment above) so it stays correctly invisible to the new
    // FindViewRegionPivot() lookup too, matching its pre-existing
    // invisibility to the OLD name-based pivot search exactly.
    rg::RenderGraphPassSnapshot ordinaryGraphicsPass = MakePass("SomeOrdinaryGraphicsPass");
    ordinaryGraphicsPass.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(ordinaryGraphicsPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    // Only "RenderOpaque" - the non-compute pass sitting BEFORE the pivot is
    // never visited by the pre-view compute loop (which only inspects
    // Compute-kind passes) and is never reached by the view-region walk
    // either (which only starts AT the pivot).
    ASSERT_EQ(snapshot.rootNodes[0].children.size(), 1u);
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"-only
// fixture, no compute pass, no other Graphics-kind pass.
TEST(FrameDebuggerSnapshotBuilderTest, DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

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

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"-only
// fixture.
TEST(FrameDebuggerSnapshotBuilderTest, ViewProjectionMatrixRoundTripsWithoutTransposing)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

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

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"-only
// fixture.
TEST(FrameDebuggerSnapshotBuilderTest, RenderTargetInfoIsRealAndNamedGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    FrameDebuggerRenderTargetInfo info;
    info.name = "ThisShouldBeOverwritten";
    info.width = 1920;
    info.height = 1080;
    info.format = "Texture (RGBA8 UNORM)";

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, info);

    // "GameView" here is the RenderTexture/resource name, completely
    // unrelated to (and unaffected by) the "RenderOpaque" PASS-name pivot
    // above - see PHASE0_MASTER_STRATEGY.md's own Step 2 point 4.
    EXPECT_EQ(snapshot.renderTarget.name, "GameView");
    EXPECT_EQ(snapshot.renderTarget.width, 1920);
    EXPECT_EQ(snapshot.renderTarget.height, 1080);
    EXPECT_EQ(snapshot.renderTarget.format, "Texture (RGBA8 UNORM)");
}

// REWRITTEN (was AerialPerspectiveCompositePassProducesThirdLeafAfterGameView,
// frame-debugger-4 campaign PHASE2) - the OLD version asserted the hardcoded
// "Aerial Perspective Composite" friendly passName/"Compute Composite"
// eventLabel/".comp" shaderName special case (BuildAerialPerspectiveCompositeLeaf(),
// now DELETED). The pass is now discovered purely via `kind`, and its
// leaf carries only real, generic, never-fabricated facts - the raw pass
// name for passName/shaderName, and the generic "Compute Dispatch"
// eventLabel every compute leaf now shares. It also now lands under the NEW
// "Compute Dispatches (Post-GameView)" group (since it runs strictly AFTER
// "RenderOpaque") rather than being appended directly as a third top-level leaf.
//
// REWRITTEN AGAIN (Frame Debugger Pass-Ownership campaign,
// task_manager/render-pass-2, PHASE3) - the composite pass now also owns a
// real "Compute Dispatch" child event; totalEventCount grows from 2 to 3.
TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewComputePassProducesLeafUnderPostGameViewGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

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
    ASSERT_EQ(root.children.size(), 2u); // "RenderOpaque" leaf + new "Compute Dispatches (Post-GameView)" group.
    EXPECT_EQ(snapshot.totalEventCount, 3); // RenderOpaque(0) + composite parent(1) + composite child(2).

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[0];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 0);

    const FrameDebuggerEventNode& postGroup = root.children[1];
    EXPECT_FALSE(postGroup.isDrawCall);
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);

    const FrameDebuggerEventNode& leaf = postGroup.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    EXPECT_EQ(leaf.name, "AtmosphereAerialPerspectiveCompositePass");
    EXPECT_EQ(leaf.eventIndex, 1); // Unaffected - the PARENT node's own index is unchanged by wrapping.
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

    // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE2/PHASE3 -
    // the pass now also owns one real child event, "Compute Dispatch".
    ASSERT_EQ(leaf.children.size(), 1u);
    const FrameDebuggerEventNode& child = leaf.children[0];
    EXPECT_EQ(child.name, "Compute Dispatch");
    EXPECT_EQ(child.eventIndex, 2);
    ASSERT_TRUE(child.details.has_value());
    EXPECT_EQ(child.details->eventLabel, "Compute Dispatch");
    EXPECT_EQ(child.details->passName, "AtmosphereAerialPerspectiveCompositePass");
}

// REWRITTEN (was NoAerialPerspectiveCompositePassAddsNoThirdLeaf) - covers
// PHASE2's own Step 3.6 item (b): zero real compute passes at all produces
// NEITHER "Compute Dispatches (...)" group, not an empty one on either side.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"-only
// fixture, nothing ever wrapped.
TEST(FrameDebuggerSnapshotBuilderTest, NoComputePassesProduceNeitherGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    EXPECT_EQ(snapshot.rootNodes[0].children.size(), 1u); // Only "RenderOpaque" - no compute group appears.
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// REWRITTEN (was AllThreeGroupsAppearTogetherInRealExecutionOrder) - the OLD
// version fed a gpuSkinningPassNamesThisFrame name list and asserted the
// single, always-after-"GameView" "GPU Skinning"/"AtmosphereAerial..." shape.
// NEW version marks both passes kind == rg::PassKind::Compute directly and asserts the SPLIT
// "Compute Dispatches (Pre-GameView)" -> "RenderOpaque" -> "Compute Dispatches
// (Post-GameView)" three-top-level-sibling shape (Locked Design Decision #8
// - PHASE2's own Step 3.6 item (a3)).
//
// REWRITTEN AGAIN (Frame Debugger Pass-Ownership campaign,
// task_manager/render-pass-2, PHASE3) - both wrapped compute passes now also
// own a real "Compute Dispatch" child event; eventIndex/totalEventCount
// updated accordingly.
TEST(FrameDebuggerSnapshotBuilderTest, PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("AtmosphereAerialPerspectiveCompositePass"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_FALSE(preGroup.isDrawCall);
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");

    const FrameDebuggerEventNode& postGroup = root.children[2];
    EXPECT_FALSE(postGroup.isDrawCall);
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");

    // eventIndex values strictly increasing left-to-right across the whole
    // tree: SkinPass_A(0) + its own child "Compute Dispatch"(1), RenderOpaque(2),
    // AtmosphereAerialPerspectiveCompositePass(3) + its own child "Compute Dispatch"(4).
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_EQ(postGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// frame-debugger-6 campaign, PHASE2
// (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md, Step 4) - the
// actual regression proof for the real-world bug this whole campaign started
// from (PHASE0_MASTER_STRATEGY.md Section 0): TWO real compute passes, ONE
// literal shared name ("AtmosphereSkyViewLutPass"), one genuinely GameView-
// scoped and one genuinely SceneView-scoped - only the GameView one may ever
// appear in this Game-View-scoped tree.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - the surviving GameView-scoped pass now also owns a real
// "Compute Dispatch" child event; totalEventCount grows from 2 to 3.
TEST(FrameDebuggerSnapshotBuilderTest, SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot gameViewScoped = MakeComputePass("AtmosphereSkyViewLutPass");
    gameViewScoped.viewScope = rg::ViewScope::GameView;
    graphSnapshot.passesInExecutionOrder.push_back(gameViewScoped);

    rg::RenderGraphPassSnapshot sceneViewScoped = MakeComputePass("AtmosphereSkyViewLutPass");
    sceneViewScoped.viewScope = rg::ViewScope::SceneView;
    graphSnapshot.passesInExecutionOrder.push_back(sceneViewScoped);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute Dispatches (Pre-GameView)" group + "RenderOpaque" leaf.

    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u); // NOT two - the SceneView-scoped duplicate must be gone.
    EXPECT_EQ(preGroup.children[0].name, "AtmosphereSkyViewLutPass");
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(snapshot.totalEventCount, 3); // Wrapped pass (parent+child) + RenderOpaque.
}

// Symmetric coverage for the POST-GameView group - models the real
// "AtmosphereAerialPerspectiveCompositePass" duplicate scenario from
// PHASE0_MASTER_STRATEGY.md Section 0.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - the surviving GameView-scoped pass now also owns a real
// "Compute Dispatch" child event; totalEventCount grows from 2 to 3.
TEST(FrameDebuggerSnapshotBuilderTest, SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

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
    ASSERT_EQ(root.children.size(), 2u); // "RenderOpaque" leaf + "Compute Dispatches (Post-GameView)" group.

    const FrameDebuggerEventNode& postGroup = root.children[1];
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u); // NOT two - the SceneView-scoped duplicate must be gone.
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(snapshot.totalEventCount, 3); // RenderOpaque + wrapped pass (parent+child).
}

// Guards against an over-eager fix that accidentally also excludes
// ViewScope::Shared passes (e.g. the real Transmittance/Multi-Scattering LUT
// passes, genuinely computed once per frame, not once per view) - these must
// still appear exactly as before this campaign.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - the surviving Shared-scoped pass now also owns a real "Compute
// Dispatch" child event; totalEventCount grows from 2 to 3.
TEST(FrameDebuggerSnapshotBuilderTest, SharedViewScopedPassStillAppearsNormally)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot shared = MakeComputePass("AtmosphereTransmittanceLutPass");
    shared.viewScope = rg::ViewScope::Shared; // Explicit, though this is also the default.
    graphSnapshot.passesInExecutionOrder.push_back(shared);

    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u);
    const FrameDebuggerEventNode& preGroup = root.children[0];
    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].name, "AtmosphereTransmittanceLutPass");
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");
    EXPECT_EQ(snapshot.totalEventCount, 3);
}

// frame-debugger-6 campaign, PHASE4
// (PHASE4_GAMEVIEW_PER_ENTITY_DRAW_TREE_LEAVES.md, Step 4) - the actual
// user-facing feature: the "RenderOpaque" node now gets one real child leaf
// per real FrameDebuggerDrawRecord captured this frame.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: this is
// "RenderOpaque"'s own untouched per-entity-children mechanism, explicitly
// out of scope for PHASE2's rework (Locked Design Decision #1).
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueNodeGetsOneChildLeafPerDrawRecord)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 1u); // Only "RenderOpaque" - no compute-dispatch groups.

    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[0];
    EXPECT_TRUE(renderOpaqueLeaf.isDrawCall);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 0);
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u); // (a) exactly that many children.

    // (b) each child's name/eventIndex is correct.
    const FrameDebuggerEventNode& terrainLeaf = renderOpaqueLeaf.children[0];
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

    const FrameDebuggerEventNode& cubeLeaf = renderOpaqueLeaf.children[1];
    EXPECT_EQ(cubeLeaf.name, "SmokeTestCube (Entity 3)");
    EXPECT_EQ(cubeLeaf.eventIndex, 2);
    ASSERT_TRUE(cubeLeaf.details.has_value());
    ASSERT_EQ(cubeLeaf.details->textures.size(), 1u);
    EXPECT_EQ(cubeLeaf.details->textures[0].name, "Material Texture");
    EXPECT_EQ(cubeLeaf.details->textures[0].valueLabel, "MaterialTexture abc123");

    // (c) eventIndex stays strictly monotonic increasing: RenderOpaque(0) < terrain(1) < cube(2).
    EXPECT_LT(renderOpaqueLeaf.eventIndex, terrainLeaf.eventIndex);
    EXPECT_LT(terrainLeaf.eventIndex, cubeLeaf.eventIndex);

    // (d) totalEventCount correctly includes the new children.
    EXPECT_EQ(snapshot.totalEventCount, 3);
}

// eventIndex stays strictly monotonic across pre-group -> RenderOpaque ->
// RenderOpaque's children -> post-group, extending
// EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView one level
// deeper.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - PreA/PostA now each also own a real "Compute Dispatch" child
// event; every eventIndex from "RenderOpaque" onward shifts accordingly.
TEST(FrameDebuggerSnapshotBuilderTest, EventIndexIsMonotonicAcrossPreGameViewRenderOpaqueChildrenAndPostGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u);
    ASSERT_EQ(postGroup.children.size(), 1u);

    // PreA parent(0) + its own child "Compute Dispatch"(1), RenderOpaque(2),
    // its two entity children(3, 4), PostA parent(5) + its own child(6).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(renderOpaqueLeaf.children[0].eventIndex, 3);
    EXPECT_EQ(renderOpaqueLeaf.children[1].eventIndex, 4);
    EXPECT_EQ(postGroup.children[0].eventIndex, 5);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 6);
    EXPECT_EQ(snapshot.totalEventCount, 7);
}

// No draw records captured this frame -> "RenderOpaque" keeps zero children,
// exactly like every earlier campaign's behavior (an honestly empty Game
// View, e.g. before the very first mesh entity is spawned).
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED, and explicitly
// re-checked: this is the ONE remaining real-world case where a node can
// still have zero children after this whole campaign ("RenderOpaque" itself
// with no draw records) - every OTHER pass leaf now always gets exactly one
// child, by design (Step 3.5's own "gap already covered" note).
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueNodeHasNoChildrenWhenNoDrawRecordsCaptured)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    EXPECT_TRUE(renderOpaqueLeaf.children.empty());
    EXPECT_EQ(snapshot.totalEventCount, 1);
}

// REQUIRED regression test (found by this campaign's own double-check pass,
// not a shipped bug, PHASE4's own Step 4) - a per-entity draw-record child
// leaf's own `details->passName` must NEVER be the literal string
// "RenderOpaque" - only the real "RenderOpaque" pass leaf itself may ever
// carry that exact value.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"-only
// fixture.
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueDrawRecordLeafPassNameIsNeverLiterallyRenderOpaque)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u);
    ASSERT_TRUE(renderOpaqueLeaf.children[0].details.has_value());
    EXPECT_NE(renderOpaqueLeaf.children[0].details->passName, "RenderOpaque");
    // Also confirm the real "RenderOpaque" pass leaf itself is unaffected -
    // it must still carry the literal value, unchanged.
    ASSERT_TRUE(renderOpaqueLeaf.details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.details->passName, "RenderOpaque");
}

// NEW - PHASE2's own Step 3.6 item (c): a culled compute pass must never
// appear in either group, on EITHER side of "RenderOpaque" - a culled pass
// did not really run this frame, so showing it as if it did would be dishonest.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - both surviving compute passes now also own a real "Compute
// Dispatch" child event; totalEventCount grows from 3 to 5. A culled pass
// still never consumes an index at all, unaffected.
TEST(FrameDebuggerSnapshotBuilderTest, CulledComputePassIsExcludedFromEitherComputeDispatchGroup)
{
    rg::RenderGraphSnapshot graphSnapshot;

    rg::RenderGraphPassSnapshot culledPre = MakeComputePass("CulledPreComputePass");
    culledPre.isCulled = true;
    graphSnapshot.passesInExecutionOrder.push_back(culledPre);

    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SurvivingPreComputePass"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
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
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");

    const FrameDebuggerEventNode& postGroup = root.children[2];
    ASSERT_EQ(postGroup.children.size(), 1u); // The culled post pass never appears.
    EXPECT_EQ(postGroup.children[0].name, "SurvivingPostComputePass");
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");

    // SurvivingPre parent(0) + child(1), RenderOpaque(2), SurvivingPost
    // parent(3) + child(4) - only 5 real events; a culled pass consumes
    // NO index at all, wrapped or not.
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(root.children[1].eventIndex, 2);
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5); // Only 5 real events (2 wrapped passes + RenderOpaque).
}

// NEW - PHASE2's own Step 3.6 item (d): every read/write row is labeled by
// its own real ResourceKind (PHASE1's readKinds/writeKinds), covering all
// three kinds for BOTH reads and writes on one pass.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: only reads
// `.details->textures` on the PASS-LEVEL (parent) node - no
// children.empty()/index/totalEventCount assertion exists anywhere in this
// test.
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
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

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
    // task_manager/frame-debugger-9 campaign, PHASE3 - each render-graph row
    // now also carries its own real `kind` PLUS `isRenderGraphResource ==
    // true` (Step 3.8's own required coverage: a Buffer row IS a real
    // render-graph resource too, just never image-previewable - `kind` is
    // what actually gates the "View" button, not this flag alone).
    EXPECT_EQ(details.textures[0].kind, rg::ResourceKind::Texture);
    EXPECT_TRUE(details.textures[0].isRenderGraphResource);
    EXPECT_EQ(details.textures[1].name, "Read Buffer");
    EXPECT_EQ(details.textures[1].valueLabel, "SomeBuffer");
    EXPECT_EQ(details.textures[1].kind, rg::ResourceKind::Buffer);
    EXPECT_TRUE(details.textures[1].isRenderGraphResource);
    EXPECT_EQ(details.textures[2].name, "Read Volume Texture");
    EXPECT_EQ(details.textures[2].valueLabel, "SomeVolume");
    EXPECT_EQ(details.textures[2].kind, rg::ResourceKind::VolumeTexture);
    EXPECT_TRUE(details.textures[2].isRenderGraphResource);
    EXPECT_EQ(details.textures[3].name, "Write Texture");
    EXPECT_EQ(details.textures[3].valueLabel, "OutputTexture");
    EXPECT_EQ(details.textures[3].kind, rg::ResourceKind::Texture);
    EXPECT_TRUE(details.textures[3].isRenderGraphResource);
    EXPECT_EQ(details.textures[4].name, "Write Buffer");
    EXPECT_EQ(details.textures[4].valueLabel, "OutputBuffer");
    EXPECT_EQ(details.textures[4].kind, rg::ResourceKind::Buffer);
    EXPECT_TRUE(details.textures[4].isRenderGraphResource);
    EXPECT_EQ(details.textures[5].name, "Write Volume Texture");
    EXPECT_EQ(details.textures[5].valueLabel, "OutputVolume");
    EXPECT_EQ(details.textures[5].kind, rg::ResourceKind::VolumeTexture);
    EXPECT_TRUE(details.textures[5].isRenderGraphResource);
}

// NEW - PHASE2's own Step 3.6 item (e): eventIndex values across the WHOLE
// tree are monotonically increasing in true chronological order - every
// pre-GameView leaf's index < "RenderOpaque"'s own index < every post-GameView
// leaf's index. Regression coverage for BuildRealFrameDebuggerSnapshot()'s
// own documented "do not reorder these blocks" ordering caveat.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - every compute pass now also owns a real "Compute Dispatch" child
// event; every eventIndex after the first pre-view pass shifts accordingly.
TEST(FrameDebuggerSnapshotBuilderTest, EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreB"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostB"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 3u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    const FrameDebuggerEventNode& postGroup = root.children[2];

    ASSERT_EQ(preGroup.children.size(), 2u);
    ASSERT_EQ(postGroup.children.size(), 2u);

    // PreA parent(0)+child(1), PreB parent(2)+child(3), RenderOpaque(4),
    // PostA parent(5)+child(6), PostB parent(7)+child(8).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(preGroup.children[1].eventIndex, 2);
    ASSERT_EQ(preGroup.children[1].children.size(), 1u);
    EXPECT_EQ(preGroup.children[1].children[0].eventIndex, 3);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 4);
    EXPECT_EQ(postGroup.children[0].eventIndex, 5);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 6);
    EXPECT_EQ(postGroup.children[1].eventIndex, 7);
    ASSERT_EQ(postGroup.children[1].children.size(), 1u);
    EXPECT_EQ(postGroup.children[1].children[0].eventIndex, 8);
    EXPECT_EQ(snapshot.totalEventCount, 9);
}

// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.5) - the old
// `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`
// tests (frame-debugger-5 campaign) were DELETED along with the functions
// they covered (PHASE0's Locked Design Decision #3). Replaced below with
// coverage proving `BuildRealFrameDebuggerSnapshot()` assigns the right
// `stepPreviewKind`/`stepPreviewIndex` to a Pre-GameView leaf, the
// "RenderOpaque" leaf, each per-object child (in order), a Post-GameView-
// before-composite leaf, and a Post-GameView-at/after-composite leaf.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: only reads
// `.details->stepPreviewKind` on the pass-level node - no index/child-count/
// totalEventCount assertion exists.

TEST(FrameDebuggerSnapshotBuilderTest, PreGameViewLeafGetsNotYetDrawnStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SkinPass_A"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& preGroup = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(preGroup.children.size(), 1u);
    ASSERT_TRUE(preGroup.children[0].details.has_value());
    EXPECT_EQ(preGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::NotYetDrawn);
}

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"-only
// fixture.
TEST(FrameDebuggerSnapshotBuilderTest, RenderOpaqueLeafGetsPreCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_TRUE(renderOpaqueLeaf.details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);
}

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: "RenderOpaque"'s own
// per-entity children only.
TEST(FrameDebuggerSnapshotBuilderTest, PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 2u);

    ASSERT_TRUE(renderOpaqueLeaf.children[0].details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PerObjectStep);
    EXPECT_EQ(renderOpaqueLeaf.children[0].details->stepPreviewIndex, 0);

    ASSERT_TRUE(renderOpaqueLeaf.children[1].details.has_value());
    EXPECT_EQ(renderOpaqueLeaf.children[1].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PerObjectStep);
    EXPECT_EQ(renderOpaqueLeaf.children[1].details->stepPreviewIndex, 1);
}

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: only
// postGroup.children.size() (a GROUP sibling count, unaffected) and
// `.details->stepPreviewKind` on the pass-level node.
TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
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

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: only
// postGroup.children.size() (sibling count, unaffected) and
// `.details->stepPreviewKind` on each pass-level node.
TEST(FrameDebuggerSnapshotBuilderTest, CompositePassLeafItselfGetsPostCompositeStepPreviewKind)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

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

// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: only
// postGroup.children.size() (sibling count, unaffected) and
// `.details->stepPreviewKind` on the pass-level node.
TEST(FrameDebuggerSnapshotBuilderTest, PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives)
{
    // No pass anywhere writes "GameViewComposited" this frame (e.g. a
    // capture taken before the atmosphere composite pass has ever run this
    // session) - FindPostGameViewCompositePassExecutionIndex() returns -1,
    // and every Post-GameView leaf falls into the PostComposite default
    // catch-all bucket (Step 2's own "or nothing selected" wording).
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomePostGameViewPassWithNoCompositeWrite"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& postGroup = snapshot.rootNodes[0].children[1];
    ASSERT_EQ(postGroup.children.size(), 1u);
    ASSERT_TRUE(postGroup.children[0].details.has_value());
    EXPECT_EQ(postGroup.children[0].details->stepPreviewKind, FrameDebuggerStepPreviewKind::PostComposite);
}

// Render Pass campaign (task_manager/render-pass-1), PHASE4
// (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.3) - REPLACES the old
// frame-debugger-8 campaign's "SkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit"
// test (which fed a `RecordSkyBackgroundDraw()` DrawRecord, now REMOVED - see
// FrameDebuggerCaptureTests.cpp). "DrawSkyBackground" is now a real, separate
// pass in the fixture itself, discovered by the "view region" walk - proves
// PHASE1-PHASE4 all compose correctly with the PRE-EXISTING pre/post-GameView
// compute-dispatch split (frame-debugger-5/frame-debugger-6 campaigns) with
// zero regressions.
//
// REWRITTEN AGAIN (Frame Debugger Pass-Ownership campaign,
// task_manager/render-pass-2, PHASE3) - every one of these five pass-level
// leaves now also owns a real child event of its own ("Compute Dispatch" for
// PreA/PostA, "Draw Quad" for DrawSkyBackground - PHASE1/PHASE2), doubling
// every one of their own index consumption; "RenderOpaque" itself and its
// own per-entity child are UNTOUCHED, per Locked Design Decision #1. The
// fixture also now explicitly tags "DrawSkyBackground" with
// RenderPassDrawKind::DrawQuad (GraphicsChildEventLabelFor() reads this
// field purely structurally, never the pass's own name - MakePass() never
// infers it).
TEST(FrameDebuggerSnapshotBuilderTest, DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PreA"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("PostA"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 4u);

    const FrameDebuggerEventNode& preGroup = root.children[0];
    const FrameDebuggerEventNode& renderOpaqueLeaf = root.children[1];
    const FrameDebuggerEventNode& skyLeaf = root.children[2];
    const FrameDebuggerEventNode& postGroup = root.children[3];

    EXPECT_EQ(preGroup.name, "Compute Dispatches (Pre-GameView)");
    ASSERT_EQ(preGroup.children.size(), 1u);

    EXPECT_EQ(renderOpaqueLeaf.name, "RenderOpaque");
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u); // Only the entity - sky is its own sibling leaf now.
    const FrameDebuggerEventNode& entityLeaf = renderOpaqueLeaf.children[0];
    EXPECT_EQ(entityLeaf.name, "terrain (Entity 2)");

    EXPECT_EQ(skyLeaf.name, "DrawSkyBackground");
    EXPECT_TRUE(skyLeaf.isDrawCall);
    ASSERT_EQ(skyLeaf.children.size(), 1u); // NOW owns exactly one real child event.
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_EQ(skyLeaf.details->passName, "DrawSkyBackground");
    EXPECT_EQ(skyLeaf.details->shaderName, "DrawSkyBackground");
    EXPECT_EQ(skyLeaf.details->zTest, "Equal");
    EXPECT_EQ(skyLeaf.details->zWrite, "Off");
    EXPECT_EQ(skyLeaf.details->stepPreviewKind, FrameDebuggerStepPreviewKind::PreComposite);

    const FrameDebuggerEventNode& skyChild = skyLeaf.children[0];
    EXPECT_EQ(skyChild.name, "Draw Quad"); // DrawSkyBackground is tagged RenderPassDrawKind::DrawQuad (PHASE1).
    ASSERT_TRUE(skyChild.details.has_value());
    EXPECT_EQ(skyChild.details->eventLabel, "Draw Quad");
    EXPECT_EQ(skyChild.details->passName, "DrawSkyBackground"); // Same owning pass.

    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].name, "Compute Dispatch");

    ASSERT_EQ(preGroup.children[0].children.size(), 1u);
    EXPECT_EQ(preGroup.children[0].children[0].name, "Compute Dispatch");

    // eventIndex strictly monotonic, now 8 total leaves (each wrapped pass
    // contributes one parent + one child).
    EXPECT_EQ(preGroup.children[0].eventIndex, 0);
    EXPECT_EQ(preGroup.children[0].children[0].eventIndex, 1);
    EXPECT_EQ(renderOpaqueLeaf.eventIndex, 2);
    EXPECT_EQ(entityLeaf.eventIndex, 3);
    EXPECT_EQ(skyLeaf.eventIndex, 4);
    EXPECT_EQ(skyChild.eventIndex, 5);
    EXPECT_EQ(postGroup.children[0].eventIndex, 6);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 7);
    EXPECT_EQ(snapshot.totalEventCount, 8);
}

// NEW - Render Pass campaign, PHASE4, Step 3.5 - the concrete proof the
// "RenderTransparent" mechanism is genuinely generic, not just "generic in
// theory": since that pass never actually declares itself in the graph
// today, a fixture with only "RenderOpaque" + "DrawSkyBackground" (and no
// "RenderTransparent" anywhere) produces EXACTLY those two leaves in the
// view region - no third, empty/fake leaf ever appears.
//
// EXTENDED (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - proves the actual fix itself: "DrawSkyBackground" now owns
// exactly one real child event, "Draw Quad" (PHASE1/PHASE2). The fixture
// explicitly tags it RenderPassDrawKind::DrawQuad - MakePass() never infers
// drawKind from a pass's own name.
TEST(FrameDebuggerSnapshotBuilderTest, ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // Exactly "RenderOpaque" + "DrawSkyBackground" - nothing else.
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "DrawSkyBackground");

    ASSERT_EQ(root.children[1].children.size(), 1u);
    EXPECT_EQ(root.children[1].children[0].name, "Draw Quad");
}

// NEW, REQUIRED by this phase's own 3.3/3.3b fix - Render Pass campaign,
// PHASE4 - a Graphics-kind, ViewScope::GameView, RenderPassCategory::Debug
// pass positioned in passesInExecutionOrder BETWEEN "DrawSkyBackground" and
// the eventual Post-GameView compute pass (mirroring
// AddFrameDebuggerReplayPasses()'s own real, confirmed position) must
// produce ZERO extra leaves in the view region (still exactly
// "RenderOpaque"/"DrawSkyBackground", nothing else) - this is the concrete
// regression test proving the Frame Debugger's own internal replay passes
// can never leak into the tree.
//
// REWRITTEN (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
// PHASE3) - "DrawSkyBackground" and the composite pass now each also own a
// real child event of their own; eventIndex/totalEventCount updated
// accordingly. The fixture also now explicitly tags "DrawSkyBackground"
// RenderPassDrawKind::DrawQuad.
TEST(FrameDebuggerSnapshotBuilderTest, DebugCategoryGraphicsPassInsideViewRegionProducesNoExtraLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);
    graphSnapshot.passesInExecutionOrder.push_back(
        MakeGraphicsPassWithCategory("FrameDebuggerReplayStep0", rg::RenderPassCategory::Debug));
    graphSnapshot.passesInExecutionOrder.push_back(
        MakeGraphicsPassWithCategory("FrameDebuggerReplayStep1", rg::RenderPassCategory::Debug));

    rg::RenderGraphPassSnapshot composite = MakeComputePass("AtmosphereAerialPerspectiveCompositePass");
    composite.writeNames.push_back("GameViewComposited");
    composite.writeKinds.push_back(rg::ResourceKind::Texture);
    graphSnapshot.passesInExecutionOrder.push_back(composite);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // "RenderOpaque" + "DrawSkyBackground" + "Compute Dispatches
    // (Post-GameView)" - the two Debug-category replay passes produce NO
    // leaves of their own anywhere.
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_EQ(root.children[0].name, "RenderOpaque");
    EXPECT_EQ(root.children[1].name, "DrawSkyBackground");
    const FrameDebuggerEventNode& postGroup = root.children[2];
    EXPECT_EQ(postGroup.name, "Compute Dispatches (Post-GameView)");
    ASSERT_EQ(postGroup.children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].name, "AtmosphereAerialPerspectiveCompositePass");

    // eventIndex is monotonic and SKIPS the two Debug-category passes
    // entirely (they never get an eventIndex at all): RenderOpaque(0),
    // DrawSkyBackground(1) + its own child "Draw Quad"(2), composite(3) +
    // its own child "Compute Dispatch"(4).
    EXPECT_EQ(root.children[0].eventIndex, 0);
    EXPECT_EQ(root.children[1].eventIndex, 1);
    ASSERT_EQ(root.children[1].children.size(), 1u);
    EXPECT_EQ(root.children[1].children[0].eventIndex, 2);
    EXPECT_EQ(root.children[1].children[0].name, "Draw Quad");
    EXPECT_EQ(postGroup.children[0].eventIndex, 3);
    ASSERT_EQ(postGroup.children[0].children.size(), 1u);
    EXPECT_EQ(postGroup.children[0].children[0].eventIndex, 4);
    EXPECT_EQ(snapshot.totalEventCount, 5);
}

// task_manager/frame-debugger-9 campaign, PHASE3 (Step 3.8) - a
// "Material Texture" row (BuildRenderOpaqueDrawRecordLeaf()'s per-entity draw
// leaf) must have `isRenderGraphResource == false` - it has no render-graph
// registry entry at all (Locked Design Decision #1, PHASE0_MASTER_STRATEGY.md),
// so Panels/FrameDebuggerPanel.cpp's ShaderProperties tab must never draw a
// "View" button next to it.
//
// render-pass-2 campaign, PHASE3 - CONFIRMED UNCHANGED: about a "Material
// Texture" row on "RenderOpaque"'s own per-entity children, unrelated to
// this campaign.
TEST(FrameDebuggerSnapshotBuilderTest, MaterialTextureRowIsNeverARenderGraphResource)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "MaterialTexture abc123", 12);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& renderOpaqueLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(renderOpaqueLeaf.children.size(), 1u);
    ASSERT_TRUE(renderOpaqueLeaf.children[0].details.has_value());
    ASSERT_EQ(renderOpaqueLeaf.children[0].details->textures.size(), 1u);
    EXPECT_EQ(renderOpaqueLeaf.children[0].details->textures[0].name, "Material Texture");
    EXPECT_FALSE(renderOpaqueLeaf.children[0].details->textures[0].isRenderGraphResource);
}

// ---------------------------------------------------------------------------
// NEW TESTS - Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE3, Step 3.5 - cover the fix itself (WrapPassWithOwnedChildEvent()/
// GraphicsChildEventLabelFor()), not just non-regression of the 31
// pre-existing tests above.
// ---------------------------------------------------------------------------

// Step 3.5 test 1, EXTENDED per this phase's own "gap found during review"
// note - proves dual-selectability (Locked Design Decision #2) end-to-end
// through the real FindEventDetailsByIndex() lookup function, for BOTH a
// wrapped GRAPHICS-kind pass ("DrawSkyBackground") AND a wrapped
// COMPUTE-kind pass ("SomeComputePass") in the SAME fixture - the one
// remaining un-exercised combination this campaign's own fix touches.
TEST(FrameDebuggerSnapshotBuilderTest, WrappedPassLevelNodeAndItsChildAreBothIndependentlySelectable)
{
    rg::RenderGraphSnapshot graphSnapshot;
    graphSnapshot.passesInExecutionOrder.push_back(MakeComputePass("SomeComputePass"));
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
    rg::RenderGraphPassSnapshot skyPass = MakePass("DrawSkyBackground");
    skyPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
    graphSnapshot.passesInExecutionOrder.push_back(skyPass);

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    // SomeComputePass parent(0) + child(1), RenderOpaque(2), DrawSkyBackground
    // parent(3) + child(4).
    ASSERT_EQ(snapshot.totalEventCount, 5);

    // The wrapped GRAPHICS-kind pass ("DrawSkyBackground") - both its parent
    // row and its owned child row are independently resolvable via the real
    // lookup function, not just by inspecting the tree directly.
    const std::optional<FrameDebuggerEventDetails> skyParentDetails = FindEventDetailsByIndex(snapshot, 3);
    ASSERT_TRUE(skyParentDetails.has_value());
    EXPECT_EQ(skyParentDetails->passName, "DrawSkyBackground");

    const std::optional<FrameDebuggerEventDetails> skyChildDetails = FindEventDetailsByIndex(snapshot, 4);
    ASSERT_TRUE(skyChildDetails.has_value());
    EXPECT_EQ(skyChildDetails->passName, "DrawSkyBackground");
    EXPECT_EQ(skyChildDetails->eventLabel, "Draw Quad");

    // Found during this phase's own review (PHASE3 Step 3.5's gap) -
    // WrapPassWithOwnedChildEvent() is the exact same generic mechanism for a
    // wrapped COMPUTE-kind pass too - both its parent row and its owned
    // "Compute Dispatch" child row must also be independently resolvable.
    const std::optional<FrameDebuggerEventDetails> computeParentDetails = FindEventDetailsByIndex(snapshot, 0);
    ASSERT_TRUE(computeParentDetails.has_value());
    EXPECT_EQ(computeParentDetails->passName, "SomeComputePass");

    const std::optional<FrameDebuggerEventDetails> computeChildDetails = FindEventDetailsByIndex(snapshot, 1);
    ASSERT_TRUE(computeChildDetails.has_value());
    EXPECT_EQ(computeChildDetails->passName, "SomeComputePass");
    EXPECT_EQ(computeChildDetails->eventLabel, "Compute Dispatch");
}

// Step 3.5 test 2 - three synthetic fixtures, each with "RenderOpaque" + one
// other Graphics-kind pass in the view region, each tagged a DIFFERENT
// rg::RenderPassDrawKind directly on the RenderGraphPassSnapshot fixture -
// the owned child's own label must match exactly, derived PURELY from that
// structural tag, never from the pass's own name. This is the ONE test in
// this whole campaign that actually exercises the `Blit` scaffold value
// end-to-end, even though no real pass uses it yet.
TEST(FrameDebuggerSnapshotBuilderTest, GraphicsChildEventLabelMatchesRenderPassDrawKind)
{
    // DrawQuad.
    {
        rg::RenderGraphSnapshot graphSnapshot;
        graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
        rg::RenderGraphPassSnapshot quadPass = MakePass("SyntheticQuadPass");
        quadPass.drawKind = rg::RenderPassDrawKind::DrawQuad;
        graphSnapshot.passesInExecutionOrder.push_back(quadPass);

        const FrameDebuggerCaptureContext capture;
        const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

        ASSERT_EQ(snapshot.rootNodes.size(), 1u);
        ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
        const FrameDebuggerEventNode& passLeaf = snapshot.rootNodes[0].children[1];
        EXPECT_EQ(passLeaf.name, "SyntheticQuadPass");
        ASSERT_EQ(passLeaf.children.size(), 1u);
        EXPECT_EQ(passLeaf.children[0].name, "Draw Quad");
    }

    // DrawMesh.
    {
        rg::RenderGraphSnapshot graphSnapshot;
        graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
        rg::RenderGraphPassSnapshot meshPass = MakePass("SyntheticMeshPass");
        meshPass.drawKind = rg::RenderPassDrawKind::DrawMesh;
        graphSnapshot.passesInExecutionOrder.push_back(meshPass);

        const FrameDebuggerCaptureContext capture;
        const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

        ASSERT_EQ(snapshot.rootNodes.size(), 1u);
        ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
        const FrameDebuggerEventNode& passLeaf = snapshot.rootNodes[0].children[1];
        EXPECT_EQ(passLeaf.name, "SyntheticMeshPass");
        ASSERT_EQ(passLeaf.children.size(), 1u);
        EXPECT_EQ(passLeaf.children[0].name, "Draw Mesh");
    }

    // Blit.
    {
        rg::RenderGraphSnapshot graphSnapshot;
        graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));
        rg::RenderGraphPassSnapshot blitPass = MakePass("SyntheticBlitPass");
        blitPass.drawKind = rg::RenderPassDrawKind::Blit;
        graphSnapshot.passesInExecutionOrder.push_back(blitPass);

        const FrameDebuggerCaptureContext capture;
        const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

        ASSERT_EQ(snapshot.rootNodes.size(), 1u);
        ASSERT_EQ(snapshot.rootNodes[0].children.size(), 2u);
        const FrameDebuggerEventNode& passLeaf = snapshot.rootNodes[0].children[1];
        EXPECT_EQ(passLeaf.name, "SyntheticBlitPass");
        ASSERT_EQ(passLeaf.children.size(), 1u);
        EXPECT_EQ(passLeaf.children[0].name, "Blit");
    }
}

// Step 3.5 test 3 - extends
// OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup's own
// fixture with the ONE assertion that test deliberately left as merely
// OPTIONAL: a "Compute LUT" sub-pass also owns a real "Compute Dispatch"
// child event, exactly like every other wrapped compute pass.
TEST(FrameDebuggerSnapshotBuilderTest, ComputeLutSubPassAlsoOwnsAComputeDispatchChild)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot lutPass = MakeComputePass("AtmosphereTransmittanceLutPass");
    lutPass.category = rg::RenderPassCategory::AtmosphereLut;
    graphSnapshot.passesInExecutionOrder.push_back(lutPass);
    graphSnapshot.passesInExecutionOrder.push_back(MakePass("RenderOpaque"));

    const FrameDebuggerCaptureContext capture;
    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "Compute LUT" group + "RenderOpaque" leaf, nothing else.
    const FrameDebuggerEventNode& lutGroup = root.children[0];
    EXPECT_EQ(lutGroup.name, "Compute LUT");
    ASSERT_EQ(lutGroup.children.size(), 1u);
    ASSERT_EQ(lutGroup.children[0].children.size(), 1u);
    EXPECT_EQ(lutGroup.children[0].children[0].name, "Compute Dispatch");
}

// ---------------------------------------------------------------------------
// NEW TEST - render-pass-3 campaign, PHASE4
// (PHASE4_FRAME_DEBUGGER_EVENT_PIVOT_FIX.md, Step 3.3) - the actual
// regression proof for the fix itself: a pass named something OTHER than
// "RenderOpaque" (proving the lookup is genuinely name-free), tagged
// RenderPassEvent::Opaques, is correctly found as the view-region pivot -
// its own real stats/per-entity draw records flow into the resulting leaf
// exactly the same way "RenderOpaque" itself always has, and a real pass
// LITERALLY named "RenderOpaque" sitting BEFORE it in execution order (but
// tagged BELOW Opaques, i.e. NOT itself eligible to be the pivot) is
// correctly skipped/ignored - proving this is genuinely a structural,
// order+tag-driven lookup, never a name comparison in either direction.
TEST(FrameDebuggerSnapshotBuilderTest, ViewRegionPivotIsFoundStructurallyEvenWhenNotNamedRenderOpaque)
{
    rg::RenderGraphSnapshot graphSnapshot;

    // A red herring: literally named "RenderOpaque", but tagged BELOW
    // Opaques - must NOT be picked as the pivot, proving the lookup never
    // special-cases this literal string either.
    rg::RenderGraphPassSnapshot redHerring = MakePass("RenderOpaque");
    redHerring.renderPassEvent = rg::RenderPassEvent::BeforeEverything;
    graphSnapshot.passesInExecutionOrder.push_back(redHerring);

    // The REAL pivot - a differently-named pass, tagged Opaques (the value
    // FindViewRegionPivot() actually keys off of).
    rg::RenderGraphPassSnapshot renamedOpaquePass = MakePass("MainOpaqueDrawPass");
    renamedOpaquePass.renderPassEvent = rg::RenderPassEvent::Opaques;
    renamedOpaquePass.stats.drawStats.drawCallCount = 3;
    renamedOpaquePass.stats.drawStats.triangleCount = 99;
    graphSnapshot.passesInExecutionOrder.push_back(renamedOpaquePass);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(5, 0, "renamed-pivot-proof", "Mesh.vert/Mesh.frag (PositionNormal)", "", 42);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    // Only ONE leaf in the view region - the red herring never produced a
    // group/leaf of its own (it is Graphics-kind, sits strictly before the
    // real pivot, and is invisible to both the pre-view compute loop and the
    // view-region walk, exactly like any other pre-pivot Graphics pass).
    ASSERT_EQ(root.children.size(), 1u);

    const FrameDebuggerEventNode& leaf = root.children[0];
    EXPECT_TRUE(leaf.isDrawCall);
    ASSERT_TRUE(leaf.details.has_value());
    // BuildRenderOpaqueLeaf() always stamps the literal "RenderOpaque"
    // display label/passName regardless of the underlying pass's own real
    // name - this is pre-existing, unchanged behavior (see that function's
    // own doc comment) - what this test actually proves is that the REAL,
    // differently-named pass's own DATA (draw stats, per-entity children)
    // is what got used, not the red herring's.
    bool foundDrawStats = false;
    for (const FrameDebuggerVectorProperty& vec : leaf.details->vectors) {
        if (vec.name == "Draw Stats (Calls, Tris)") {
            foundDrawStats = true;
            EXPECT_FLOAT_EQ(vec.x, 3.0f);
            EXPECT_FLOAT_EQ(vec.y, 99.0f);
        }
    }
    EXPECT_TRUE(foundDrawStats);

    ASSERT_EQ(leaf.children.size(), 1u);
    EXPECT_EQ(leaf.children[0].name, "renamed-pivot-proof (Entity 5)");

    EXPECT_EQ(snapshot.totalEventCount, 2); // The pivot leaf + its one real child - the red herring consumes zero.
}

} // namespace
} // namespace gte
