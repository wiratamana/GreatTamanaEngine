#include "RigidBodyJointGraph.h"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <vector>

namespace gte {

namespace {
const std::vector<GraphEdge> kEmptyEdgeList;
} // namespace

RigidBodyJointGraph RigidBodyJointGraph::Build(const PhysicsData& physics) {
    RigidBodyJointGraph graph;
    const std::size_t nodeCount = physics.rigidBodies.size();
    graph.m_neighborsByNode.resize(nodeCount);

    for (std::size_t jointIdx = 0; jointIdx < physics.joints.size(); ++jointIdx) {
        const Joint& joint = physics.joints[jointIdx];
        const std::int32_t a = joint.rigidBodyAIndex;
        const std::int32_t b = joint.rigidBodyBIndex;

        if (a < 0 || b < 0 || a == b) {
            continue; // Negative/self-loop - degenerate, skip entirely.
        }
        if (static_cast<std::size_t>(a) >= nodeCount || static_cast<std::size_t>(b) >= nodeCount) {
            continue; // Out of range - skip entirely, never crash.
        }

        graph.m_neighborsByNode[static_cast<std::size_t>(a)].push_back(
            GraphEdge{b, static_cast<std::int32_t>(jointIdx)});
        graph.m_neighborsByNode[static_cast<std::size_t>(b)].push_back(
            GraphEdge{a, static_cast<std::int32_t>(jointIdx)});
    }

    // Sort every node's own edge list by (neighborRigidBodyIndex, jointIndex)
    // ascending - this is what makes Neighbors() output independent of
    // physics.joints' own storage order.
    for (std::vector<GraphEdge>& edges : graph.m_neighborsByNode) {
        std::sort(edges.begin(), edges.end(), [](const GraphEdge& lhs, const GraphEdge& rhs) {
            if (lhs.neighborRigidBodyIndex != rhs.neighborRigidBodyIndex) {
                return lhs.neighborRigidBodyIndex < rhs.neighborRigidBodyIndex;
            }
            return lhs.jointIndex < rhs.jointIndex;
        });
    }

    return graph;
}

const std::vector<GraphEdge>& RigidBodyJointGraph::Neighbors(std::int32_t rigidBodyIndex) const {
    if (rigidBodyIndex < 0 || static_cast<std::size_t>(rigidBodyIndex) >= m_neighborsByNode.size()) {
        return kEmptyEdgeList;
    }
    return m_neighborsByNode[static_cast<std::size_t>(rigidBodyIndex)];
}

ReachabilityResult ComputeReachabilityFromStaticAnchors(const PhysicsData& physics, const RigidBodyJointGraph& graph) {
    ReachabilityResult result;

    const std::size_t nodeCount = physics.rigidBodies.size();
    std::vector<bool> visited(nodeCount, false);
    std::queue<std::int32_t> frontier;

    // Seed the multi-source BFS from every Static rigid body, ascending order
    // (for determinism - though BFS order itself does not affect the final
    // visited set, only the seeding order is made deterministic here for
    // clarity/reproducibility).
    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (physics.rigidBodies[i].motionType == RigidBodyMotionType::Static) {
            if (!visited[i]) {
                visited[i] = true;
                frontier.push(static_cast<std::int32_t>(i));
            }
        }
    }

    while (!frontier.empty()) {
        const std::int32_t current = frontier.front();
        frontier.pop();
        for (const GraphEdge& edge : graph.Neighbors(current)) {
            const std::int32_t neighbor = edge.neighborRigidBodyIndex;
            if (neighbor < 0 || static_cast<std::size_t>(neighbor) >= nodeCount) {
                continue;
            }
            if (!visited[static_cast<std::size_t>(neighbor)]) {
                visited[static_cast<std::size_t>(neighbor)] = true;
                frontier.push(neighbor);
            }
        }
    }

    // Classify every Dynamic/DynamicAndBoneMerge rigid body - Static bodies
    // are never themselves classified into either list (they are anchors,
    // purely a BFS seed).
    for (std::size_t i = 0; i < nodeCount; ++i) {
        const RigidBodyMotionType motionType = physics.rigidBodies[i].motionType;
        if (motionType == RigidBodyMotionType::Static) {
            continue;
        }
        if (visited[i]) {
            result.reachableDynamicRigidBodyIndices.push_back(static_cast<std::int32_t>(i));
        } else {
            result.orphanedDynamicRigidBodyIndices.push_back(static_cast<std::int32_t>(i));
        }
    }

    return result;
}

} // namespace gte
