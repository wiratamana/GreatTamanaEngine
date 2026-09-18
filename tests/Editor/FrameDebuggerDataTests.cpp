// Unit tests for the Editor's "Frame Debugger" window's pure data model
// (src/Editor/FrameDebuggerData.h) - deliberately pure (no ImGui/Renderer/
// live-Vulkan-device involved at all), so it's Tier-1-testable exactly like
// JobsPanelData.h/ProfilerPanelData.h/MemoryPanelData.h despite living under
// src/Editor/ - see AGENTS.md, "Testability & Regression Safety". Only
// built when GTE_ENABLE_EDITOR is ON, since FrameDebuggerData.h/.cpp are
// only compiled into gte_core then (see the root CMakeLists.txt's "Editor
// Module Structure") - the same "zero-touch when off" rule already applied
// to tests/Editor/JobsPanelDataTests.cpp.
//
// task_manager/frame-debugger-2 campaign, PHASE1.

#include "Editor/FrameDebuggerData.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace gte {
namespace {

TEST(FrameDebuggerDataTest, BuildPlaceholderFrameDebuggerSnapshotTest)
{
    const FrameDebuggerSnapshot snapshot = BuildPlaceholderFrameDebuggerSnapshot();
    EXPECT_TRUE(snapshot.rootNodes.empty());
    EXPECT_EQ(snapshot.totalEventCount, 0);
    EXPECT_EQ(snapshot.renderTarget.name, "<No name>");
}

TEST(FrameDebuggerDataTest, FormatFrameStepperLabelTest)
{
    EXPECT_EQ(FormatFrameStepperLabel(0, 0), "0 of 0");
    EXPECT_EQ(FormatFrameStepperLabel(-1, 0), "0 of 0");
    EXPECT_EQ(FormatFrameStepperLabel(0, 2117), "1 of 2117");
    EXPECT_EQ(FormatFrameStepperLabel(2116, 2117), "2117 of 2117");
}

TEST(FrameDebuggerDataTest, ClampSelectedEventIndexTest)
{
    EXPECT_EQ(ClampSelectedEventIndex(-1, 0), -1);
    EXPECT_EQ(ClampSelectedEventIndex(5, 0), -1);
    EXPECT_EQ(ClampSelectedEventIndex(-1, 10), 0);
    EXPECT_EQ(ClampSelectedEventIndex(999, 10), 9);
    EXPECT_EQ(ClampSelectedEventIndex(4, 10), 4); // Identity for an already-valid index.
}

// task_manager/frame-debugger-9 campaign, PHASE1
// (PHASE1_ASPECT_RATIO_CORRECT_PREVIEW.md, Step 3.4) - the new pure
// aspect-fit helper (Locked Design Decision #7, PHASE0_MASTER_STRATEGY.md).
TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectExactAspectMatchFillsWithNoOffset)
{
    // A 800x400 box (2:1) with an 800x400-aspect (2:1) source -> fills
    // exactly, no offset either axis.
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(800.0f, 400.0f, 1600.0f, 800.0f);
    EXPECT_FLOAT_EQ(fit.width, 800.0f);
    EXPECT_FLOAT_EQ(fit.height, 400.0f);
    EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectWiderSourceLetterboxesTopAndBottom)
{
    // A square 400x400 box with a 800x200 (4:1, much wider) source ->
    // width-constrained: width fills exactly, height is smaller, centered
    // vertically (black bars top/bottom).
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(400.0f, 400.0f, 800.0f, 200.0f);
    EXPECT_FLOAT_EQ(fit.width, 400.0f);
    EXPECT_LT(fit.height, 400.0f);
    EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
    EXPECT_GT(fit.offsetY, 0.0f);
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectTallerSourcePillarboxesLeftAndRight)
{
    // A square 400x400 box with a 200x800 (1:4, much taller) source ->
    // height-constrained: height fills exactly, width is smaller, centered
    // horizontally (black bars left/right).
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(400.0f, 400.0f, 200.0f, 800.0f);
    EXPECT_FLOAT_EQ(fit.height, 400.0f);
    EXPECT_LT(fit.width, 400.0f);
    EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
    EXPECT_GT(fit.offsetX, 0.0f);
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectDegenerateInputFillsAtOrigin)
{
    // Any non-positive input -> safe "fill the box at (0, 0)" fallback,
    // never NaN/negative.
    {
        const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(0.0f, 400.0f, 100.0f, 100.0f);
        EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
        EXPECT_FLOAT_EQ(fit.width, 0.0f);
        EXPECT_FLOAT_EQ(fit.height, 400.0f);
    }
    {
        const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(400.0f, 400.0f, -1.0f, 100.0f);
        EXPECT_FLOAT_EQ(fit.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
        EXPECT_FLOAT_EQ(fit.width, 400.0f);
        EXPECT_FLOAT_EQ(fit.height, 400.0f);
    }
}

TEST(FrameDebuggerDataTest, ComputeAspectFitImageRectRealWorldCase417x333In800x400Box)
{
    // This campaign's own reference screenshot's reported resolution
    // (417x333, aspect ~1.2523) inside an 800x400 box (aspect 2.0) - source
    // is relatively TALLER than the box -> height-constrained (pillarbox).
    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(800.0f, 400.0f, 417.0f, 333.0f);
    const float expectedWidth = 400.0f * (417.0f / 333.0f); // ~500.9009
    EXPECT_FLOAT_EQ(fit.height, 400.0f);
    EXPECT_NEAR(fit.width, expectedWidth, 0.01f);
    EXPECT_FLOAT_EQ(fit.offsetY, 0.0f);
    EXPECT_NEAR(fit.offsetX, (800.0f - expectedWidth) * 0.5f, 0.01f);
}

// task_manager/frame-debugger-3 campaign, PHASE4
// (PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md) - the NEW Frame-History
// mini-toolbar's own label formatter, a completely separate axis from
// FormatFrameStepperLabelTest above (Locked Design Decision #4).
TEST(FrameDebuggerDataTest, FormatFrameHistoryLabelTest)
{
    EXPECT_EQ(FormatFrameHistoryLabel(0, 0), "Frame 0 of 0");
    EXPECT_EQ(FormatFrameHistoryLabel(-1, 0), "Frame 0 of 0");
    EXPECT_EQ(FormatFrameHistoryLabel(0, 1), "Frame 1 of 1");
    EXPECT_EQ(FormatFrameHistoryLabel(2, 8), "Frame 3 of 8");
    EXPECT_EQ(FormatFrameHistoryLabel(7, 8), "Frame 8 of 8");
}

TEST(FrameDebuggerDataTest, FindEventDetailsByIndexTest)
{
    // Hand-build a small, synthetic, non-empty FrameDebuggerSnapshot - the
    // ONE place in this whole campaign a non-empty tree is ever
    // constructed - purely so the recursive lookup logic itself is proven
    // correct ahead of any future real capture code depending on it.
    FrameDebuggerEventDetails leafDetails;
    leafDetails.eventIndex = 3;
    leafDetails.shaderName = "Standard/DistinguishableTestShader";

    FrameDebuggerEventNode leaf;
    leaf.name = "Draw Mesh Foo";
    leaf.isDrawCall = true;
    leaf.eventIndex = 3;
    leaf.details = leafDetails;

    FrameDebuggerEventNode innerGroup;
    innerGroup.name = "Render.OpaqueGeometry";
    innerGroup.isDrawCall = false;
    innerGroup.children.push_back(leaf);

    FrameDebuggerEventNode outerGroup;
    outerGroup.name = "Drawing";
    outerGroup.isDrawCall = false;
    outerGroup.children.push_back(innerGroup);

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(outerGroup);
    snapshot.totalEventCount = 1;

    const std::optional<FrameDebuggerEventDetails> found = FindEventDetailsByIndex(snapshot, 3);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->shaderName, "Standard/DistinguishableTestShader");

    EXPECT_FALSE(FindEventDetailsByIndex(snapshot, 999).has_value());
    EXPECT_FALSE(FindEventDetailsByIndex(snapshot, -1).has_value());
}

