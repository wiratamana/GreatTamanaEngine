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

} // namespace
} // namespace gte
