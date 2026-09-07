#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// One joint's rigid-body endpoints only - the minimal shape
// BuildRigidBodyAdjacency() needs, deliberately decoupled from the full
// Joint/BoneViewerWindow::JointEntry struct (src/Assets/PhysicsData.h /
// BoneViewerWindow.h) so this module stays a pure, dependency-free graph/
// selection algorithm, exactly like RigidBodyWireframe.h is a pure,
// dependency-free geometry builder for a different problem (see
// task_manager/verlet-integration-3/PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md).
struct RigidBodyJointEdge {
    std::int32_t rigidBodyAIndex = -1;
    std::int32_t rigidBodyBIndex = -1;
};

// Builds a per-rigid-body adjacency list - adjacency[i] lists every OTHER
// rigid-body index directly connected to body i by at least one joint edge -
// from a flat list of joint rigid-body-pairs. `rigidBodyCount` bounds the
// returned vector's size (index i exists in the result for every
// 0 <= i < rigidBodyCount, even if it ends up with zero neighbors) and is
// also used to defensively drop any out-of-range edge (mirrors this
// codebase's own established "never trust indices blindly" convention - see
// BoneViewerWindow.cpp's own bounds checks before indexing m_rigidBodies/
// m_bones). A self-referencing edge (rigidBodyAIndex == rigidBodyBIndex) is
// never a real connection and is dropped. Duplicate edges (the same pair
// joined by more than one Joint) are naturally absorbed - adjacency[i] never
// lists the same neighbor twice. This is the graph SelectRigidBodyBranch()
// below walks.
std::vector<std::vector<std::int32_t>> BuildRigidBodyAdjacency(
    const std::vector<RigidBodyJointEdge>& edges, std::size_t rigidBodyCount);

// Returns every rigid-body index (INCLUDING `seedIndex` itself) whose
// groups[i] equals groups[seedIndex] - the "select all group" story
// requirement, verbatim: PMX collision `group` (0-15,
// src/Assets/PhysicsData.h's own RigidBody::group) is the only notion of
// "group" this engine's rigid bodies have; this is a flat property
// comparison across the WHOLE list, no adjacency/graph involved at all.
// Returns an empty result if `seedIndex` is out of range for `groups`
// (nothing to compare against). Result is ascending-sorted (ready to hand
// straight to Selection::SelectModelParts() - see task_manager/
// verlet-integration-4/PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md).
std::vector<std::int32_t> SelectRigidBodiesByGroup(const std::vector<std::uint8_t>& groups, std::int32_t seedIndex);

// Walks outward from `seedIndex` over `adjacency` (see
// BuildRigidBodyAdjacency() above) via a plain graph walk - the exact
// visitation ORDER is deliberately unspecified/implementation-defined (the
// reference implementation below happens to use an explicit stack, i.e.
// depth-first order, but this is not part of this function's contract and
// must never be relied upon by a caller) - collecting every reachable rigid
// body EXCEPT a "branch" node - one connected to 3 or more OTHER rigid
// bodies at once, i.e. a point where more than one chain meets (the story's
// own "+" in "+--A--+"/"|     |": a corner where the horizontal chain and a
// vertical chain both meet, PLUS at least one further, unshown connection,
// is what makes it a genuine branch rather than just an ordinary two-
// neighbor corner). A branch node itself is EXCLUDED from the result, and
// expansion NEVER continues through it (none of ITS OTHER neighbors are
// visited via it) - so the result is exactly the single uninterrupted chain
// of degree-<=2 bodies the seed belongs to, stopping cleanly at every branch
// point on either side, never leaking into a neighboring chain on the far
// side of a junction. If `seedIndex` ITSELF is already a branch node (there
// is no single unambiguous chain to walk - which of its 3+ directions would
// even be "the branch"?), the result is just `{ seedIndex }` alone, by
// design - see this file's own test coverage for this exact edge case. The
// FINAL RESULT (as opposed to the traversal order that produces it) is
// always ascending-sorted and always includes `seedIndex` itself whenever
// the result is non-empty, so it is fully deterministic and reproducible
// regardless of adjacency-list ordering or traversal strategy. Returns an
// empty result if `seedIndex` is out of range for `adjacency`.
std::vector<std::int32_t> SelectRigidBodyBranch(
    const std::vector<std::vector<std::int32_t>>& adjacency, std::int32_t seedIndex);

} // namespace gte