// frame-debugger-6 campaign, PHASE4
// (PHASE4_GAMEVIEW_PER_ENTITY_DRAW_TREE_LEAVES.md, Step 4) - proves
// FindEventDetailsByIndexRecursive() already works correctly against the NEW
// nested shape (a selectable leaf, e.g. "GameView", that ALSO has its own
// children) with zero changes needed to this function itself - only
// RenderEventNode() (the ImGui-side panel code) needed a fix for this phase.
TEST(FrameDebuggerDataTest, FindEventDetailsByIndexFindsAPerEntityChildLeafOfASelectableParentLeaf)
{
    FrameDebuggerEventDetails childDetails;
    childDetails.eventIndex = 5;
    childDetails.passName = "GameView (Entity Draw)";
    childDetails.shaderName = "Mesh.vert/Mesh.frag (PositionNormal)";

    FrameDebuggerEventNode childLeaf;
    childLeaf.name = "terrain (Entity 2)";
    childLeaf.isDrawCall = true;
    childLeaf.eventIndex = 5;
    childLeaf.details = childDetails;

    // The parent leaf is ITSELF a selectable draw call (isDrawCall == true)
    // AND has real children - the new shape this phase introduces for the
    // real "GameView" leaf.
    FrameDebuggerEventDetails parentDetails;
    parentDetails.eventIndex = 4;
    parentDetails.passName = "GameView";

    FrameDebuggerEventNode parentLeaf;
    parentLeaf.name = "GameView";
    parentLeaf.isDrawCall = true;
    parentLeaf.eventIndex = 4;
    parentLeaf.details = parentDetails;
    parentLeaf.children.push_back(childLeaf);

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(parentLeaf);
    snapshot.totalEventCount = 2;

    const std::optional<FrameDebuggerEventDetails> foundParent = FindEventDetailsByIndex(snapshot, 4);
    ASSERT_TRUE(foundParent.has_value());
    EXPECT_EQ(foundParent->passName, "GameView");

    const std::optional<FrameDebuggerEventDetails> foundChild = FindEventDetailsByIndex(snapshot, 5);
    ASSERT_TRUE(foundChild.has_value());
    EXPECT_EQ(foundChild->passName, "GameView (Entity Draw)");
    EXPECT_EQ(foundChild->shaderName, "Mesh.vert/Mesh.frag (PositionNormal)");
}

