// Unit tests for FlatListRangeSelection (src/Editor/FlatListRangeSelection.h)
// - the pure "build a Shift-click contiguous range" logic behind the Bone
// Viewer's Rigid Body/Joint flat-row and viewport-dot Shift-click handling
// (see AGENTS.md, "Testability and Regression Safety", and task_manager/
// verlet-integration-4/PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md's
// own v2 revision). Deliberately pure logic with no ImGui/SDL/Vulkan/
// BoneViewerWindow dependency at all - only compiled/linked when
// GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are both ON, since
// FlatListRangeSelection itself is only ever compiled into gte_core then
// (see tests/CMakeLists.txt, alongside RigidBodySelectionAlgorithmsTests.cpp).

#include "Editor/FlatListRangeSelection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FlatListRangeSelectionTest, NoAnchorReturnsOnlyTheClickedIndex)
{
    EXPECT_EQ(BuildInclusiveIndexRange(-1, 7), (std::vector<std::int32_t>{ 7 }));
}

TEST(FlatListRangeSelectionTest, AnchorBeforeClickedBuildsAscendingInclusiveRange)
{
    EXPECT_EQ(BuildInclusiveIndexRange(2, 5), (std::vector<std::int32_t>{ 2, 3, 4, 5 }));
}

TEST(FlatListRangeSelectionTest, AnchorAfterClickedStillBuildsAscendingInclusiveRange)
{
    EXPECT_EQ(BuildInclusiveIndexRange(5, 2), (std::vector<std::int32_t>{ 2, 3, 4, 5 }));
}

TEST(FlatListRangeSelectionTest, AnchorEqualToClickedReturnsASingleElement)
{
    EXPECT_EQ(BuildInclusiveIndexRange(3, 3), (std::vector<std::int32_t>{ 3 }));
}

TEST(FlatListRangeSelectionTest, AdjacentAnchorAndClickedReturnsExactlyTheTwoElements)
{
    EXPECT_EQ(BuildInclusiveIndexRange(4, 5), (std::vector<std::int32_t>{ 4, 5 }));
}

} // namespace
} // namespace gte
