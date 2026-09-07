# PHASE2 — RigidBody/Joint Graph Analysis (pure, reusable module)

Part of the `verlet-integration-6` campaign — read `PHASE0_MASTER_STRATEGY.md`
and `PHASE1_TREE_BASED_CHAIN_DATA_MODEL_AND_SOLVER.md` first. Phase 1 must
already compile and pass its own tests before starting this phase. This
phase adds ONE new pure module with ZERO `SkeletonData`/ECS/Editor/GPU
dependency — it only ever reasons about `PhysicsData::rigidBodies`/`joints`
indices. Phase 3 is the only later phase allowed to combine this module's
output with skeleton bone data.

**v2 note:** this document was independently re-reviewed end-to-end during
the campaign's v2 pass (see `PHASE0_MASTER_STRATEGY.md`'s own "Revision
Notes (v2)") and required NO changes — the bug found elsewhere in this
campaign (Culprit F, fixed in `PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md`)
turned out to be entirely in how Phase 3 *consumed* this module's already-
correct `ReachabilityResult` output, never in this module's own contract or
implementation. Kept verbatim.

## Step 1: The Goal (Where are we going?)

Give Phase 3 a small, already-tested, already-correct answer to exactly one
question, stated precisely: **"given `PhysicsData`, which `RigidBody`
entries (by index) are `Dynamic`/`DynamicAndBoneMerge` AND graph-reachable
from at least one `Static` `RigidBody`, by walking `Joint` edges — and which
ones are not (orphaned)?"** — plus the raw adjacency data needed to answer
follow-up questions later ("what are ALL the joints touching rigid body
N?"). This module deliberately does **not** decide chain topology, tree
shape, or anchoring order itself — it only tells Phase 3 which nodes/edges
exist and which nodes are provably reachable from a `Static` node. Keeping
this boundary this narrow is what makes it independently, exhaustively
testable without a single `SkeletonData` fixture anywhere in this phase's own
test file.

## Step 2: The Situation / The Problem (Where are we now?)

There is no graph representation of `PhysicsData` anywhere in the engine
today. `src/Assets/PhysicsData.h`'s `RigidBody`/`Joint` structs are pure,
flat, index-referencing data (`Joint::rigidBodyAIndex`/`rigidBodyBIndex` are
plain `std::int32_t` indices into `PhysicsData::rigidBodies`) — nothing
builds an adjacency list, computes connected components, or does any
reachability analysis over them; `DynamicChainDetection.cpp` today (before
Phase 3 rewrites it) never even iterates `PhysicsData::joints` at all (see
`PHASE0_MASTER_STRATEGY.md`, Culprit B).

## Step 3: The Plan

### 3.1 New file `src/Physics/RigidBodyJointGraph.h`

```cpp
#pragma once
#include "../Assets/PhysicsData.h"

#include <cstdint>
#include <vector>

namespace gte {

// One edge of the RigidBody/Joint graph, from ONE endpoint's own point of
// view (see RigidBodyJointGraph::neighbors below - each real Joint produces
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
```

### 3.2 New file `src/Physics/RigidBodyJointGraph.cpp`

- `Build()`: allocate `m_neighborsByNode.resize(physics.rigidBodies.size())`;
  for each `Joint` in `physics.joints` (in whatever order they happen to be
  stored — this does not affect the final result, see below), validate both
  `rigidBodyAIndex`/`rigidBodyBIndex` are in `[0, rigidBodies.size())` and
  not equal to each other, then push `GraphEdge{B, jointIdx}` onto A's list
  AND `GraphEdge{A, jointIdx}` onto B's list. AFTER the full pass, sort EVERY
  node's own edge list by `(neighborRigidBodyIndex, jointIndex)` ascending —
  this single sort step, done once per node right after construction, is
  what makes the whole module's output independent of `physics.joints`' own
  storage order (a later `Neighbors()` call always returns the same
  sequence, regardless of what order `Build()` originally saw the edges in).
- `ComputeReachabilityFromStaticAnchors()`: collect every `RigidBody` index
  whose `motionType == RigidBodyMotionType::Static` into a queue/visited-set
  seed (sorted ascending first, for determinism), run a standard BFS/DFS
  over `graph.Neighbors(...)` marking every reached node "visited," then
  classify every `RigidBody` with `motionType != Static`: visited →
  `reachableDynamicRigidBodyIndices`, not visited → `orphanedDynamicRigidBodyIndices`
  (both emitted in ascending rigid-body-index order by construction, since
  the classification pass itself iterates `physics.rigidBodies` in order).
  Note that a `Static` rigid body's own reachability is irrelevant to this
  function's output (it is never itself classified into either list) — it
  is purely a BFS seed.

### 3.3 New test file `tests/Physics/RigidBodyJointGraphTests.cpp`

Hand-build small `PhysicsData` fixtures (no `SkeletonData` involved at all —
this module has no such dependency) covering:

- **`LinearChainAllReachableFromSingleStaticAnchor`** — Static(0) —
  Joint — Dynamic(1) — Joint — Dynamic(2): both 1 and 2 land in
  `reachableDynamicRigidBodyIndices`, `orphanedDynamicRigidBodyIndices` empty.
- **`SpiderWebHubWithFourChildrenAllReachableFromOneAnchor`** — one
  Static(0) connected via 4 separate Joints to Dynamic(1..4), each of which
  ALSO has its own Joint further out to a fifth-level Dynamic body, PLUS a
  Joint directly cross-connecting Dynamic(1) and Dynamic(2) (the "ring
  brace") — assert every Dynamic body is reachable, and assert
  `graph.Neighbors(1)` contains BOTH its parent-ward edge to 0 AND its cross
  edge to 2 (proving the graph itself does not discard "extra" edges — Phase
  3 needs both).
- **`OrphanedIslandWithNoStaticAnchorAnywhereIsFlagged`** — two Dynamic
  bodies jointed only to each other, with no Static body anywhere in the
  whole `PhysicsData` at all: both land in `orphanedDynamicRigidBodyIndices`.
- **`MultipleIndependentStaticAnchorsEachOwnTheirOwnReachableSet`** — two
  entirely separate Static-rooted chains in the same `PhysicsData` (no edge
  between them): assert every Dynamic body from BOTH chains lands in
  `reachableDynamicRigidBodyIndices` (this function does not need to know
  WHICH anchor a body is nearest to — that per-anchor assignment is Phase
  3's own job, using `SkeletonData` ancestry, not this module).
- **`OutOfRangeAndSelfLoopJointsAreSkippedNotCrashed`** — a `Joint` with
  `rigidBodyAIndex = 99` (out of range) and another with
  `rigidBodyAIndex == rigidBodyBIndex` (self-loop): assert `Build()` does not
  throw/crash and produces a graph identical to one built without those two
  malformed joints at all.
- **`ResultIsIdenticalRegardlessOfRigidBodyAndJointStorageOrder`** — build
  the SAME logical graph (a 4-5 node linear-plus-branch shape) twice: once in
  "natural" order, once with `physics.rigidBodies`/`physics.joints` both
  reversed/shuffled (indices renumbered consistently so the same LOGICAL
  bodies/joints exist, just stored in a different order/numbering) — assert
  `ComputeReachabilityFromStaticAnchors()`'s two output SETS (compare as
  `std::set`, not vector order) are identical between the two runs. This is
  the concrete regression test proving the user's own "order-independent but
  fully deterministic" requirement (`PHASE0_MASTER_STRATEGY.md`, Step 1)
  holds for this module.

### 3.4 Build wiring

Add `src/Physics/RigidBodyJointGraph.cpp` to `CMakeLists.txt`'s main source
list (alongside the other `src/Physics/*.cpp` entries, e.g. right next to
`src/Physics/DynamicChainDetection.cpp`, line ~250) and add
`Physics/RigidBodyJointGraphTests.cpp` to the same `CMakeLists.txt`'s test
source list (alongside `Physics/DynamicChainDetectionTests.cpp`, line
~1212), matching this codebase's own existing flat-list convention exactly
(no separate test `CMakeLists.txt` to touch — confirmed by inspecting how
`DynamicChainDetectionTests.cpp` itself is wired in).