TEST(FrameDebuggerDataTest, FormatVectorPropertyTest)
{
    FrameDebuggerVectorProperty color;
    color.name = "_Color";
    color.x = 1.0f;
    color.y = 1.0f;
    color.z = 1.0f;
    color.w = 1.0f;
    EXPECT_EQ(FormatVectorProperty(color), "(1, 1, 1, 1)");

    FrameDebuggerVectorProperty other;
    other.name = "_Other";
    other.x = 0.5f;
    other.y = 0.0f;
    other.z = 0.0f;
    other.w = 0.0f;
    EXPECT_EQ(FormatVectorProperty(other), "(0.5, 0, 0, 0)");
}

TEST(FrameDebuggerDataTest, FormatMatrixPropertyTest)
{
    FrameDebuggerMatrixProperty matrix;
    matrix.name = "unity_MatrixVP";
    matrix.values = { 0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0019f, 0.0f, 0.0f, 0.0f, 0.0f, 0.00023f, 0.5f, 0.0f, 0.0f,
        0.0f, 1.0f };

    const std::string formatted = FormatMatrixProperty(matrix);
    EXPECT_EQ(formatted.substr(0, formatted.find('\n')), "0.001 0 0 0");
    EXPECT_EQ(formatted.substr(formatted.rfind('\n') + 1), "0 0 0 1");
    EXPECT_EQ(std::count(formatted.begin(), formatted.end(), '\n'), 3);
}

