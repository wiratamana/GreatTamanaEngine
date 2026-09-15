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

} // namespace
} // namespace gte
