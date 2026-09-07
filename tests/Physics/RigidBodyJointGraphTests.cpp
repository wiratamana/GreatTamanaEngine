// Unit tests for RigidBodyJointGraph/ComputeReachabilityFromStaticAnchors
// (src/Physics/RigidBodyJointGraph.h) - task_manager/verlet-integration-6,
// PHASE2_RIGIDBODY_JOINT_GRAPH_ANALYSIS.md. Pure module - hand-built
// PhysicsData fixtures only, no SkeletonData/ECS/GPU/file I/O involved.

#include "Physics/RigidBodyJointGraph.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace gte {
namespace {

RigidBody MakeRigidBody(RigidBodyMotionType motionType) {
    RigidBody body;
    body.motionType = motionType;
    return body;
}

Joint MakeJoint(std::int32_t a, std::int32_t b) {
    Joint joint;
    joint.rigidBodyAIndex = a;
    joint.rigidBodyBIndex = b;
    return joint;
}

std::set<std::int32_t> ToSet(const std::vector<std::int32_t>& values) {
    return std::set<std::int32_t>(values.begin(), values.end());
}

} // namespace

TEST(RigidBodyJointGraphTests, LinearChainAllReachableFromSingleStaticAnchor) {
    // Static(0) - Joint - Dynamic(1) - Joint - Dynamic(2).
    PhysicsData physics;
    physics.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Static),
        MakeRigidBody(RigidBodyMotionType::Dynamic),
        MakeRigidBody(RigidBodyMotionType::Dynamic),
    };
    physics.joints = { MakeJoint(0, 1), MakeJoint(1, 2) };

    const RigidBodyJointGraph graph = RigidBodyJointGraph::Build(physics);
    const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(physics, graph);

    EXPECT_EQ(ToSet(reach.reachableDynamicRigidBodyIndices), (std::set<std::int32_t>{ 1, 2 }));
    EXPECT_TRUE(reach.orphanedDynamicRigidBodyIndices.empty());
}

TEST(RigidBodyJointGraphTests, SpiderWebHubWithFourChildrenAllReachableFromOneAnchor) {
    // Static(0) connected via 4 separate Joints to Dynamic(1..4), each of
    // which ALSO has its own Joint further out to a fifth-level Dynamic body
    // (5..8), PLUS a Joint directly cross-connecting Dynamic(1) and
    // Dynamic(2) (the "ring brace").
    PhysicsData physics;
    physics.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Static),     // 0
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 1
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 2
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 3
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 4
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 5
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 6
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 7
        MakeRigidBody(RigidBodyMotionType::Dynamic),     // 8
    };
    physics.joints = {
        MakeJoint(0, 1), MakeJoint(0, 2), MakeJoint(0, 3), MakeJoint(0, 4),
        MakeJoint(1, 5), MakeJoint(2, 6), MakeJoint(3, 7), MakeJoint(4, 8),
        MakeJoint(1, 2), // Ring brace, cross-connects two siblings.
    };

    const RigidBodyJointGraph graph = RigidBodyJointGraph::Build(physics);
    const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(physics, graph);

    EXPECT_EQ(ToSet(reach.reachableDynamicRigidBodyIndices), (std::set<std::int32_t>{ 1, 2, 3, 4, 5, 6, 7, 8 }));
    EXPECT_TRUE(reach.orphanedDynamicRigidBodyIndices.empty());

    // graph.Neighbors(1) must contain BOTH its parent-ward edge to 0 AND its
    // cross edge to 2 - proving the graph itself does not discard "extra"
    // edges (Phase 3 needs both).
    const std::vector<GraphEdge>& neighborsOf1 = graph.Neighbors(1);
    const bool hasEdgeTo0 = std::any_of(neighborsOf1.begin(), neighborsOf1.end(),
        [](const GraphEdge& edge) { return edge.neighborRigidBodyIndex == 0; });
    const bool hasEdgeTo2 = std::any_of(neighborsOf1.begin(), neighborsOf1.end(),
        [](const GraphEdge& edge) { return edge.neighborRigidBodyIndex == 2; });
    const bool hasEdgeTo5 = std::any_of(neighborsOf1.begin(), neighborsOf1.end(),
        [](const GraphEdge& edge) { return edge.neighborRigidBodyIndex == 5; });
    EXPECT_TRUE(hasEdgeTo0);
    EXPECT_TRUE(hasEdgeTo2);
    EXPECT_TRUE(hasEdgeTo5);
    EXPECT_EQ(neighborsOf1.size(), 3u);
}

TEST(RigidBodyJointGraphTests, OrphanedIslandWithNoStaticAnchorAnywhereIsFlagged) {
    // Two Dynamic bodies jointed only to each other, no Static body anywhere.
    PhysicsData physics;
    physics.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Dynamic),
        MakeRigidBody(RigidBodyMotionType::Dynamic),
    };
    physics.joints = { MakeJoint(0, 1) };

    const RigidBodyJointGraph graph = RigidBodyJointGraph::Build(physics);
    const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(physics, graph);

    EXPECT_TRUE(reach.reachableDynamicRigidBodyIndices.empty());
    EXPECT_EQ(ToSet(reach.orphanedDynamicRigidBodyIndices), (std::set<std::int32_t>{ 0, 1 }));
}