// frame-debugger-4 campaign, PHASE3
// (PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md, Step 3.1) - the
// pure decision function extracted out of PHASE1's own Panels/
// FrameDebuggerPanel.cpp EnsurePreviewDescriptor(), covering every
// meaningful input combination the phase document itself calls out.
//
// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.5) - REWRITTEN for the
// new `FrameDebuggerStepPreviewKind`-based signature (the old
// `isViewingGameViewLeaf`/`hasSelectedComputePassPreview` booleans and the
// `ComputePassPreview` choice are GONE - PHASE0's Locked Design Decision
// #3) - covers all four FrameDebuggerStepPreviewKind values.
TEST(FrameDebuggerDataTest, ChooseFrameDebuggerPreviewSourceTest)
{
    // No entry at all (fresh capture) -> None, regardless of stepPreviewKind.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(
                  false, FrameDebuggerStepPreviewKind::PostComposite, false, false, false),
        FrameDebuggerPreviewSourceChoice::None);

    // NotYetDrawn (a Pre-GameView compute leaf) -> NotYetDrawn, ALWAYS - even
    // if hasPreview/hasCompositedPreview are (nonsensically) true.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::NotYetDrawn, true, true, true),
        FrameDebuggerPreviewSourceChoice::NotYetDrawn);
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::NotYetDrawn, false, false, false),
        FrameDebuggerPreviewSourceChoice::NotYetDrawn);

    // PreComposite (the literal "GameView" leaf itself, or a Post-GameView
    // leaf before the composite pass) -> Preview, even if compositedPreview
    // is ALSO present (explicit pre-composite selection always wins).
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PreComposite, true, true, false),
        FrameDebuggerPreviewSourceChoice::Preview);
    // PreComposite, preview somehow absent (defensive-only, should not
    // happen in practice) -> None.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PreComposite, false, true, false),
        FrameDebuggerPreviewSourceChoice::None);

    // PostComposite (the composite pass itself, anything after it, or
    // nothing selected) with compositedPreview present -> CompositedPreview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PostComposite, true, true, false),
        FrameDebuggerPreviewSourceChoice::CompositedPreview);
    // PostComposite, compositedPreview absent, preview present -> falls back to Preview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PostComposite, true, false, false),
        FrameDebuggerPreviewSourceChoice::Preview);
    // PostComposite, neither present -> None.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PostComposite, false, false, false),
        FrameDebuggerPreviewSourceChoice::None);

    // PerObjectStep (fixes Bug 2) - the selected step's own retained image,
    // when it actually resolves -> PerObjectStepPreview, even if
    // compositedPreview is ALSO present (never shows the fog-inclusive
    // whole-frame image for this bucket).
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PerObjectStep, true, true, true),
        FrameDebuggerPreviewSourceChoice::PerObjectStepPreview);
    // PerObjectStep, but the index doesn't resolve to a real retained image
    // -> None (deliberately NEVER falls back to compositedPreview/preview).
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, FrameDebuggerStepPreviewKind::PerObjectStep, true, true, false),
        FrameDebuggerPreviewSourceChoice::None);
}

// frame-debugger-8 campaign, PHASE3
// (PHASE3_SNAPSHOT_TREE_LEAF_AND_TESTS.md, Step 3.3) - proves the new Sky
// Background branch inside BuildGameViewDrawRecordLeaf() (reached indirectly
// via the public BuildRealFrameDebuggerSnapshot() entry point, mirroring
// every other test in this file that exercises that function) builds a
// real, distinctly-shaped leaf - never mistaken for a per-entity one.
TEST(FrameDebuggerDataTest, SkyBackgroundDrawRecordProducesDistinctLeaf)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot gameView;
    gameView.name = "GameView";
    graphSnapshot.passesInExecutionOrder.push_back(gameView);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordSkyBackgroundDraw("Sky.vert/Sky.frag");

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 2u);

    const FrameDebuggerEventNode& skyLeaf = gameViewLeaf.children[1];
    EXPECT_EQ(skyLeaf.name, "Sky.vert/Sky.frag");
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_EQ(skyLeaf.details->passName, "GameView (Sky Draw)");
    EXPECT_EQ(skyLeaf.details->zTest, "Equal");
    EXPECT_EQ(skyLeaf.details->zWrite, "Off");
}

// Step 3.3, test 2 - the sky leaf must never fabricate an ECS entity
// identity - there is no real entity behind this draw at all.
TEST(FrameDebuggerDataTest, SkyBackgroundLeafHasNoEntityIdentityVector)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot gameView;
    gameView.name = "GameView";
    graphSnapshot.passesInExecutionOrder.push_back(gameView);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordSkyBackgroundDraw("Sky.vert/Sky.frag");

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 2u);
    const FrameDebuggerEventNode& skyLeaf = gameViewLeaf.children[1];
    ASSERT_TRUE(skyLeaf.details.has_value());

    bool foundEntityIdentity = false;
    bool foundTriangleCount = false;
    for (const FrameDebuggerVectorProperty& vec : skyLeaf.details->vectors) {
        if (vec.name == "Entity (Index, Generation)") {
            foundEntityIdentity = true;
        }
        if (vec.name == "Triangle Count") {
            foundTriangleCount = true;
        }
    }
    EXPECT_FALSE(foundEntityIdentity);
    EXPECT_TRUE(foundTriangleCount);
}

