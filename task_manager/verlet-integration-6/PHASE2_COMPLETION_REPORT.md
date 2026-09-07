# PHASE2 — Completion Report: RigidBody/Joint Graph Analysis

Status: **DONE**. Branch: `feature/physics-from-scratch`. Scope executed exactly
per `PHASE2_RIGIDBODY_JOINT_GRAPH_ANALYSIS.md` (v2, unchanged from v1 per that
document's own note) — a brand-new, pure, Tier-1-tested module with ZERO
`SkeletonData`/ECS/Editor/GPU dependency. `DynamicChainDetection.h/.cpp`
(Phase 3's territory) was **not** touched at all in this phase, exactly as
planned.

## What changed

### `src/Physics/RigidBodyJointGraph.h` (new)
- `GraphEdge` — one directed view of an undirected Joint edge
  (`neighborRigidBodyIndex` + `jointIndex`).
- `RigidBodyJointGraph` — a plain adjacency-list wrapper around
  `PhysicsData::rigidBodies`/`joints`:
  - `Build(const PhysicsData&)` — validates every `Joint`'s
    `rigidBodyAIndex`/`rigidBodyBIndex` (negative, out-of-range, or
    self-loop edges are skipped entirely, never crash), and records BOTH
    directions of every valid edge.
  - `Neighbors(rigidBodyIndex)` — returns that node's own edge list, sorted
    by `(neighborRigidBodyIndex, jointIndex)` ascending — this is the exact
    step that makes the whole module's output independent of
    `PhysicsData::joints`' own storage order.
  - `NodeCount()`.
- `ReachabilityResult` (`reachableDynamicRigidBodyIndices` /
  `orphanedDynamicRigidBodyIndices`) + `ComputeReachabilityFromStaticAnchors()`
  — a multi-source BFS seeded from every `RigidBodyMotionType::Static` body at
  once, classifying every `Dynamic`/`DynamicAndBoneMerge` body as reachable or
  orphaned. `Static` bodies themselves are never classified into either list
  (pure BFS seeds), matching `DynamicChainDefinition::rootBoneIndex`'s own
  "never itself simulated" convention.

### `src/Physics/RigidBodyJointGraph.cpp` (new)
Implements both functions exactly per the strategy doc's Step 3.2: `Build()`
resizes `m_neighborsByNode` to `rigidBodies.size()`, pushes both directions of
every valid `Joint` edge, then sorts each node's own edge list once;
`ComputeReachabilityFromStaticAnchors()` seeds a `std::queue`-based BFS from
every Static body (ascending index order) and classifies every non-Static body
by its final `visited` state.

### Build wiring
- `CMakeLists.txt` — added `src/Physics/RigidBodyJointGraph.h` +
  `.cpp` to the main `gte_core` source list, immediately after
  `src/Physics/DynamicChainDetection.cpp` (line ~250), per the strategy doc's
  own placement instruction.
- `tests/CMakeLists.txt` — added `Physics/RigidBodyJointGraphTests.cpp` to
  `GTE_TEST_SOURCES`, immediately after `Physics/DynamicChainDetectionTests.cpp`
  (line ~1212).

## Tests added
`tests/Physics/RigidBodyJointGraphTests.cpp` (6 tests, all from the strategy
doc's own Step 3.3 list, verbatim scenarios):
- `LinearChainAllReachableFromSingleStaticAnchor`
- `SpiderWebHubWithFourChildrenAllReachableFromOneAnchor` — also asserts
  `graph.Neighbors(1)` contains BOTH its parent-ward edge (to the Static hub)
  AND its cross "ring brace" edge (to sibling Dynamic body 2), proving no edge
  is ever silently discarded.
- `OrphanedIslandWithNoStaticAnchorAnywhereIsFlagged` — two Dynamic bodies
  jointed only to each other, no Static body anywhere at all.
- `MultipleIndependentStaticAnchorsEachOwnTheirOwnReachableSet` — two
  disconnected Static-rooted chains in one `PhysicsData`.
- `OutOfRangeAndSelfLoopJointsAreSkippedNotCrashed` — an out-of-range
  (`rigidBodyAIndex = 99`) Joint and a self-loop Joint (`rigidBodyAIndex ==
  rigidBodyBIndex`) are both skipped, producing a graph byte-identical to one
  built without them at all.
- `ResultIsIdenticalRegardlessOfRigidBodyAndJointStorageOrder` — the same
  logical 4-node graph built twice (once "natural," once fully
  reversed/renumbered) produces the exact same reachable/orphaned SETS once
  indices are remapped back — the concrete regression test for the campaign's
  "order-independent but fully deterministic" requirement.

## Compile check
Ran `cmake --build build --target GreatTamanaEngineTests` (Ninja/MinGW) —
clean incremental build (5 steps: compile `RigidBodyJointGraph.cpp`, relink
`libgte_core.a`, compile `RigidBodyJointGraphTests.cpp`, relink+copy
`GreatTamanaEngineTests.exe`), no errors/warnings.

Then ran a scoped (not full-suite) regression pass covering every touched/
adjacent area:
`GreatTamanaEngineTests.exe --gtest_filter=DynamicChain*:BoneChainPhysicsResolver*:RigidBodyJointGraph*`
→ **39/39 tests passed** (0 failures), including all 6 newly-added
`RigidBodyJointGraphTests`, plus every pre-existing `DynamicChain*`/
`BoneChainPhysicsResolver*` test from Phase 1 still green (proving this new,
additive module introduced zero regressions anywhere it touches). Per the
workflow rules for this campaign, this is a **quick/scoped** compile+test
check, not the full regression suite (`ctest`) — that remains reserved for a
later phase per the task instructions.

## Known, expected, temporary state (not a bug)
`RigidBodyJointGraph`/`ComputeReachabilityFromStaticAnchors()` are not called
from anywhere in production code yet — this phase's own module is pure raw
material with no consumer until Phase 3 rewrites `DetectDynamicChains()` to
use it for chain membership/anchoring decisions. This matches
`PHASE0_MASTER_STRATEGY.md`'s own "Step 5: Their Role" ordering exactly:
"Phase 3... [cannot] decide chain membership without Phase 2's
graph-reachability analysis."

## Next
Proceed to `PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md` — the heart of
the campaign: rewriting `DetectDynamicChains()` to consume both this phase's
graph/reachability module AND Phase 1's tree-shaped `DynamicChainDefinition`.
