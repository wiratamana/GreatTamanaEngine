// Unit tests for RigidBodyGroupSelection (src/Editor/RigidBodyGroupSelection.h)
// - the pure "select all group"/"select all branch" algorithms behind the
// Bone Viewer's Rigid Body toolbar buttons (see AGENTS.md, "Testability and
// Regression Safety", and task_manager/verlet-integration-4/
// PHASE2_RIGID_BODY_GROUP_FIELD_AND_ADJACENCY_ALGORITHMS.md). Deliberately
// pure logic with no ImGui/SDL/Vulkan dependency at all - only compiled/
// linked when GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are both ON,
// since RigidBodyGroupSelection itself is only ever compiled into gte_core
// then (see tests/CMakeLists.txt, alongside RigidBodyWireframeTests.cpp).

#include "Editor/RigidBodyGroupSelection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(RigidBodyGroupSelectionTest, BuildAdjacencyProducesBidirectionalNeighborLists)
{
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 1, 2 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 3);

    ASSERT_EQ(adjacency.size(), 3u);
    EXPECT_EQ(adjacency[0], (std::vector<std::int32_t>{ 1 }));
    EXPECT_EQ(adjacency[1], (std::vector<std::int32_t>{ 0, 2 }));
    EXPECT_EQ(adjacency[2], (std::vector<std::int32_t>{ 1 }));
}

TEST(RigidBodyGroupSelectionTest, BuildAdjacencyDropsOutOfRangeAndSelfAndDuplicateEdges)
{
    const std::vector<RigidBodyJointEdge> edges{
        { 0, 1 }, { 0, 1 }, // Duplicate - must not double-list.
        { 5, 0 }, // Out of range (rigidBodyCount is 2) - dropped.
        { 1, 1 }, // Self-joint - dropped.
        { -1, 0 }, // Negative - dropped.
    };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 2);

    ASSERT_EQ(adjacency.size(), 2u);
    EXPECT_EQ(adjacency[0], (std::vector<std::int32_t>{ 1 }));
    EXPECT_EQ(adjacency[1], (std::vector<std::int32_t>{ 0 }));
}

TEST(RigidBodyGroupSelectionTest, BuildAdjacencyOnEmptyEdgesStillSizesToRigidBodyCount)
{
    const auto adjacency = BuildRigidBodyAdjacency({}, 4);
    ASSERT_EQ(adjacency.size(), 4u);
    for (const auto& neighbors : adjacency) {
        EXPECT_TRUE(neighbors.empty());
    }
}

TEST(RigidBodyGroupSelectionTest, SelectByGroupReturnsEverySameGroupIndexIncludingSeed)
{
    const std::vector<std::uint8_t> groups{ 0, 1, 1, 0, 1 };
    const auto result = SelectRigidBodiesByGroup(groups, /*seedIndex=*/2);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 1, 2, 4 }));
}

TEST(RigidBodyGroupSelectionTest, SelectByGroupWithUniqueGroupReturnsOnlyTheSeed)
{
    const std::vector<std::uint8_t> groups{ 0, 1, 2, 3 };
    const auto result = SelectRigidBodiesByGroup(groups, /*seedIndex=*/2);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 2 }));
}

TEST(RigidBodyGroupSelectionTest, SelectByGroupReturnsEmptyForOutOfRangeSeed)
{
    const std::vector<std::uint8_t> groups{ 0, 1 };
    EXPECT_TRUE(SelectRigidBodiesByGroup(groups, /*seedIndex=*/5).empty());
    EXPECT_TRUE(SelectRigidBodiesByGroup(groups, /*seedIndex=*/-1).empty());
}

// The story's own topology, transcribed to indices:
//
//   +--A--+
//   |     |
//
// Left '+' = index 0, right '+' = index 5. The 'A' segment between them is
// three interior bodies: 1, 2, 3 (0-1-2-3-5 forms the top chain). Each '+'
// ALSO connects downward to a further body (4 for the left one, 6 for the
// right one) that isn't part of 'A' at all - this third connection is
// exactly what makes each '+' a genuine branch (degree 3: one neighbor
// along 'A', one neighbor going down, per this test's own minimal
// topology - a real model would have a 4th connection completing a loop,
// but 3 is already sufficient to qualify as a branch per this module's own
// >= 3 rule).
TEST(RigidBodyGroupSelectionTest, SelectBranchMatchesTheStorysOwnDiagramExactly)
{
    // 0 = '+' (left junction): neighbors 1 (along A), 4 (down), 6 (extra third edge to prove degree 3).
    // 1,2,3 = 'A' interior chain.
    // 5 = '+' (right junction): neighbors 3 (along A), 7 (down), 8 (extra third edge).
    const std::vector<RigidBodyJointEdge> edges{
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 5 }, // The top chain, 0 through 5.
        { 0, 4 }, { 0, 6 }, // Left junction's other two connections.
        { 5, 7 }, { 5, 8 }, // Right junction's other two connections.
    };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 9);

    ASSERT_EQ(adjacency[0].size(), 3u); // Confirms index 0 really is a branch (degree 3).
    ASSERT_EQ(adjacency[5].size(), 3u); // Confirms index 5 really is a branch (degree 3).

    for (const std::int32_t seed : { 1, 2, 3 }) {
        const auto result = SelectRigidBodyBranch(adjacency, seed);
        EXPECT_EQ(result, (std::vector<std::int32_t>{ 1, 2, 3 })) << "seed was " << seed;
    }
}

TEST(RigidBodyGroupSelectionTest, SelectBranchFromABranchNodeItselfReturnsOnlyTheSeed)
{
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 0, 2 }, { 0, 3 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 4);

    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/0);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 0 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchOnASimpleClosedLoopWithNoBranchesSelectsTheWholeLoop)
{
    // A plain 4-body ring, every node degree 2 - no branches at all, so the
    // whole loop is one single chain/branch, whichever node you start from.
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 4);

    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/1);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 0, 1, 2, 3 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchOnAnOpenEndedChainSelectsEveryBodyToBothLeafEnds)
{
    // A plain straight chain with no branches - leaves at both ends
    // (degree 1), never mistaken for a branch (branch requires degree >= 3).
    const std::vector<RigidBodyJointEdge> edges{ { 0, 1 }, { 1, 2 }, { 2, 3 } };
    const auto adjacency = BuildRigidBodyAdjacency(edges, 4);

    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/2);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 0, 1, 2, 3 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchWithNoJointsAtAllSelectsOnlyTheSeed)
{
    const auto adjacency = BuildRigidBodyAdjacency({}, 3);
    const auto result = SelectRigidBodyBranch(adjacency, /*seedIndex=*/1);
    EXPECT_EQ(result, (std::vector<std::int32_t>{ 1 }));
}

TEST(RigidBodyGroupSelectionTest, SelectBranchReturnsEmptyForOutOfRangeSeed)
{
    const auto adjacency = BuildRigidBodyAdjacency({}, 3);
    EXPECT_TRUE(SelectRigidBodyBranch(adjacency, /*seedIndex=*/9).empty());
    EXPECT_TRUE(SelectRigidBodyBranch(adjacency, /*seedIndex=*/-1).empty());
}

} // namespace
} // namespace gte