// Step 3.3, test 3 - direct regression test for this campaign's own
// cross-phase ordering invariant (PHASE0_MASTER_STRATEGY.md).
TEST(FrameDebuggerDataTest, SkyBackgroundLeafEventIndexIsMonotonicallyAfterEntityLeaves)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot gameView;
    gameView.name = "GameView";
    graphSnapshot.passesInExecutionOrder.push_back(gameView);

    rg::RenderGraphPassSnapshot postPass;
    postPass.name = "SomePostGameViewComputePass";
    postPass.isComputePass = true;
    graphSnapshot.passesInExecutionOrder.push_back(postPass);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordSkyBackgroundDraw("Sky.vert/Sky.frag");

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& root = snapshot.rootNodes[0];
    ASSERT_EQ(root.children.size(), 2u); // "GameView" leaf + "Compute Dispatches (Post-GameView)" group.
    const FrameDebuggerEventNode& gameViewLeaf = root.children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 2u);

    const int entityEventIndex = gameViewLeaf.children[0].eventIndex;
    const int skyEventIndex = gameViewLeaf.children[1].eventIndex;
    EXPECT_EQ(skyEventIndex, entityEventIndex + 1);

    const FrameDebuggerEventNode& postGroup = root.children[1];
    ASSERT_EQ(postGroup.children.size(), 1u);
    EXPECT_LT(skyEventIndex, postGroup.children[0].eventIndex);
}

// Step 3.3, test 4 - direct regression test for PHASE0's own "Cross-phase
// invariant" section: the sky leaf's own stepPreviewIndex is its real,
// 0-based position among capture.DrawRecords().
TEST(FrameDebuggerDataTest, SkyBackgroundLeafStepPreviewIndexMatchesItsPositionInDrawRecords)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot gameView;
    gameView.name = "GameView";
    graphSnapshot.passesInExecutionOrder.push_back(gameView);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);
    capture.RecordEntityDraw(3, 0, "SmokeTestCube", "Mesh.vert/Mesh.frag (PositionNormal)", "", 12);
    capture.RecordSkyBackgroundDraw("Sky.vert/Sky.frag");

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 3u);
    const FrameDebuggerEventNode& skyLeaf = gameViewLeaf.children[2];
    ASSERT_TRUE(skyLeaf.details.has_value());
    EXPECT_EQ(skyLeaf.details->stepPreviewIndex, static_cast<int>(capture.DrawRecords().size()) - 1);
}

// Step 3.3, test 5 - pure regression guard: with no sky record at all, the
// pre-campaign per-entity leaf shape is completely unchanged (proves the
// `else` branch of BuildGameViewDrawRecordLeaf() is truly untouched).
TEST(FrameDebuggerDataTest, EntityLeafShapeIsUnchangedWhenNoSkyRecordExists)
{
    rg::RenderGraphSnapshot graphSnapshot;
    rg::RenderGraphPassSnapshot gameView;
    gameView.name = "GameView";
    graphSnapshot.passesInExecutionOrder.push_back(gameView);

    FrameDebuggerCaptureContext capture;
    capture.RecordEntityDraw(2, 0, "terrain", "Mesh.vert/Mesh.frag (PositionNormal)", "", 1045458);

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, capture);

    ASSERT_EQ(snapshot.rootNodes.size(), 1u);
    const FrameDebuggerEventNode& gameViewLeaf = snapshot.rootNodes[0].children[0];
    ASSERT_EQ(gameViewLeaf.children.size(), 1u);

    const FrameDebuggerEventNode& terrainLeaf = gameViewLeaf.children[0];
    EXPECT_EQ(terrainLeaf.name, "terrain (Entity 2)");
    ASSERT_TRUE(terrainLeaf.details.has_value());
    EXPECT_EQ(terrainLeaf.details->passName, "GameView (Entity Draw)");
    EXPECT_EQ(terrainLeaf.details->eventLabel, "Draw Mesh");
    EXPECT_EQ(terrainLeaf.details->zTest, "Less");
    EXPECT_EQ(terrainLeaf.details->zWrite, "On");

    bool foundEntityIdentity = false;
    for (const FrameDebuggerVectorProperty& vec : terrainLeaf.details->vectors) {
        if (vec.name == "Entity (Index, Generation)") {
            foundEntityIdentity = true;
            EXPECT_FLOAT_EQ(vec.x, 2.0f);
            EXPECT_FLOAT_EQ(vec.y, 0.0f);
        }
    }
    EXPECT_TRUE(foundEntityIdentity);
}

} // namespace
} // namespace gte
