#pragma once
#include "../Assets/PhysicsData.h"

#include <cstdint>
#include <vector>

namespace gte {

// One edge of the RigidBody/Joint graph, from ONE endpoint's own point of
// view (see RigidBodyJointGraph::Neighbors below - each real Joint produces
// TWO GraphEdge entries, one per direction, so a caller walking outward from
// any rigid body only ever needs to look at ITS OWN neighbor list).
struct GraphEdge {
    std::int32_t neighborRigidBodyIndex = -1; // The OTHER rigid body this edge connects to.
    std::int32_t jointIndex = -1;             // Which PhysicsData::joints entry this edge came from.
};

// task_manager/verlet-integration-6, Phase 2 - a plain, undirected adjacency
// representation of PhysicsData's own RigidBody/Joint graph (rigid bodies
// are nodes, Joints are edges) - deliberately pure/free of SkeletonData, ECS,
// Editor, and GPU state, so both this phase's own tests and Phase 3's
// consumption of it stay simple and fast. Construction is ORDER-INDEPENDENT
// with respect to PhysicsData::rigidBodies/joints' own storage order - see
// Build()'s own doc comment below for the exact determinism guarantee.
class RigidBodyJointGraph {
public:
    // Builds the adjacency list from `physics`. A Joint whose
    // rigidBodyAIndex/rigidBodyBIndex is out of range, negative, or equal to
    // itself (a degenerate self-loop) is skipped entirely - never crashes,
    // never added as a half-formed edge. Two OR MORE Joints connecting the
    // exact same pair of rigid bodies are all still recorded independently
    // (deduping is a Phase 3 concern, not this module's - a caller that
    // cares about "how many joints connect A and B" should see all of them).
    static RigidBodyJointGraph Build(const PhysicsData& physics);

    // Every edge touching `rigidBodyIndex`, in ASCENDING neighborRigidBodyIndex
    // order (falling back to ascending jointIndex to break a tie when the
    // SAME neighbor is connected by more than one Joint) - this fixed,
    // content-derived ordering is what makes every later consumer (Phase 3's
    // BFS/DFS) produce identical results regardless of PhysicsData::joints'
    // own on-disk/in-memory order. Returns an empty vector for an
    // out-of-range or unconnected rigidBodyIndex.
    const std::vector<GraphEdge>& Neighbors(std::int32_t rigidBodyIndex) const;

    // Total node slots this graph was built against (== physics.rigidBodies.size()).
    std::size_t NodeCount() const noexcept { return m_neighborsByNode.size(); }

private:
    std::vector<std::vector<GraphEdge>> m_neighborsByNode; // index-aligned with PhysicsData::rigidBodies.
};

// task_manager/verlet-integration-6, Phase 2 - for every Dynamic/
// DynamicAndBoneMerge rigid body in `physics` (RigidBodyMotionType::Static
// bodies are never themselves a "member" - they are anchors, matching
// DynamicChainDefinition::rootBoneIndex's own "never itself simulated"
// convention), determines whether it is graph-reachable from AT LEAST ONE
// Static rigid body by walking `graph`'s own edges (a plain multi-source BFS
// seeded from every Static rigid body at once, matching a real skirt's
// common "attached at several separate hip/belt points" topology). Returns
// TWO disjoint, together-exhaustive lists (every Dynamic/DynamicAndBoneMerge
// rigid body with a valid... see ReachabilityResult's own fields below).
// Fully order-independent: shuffling physics.rigidBodies/joints' own storage
// order before calling Build()+ComputeReachability() again produces the
// exact same two lists (as SETS - callers that care about a stable, sorted
// output should sort the returned vectors themselves; this function returns
// them in ascending rigid-body-index order already, for convenience, but
// that is a courtesy, not a correctness requirement).
struct ReachabilityResult {
    // Dynamic/DynamicAndBoneMerge rigid bodies reachable from >=1 Static body.
    std::vector<std::int32_t> reachableDynamicRigidBodyIndices;
    // Dynamic/DynamicAndBoneMerge rigid bodies with NO Static body reachable
    // anywhere in their own connected component - task_manager/
    // verlet-integration-6 Phase 3's own "orphan" input (see
    // PHASE0_MASTER_STRATEGY.md's own quoted user answer on this exact case).
    std::vector<std::int32_t> orphanedDynamicRigidBodyIndices;
};
ReachabilityResult ComputeReachabilityFromStaticAnchors(const PhysicsData& physics, const RigidBodyJointGraph& graph);

} // namespace gte