TEST(RigidBodyJointGraphTests, MultipleIndependentStaticAnchorsEachOwnTheirOwnReachableSet) {
    // Two entirely separate Static-rooted chains in the same PhysicsData, no
    // edge between them.
    PhysicsData physics;
    physics.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Static),  // 0
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 1
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 2
        MakeRigidBody(RigidBodyMotionType::Static),  // 3
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 4
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 5
    };
    physics.joints = { MakeJoint(0, 1), MakeJoint(1, 2), MakeJoint(3, 4), MakeJoint(4, 5) };

    const RigidBodyJointGraph graph = RigidBodyJointGraph::Build(physics);
    const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(physics, graph);

    EXPECT_EQ(ToSet(reach.reachableDynamicRigidBodyIndices), (std::set<std::int32_t>{ 1, 2, 4, 5 }));
    EXPECT_TRUE(reach.orphanedDynamicRigidBodyIndices.empty());
}

TEST(RigidBodyJointGraphTests, OutOfRangeAndSelfLoopJointsAreSkippedNotCrashed) {
    PhysicsData physicsWithBadJoints;
    physicsWithBadJoints.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Static),
        MakeRigidBody(RigidBodyMotionType::Dynamic),
        MakeRigidBody(RigidBodyMotionType::Dynamic),
    };
    physicsWithBadJoints.joints = {
        MakeJoint(0, 1),
        MakeJoint(99, 1),  // Out of range - skipped.
        MakeJoint(2, 2),   // Self-loop - skipped.
    };

    PhysicsData physicsWithoutBadJoints;
    physicsWithoutBadJoints.rigidBodies = physicsWithBadJoints.rigidBodies;
    physicsWithoutBadJoints.joints = { MakeJoint(0, 1) };

    const RigidBodyJointGraph graphWithBad = RigidBodyJointGraph::Build(physicsWithBadJoints);
    const RigidBodyJointGraph graphWithoutBad = RigidBodyJointGraph::Build(physicsWithoutBadJoints);

    ASSERT_EQ(graphWithBad.NodeCount(), graphWithoutBad.NodeCount());
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(graphWithBad.NodeCount()); ++i) {
        const std::vector<GraphEdge>& withBad = graphWithBad.Neighbors(i);
        const std::vector<GraphEdge>& withoutBad = graphWithoutBad.Neighbors(i);
        ASSERT_EQ(withBad.size(), withoutBad.size());
        for (std::size_t j = 0; j < withBad.size(); ++j) {
            EXPECT_EQ(withBad[j].neighborRigidBodyIndex, withoutBad[j].neighborRigidBodyIndex);
            EXPECT_EQ(withBad[j].jointIndex, withoutBad[j].jointIndex);
        }
    }

    const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(physicsWithBadJoints, graphWithBad);
    EXPECT_EQ(ToSet(reach.reachableDynamicRigidBodyIndices), (std::set<std::int32_t>{ 1 }));
    EXPECT_EQ(ToSet(reach.orphanedDynamicRigidBodyIndices), (std::set<std::int32_t>{ 2 }));
}

TEST(RigidBodyJointGraphTests, ResultIsIdenticalRegardlessOfRigidBodyAndJointStorageOrder) {
    // "Natural" order: Static(0) - Dynamic(1) - Dynamic(2), plus Dynamic(3)
    // branching off Dynamic(1).
    PhysicsData natural;
    natural.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Static),  // 0
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 1
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 2
        MakeRigidBody(RigidBodyMotionType::Dynamic), // 3
    };
    natural.joints = { MakeJoint(0, 1), MakeJoint(1, 2), MakeJoint(1, 3) };

    // Same logical graph, reversed/renumbered: old index i -> new index (3 - i).
    // 0->3 (Static), 1->2 (Dynamic), 2->1 (Dynamic), 3->0 (Dynamic).
    PhysicsData shuffled;
    shuffled.rigidBodies = {
        MakeRigidBody(RigidBodyMotionType::Dynamic), // was 3
        MakeRigidBody(RigidBodyMotionType::Dynamic), // was 2
        MakeRigidBody(RigidBodyMotionType::Dynamic), // was 1
        MakeRigidBody(RigidBodyMotionType::Static),  // was 0
    };
    // Joints reversed in storage order too, and re-indexed to match.
    shuffled.joints = { MakeJoint(2, 0), MakeJoint(1, 2), MakeJoint(3, 2) };

    const RigidBodyJointGraph naturalGraph = RigidBodyJointGraph::Build(natural);
    const ReachabilityResult naturalReach = ComputeReachabilityFromStaticAnchors(natural, naturalGraph);

    const RigidBodyJointGraph shuffledGraph = RigidBodyJointGraph::Build(shuffled);
    const ReachabilityResult shuffledReach = ComputeReachabilityFromStaticAnchors(shuffled, shuffledGraph);

    // Map shuffled indices back to natural indices for comparison: new = 3 - old.
    std::set<std::int32_t> shuffledReachableRemapped;
    for (std::int32_t idx : shuffledReach.reachableDynamicRigidBodyIndices) {
        shuffledReachableRemapped.insert(3 - idx);
    }
    std::set<std::int32_t> shuffledOrphanedRemapped;
    for (std::int32_t idx : shuffledReach.orphanedDynamicRigidBodyIndices) {
        shuffledOrphanedRemapped.insert(3 - idx);
    }

    EXPECT_EQ(ToSet(naturalReach.reachableDynamicRigidBodyIndices), shuffledReachableRemapped);
    EXPECT_EQ(ToSet(naturalReach.orphanedDynamicRigidBodyIndices), shuffledOrphanedRemapped);
}

} // namespace gte
