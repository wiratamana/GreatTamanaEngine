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
TEST(FrameDebuggerDataTest, ChooseFrameDebuggerPreviewSourceTest)
{
    // No entry at all (fresh history) -> None.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(false, false, false, false), FrameDebuggerPreviewSourceChoice::None);

    // Entry exists, nothing selected, compositedPreview present -> CompositedPreview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, true, true, false),
        FrameDebuggerPreviewSourceChoice::CompositedPreview);

    // Entry exists, nothing selected, compositedPreview absent, preview present -> Preview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, true, false, false), FrameDebuggerPreviewSourceChoice::Preview);

    // Entry exists, "GameView" leaf selected, preview present -> Preview (even if
    // compositedPreview is ALSO present - explicit leaf selection always wins).
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, true, true, true), FrameDebuggerPreviewSourceChoice::Preview);

    // Entry exists, "GameView" leaf selected, preview somehow absent (defensive-only,
    // should not happen in practice) -> None.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, false, true, true), FrameDebuggerPreviewSourceChoice::None);

    // Entry exists, some OTHER leaf selected (e.g. "Aerial Perspective Composite" or
    // "GPU Skinning"), compositedPreview present -> CompositedPreview.
    EXPECT_EQ(ChooseFrameDebuggerPreviewSource(true, true, true, false),
        FrameDebuggerPreviewSourceChoice::CompositedPreview);
}

} // namespace
} // namespace gte
