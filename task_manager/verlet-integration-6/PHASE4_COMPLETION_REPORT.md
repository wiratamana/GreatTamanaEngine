# PHASE4 — Completion Report: Runtime Integration, Cache Updates, and Cross-Codebase Test Fixture Repair

Status: **DONE**. Branch: `feature/physics-from-scratch`. Scope executed per
`PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md` (v2) — the one real call
site (`PhysicsSystem::RegisterDynamicChains()`) and `DynamicChainRigCache::
ModelEntry` are rewired to Phase 3's new `DetectDynamicChains()` return type
(`DynamicChainDetectionResult`), a debug-time disjointness assertion was
added, and every affected test fixture across the codebase was repaired so
the full suite compiles and passes again.

## What changed

### `src/Game/Physics/DynamicChainRigCache.h`
- Added `#include "../../Physics/DynamicChainDetection.h"` (for
  `DynamicChainDetectionDiagnostics`).
- `ModelEntry` gained a new field: `DynamicChainDetectionDiagnostics
  diagnostics;` — carried straight through from `DetectDynamicChains()`'s own
  `DynamicChainDetectionResult::diagnostics`, unmodified. Phase 5's Editor
  visualization will read `diagnostics.orphanedDynamicBoneIndices`/
  `crossChainJointsDropped`/`duplicateBoneRigidBodyAssignmentsDropped` from
  here.
- Replaced the stale "PHASE3 STUB" header comment (which described a
  not-yet-real chain detection from `verlet-integration-1`'s own Phase 3/4 —
  chain detection has been real since that campaign, and fully rewritten by
  `verlet-integration-6`'s Phase 3) with an accurate note that
  `Register()`/`TryGet()`/`TryGetMutable()` are unchanged by this campaign,
  only `ModelEntry`'s own shape gained the new field.

### `src/Game/Physics/PhysicsSystem.cpp`
- `#include <cassert>` and `#include <unordered_set>` added.
- `RegisterDynamicChains()` now calls `DetectDynamicChains()` and receives a
  `DynamicChainDetectionResult` (`detection`) instead of a bare
  `std::vector<DynamicChainDefinition>`.
- A `#ifndef NDEBUG` block asserts every two chains' own `jointBoneIndices`
  sets are disjoint (Culprit E's defensive re-verification) right after
  detection, before anything is cached — a debug-time-only safety net for the
  parallel-dispatch invariant `PhysicsSystem::Update()`'s job-batch path
  relies on.
- `entry.diagnostics = std::move(detection.diagnostics);` is the one new line
  threading Phase 3's diagnostics through to the cache — `entry.chains`/
  `entry.skeleton` are otherwise unchanged.
- Updated the function's own header comment to describe the new
  RigidBody/Joint-graph-driven algorithm (replacing the stale
  `Bone::deformAfterPhysics`/`RigidBody::motionType` wording) and
  cross-reference `task_manager/verlet-integration-6/PHASE0_MASTER_STRATEGY.md`.

### Test fixture repair
- `tests/Game/Physics/DynamicChainRigCacheTests.cpp` and
  `tests/Game/Physics/PhysicsSystemParallelTests.cpp` were found **already**
  carrying `chain.parentJointIndex = DynamicChainDefinition::
  MakeLinearParentIndices(...)` on their hand-built fixtures (this had
  already been done, alongside Phase 1's own three fixture files, before this
  phase started) — no further edit was needed for either file's own chain
  construction. Confirmed both still compile/pass against Phase 3's REAL
  output shape (not just Phase 1's hand-rolled fixtures), per this phase's own
  3.5 verification checkpoint.
- Added one new, encouraged-by-the-v2-plan test to
  `DynamicChainRigCacheTests.cpp`:
  `RegisteredDiagnosticsRoundTripThroughTryGet` — proves
  `ModelEntry::diagnostics` (all three fields) round-trips through
  `Register()`/`TryGet()` byte-for-byte, giving Phase 5 confidence its new
  data source (which starts reading `crossChainJointsDropped`/
  `duplicateBoneRigidBodyAssignmentsDropped` for the first time, not just
  `orphanedDynamicBoneIndices`) is wired correctly before it starts drawing
  anything with it. Required `#include <cstdint>`/`<utility>`/`<vector>` to be
  added to that test file.
- **`tests/Game/Physics/PhysicsSystemTests.cpp`'s
  `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity`** — the
  "additional fallout" Phase 3's own completion report explicitly flagged for
  this phase to fix (it built a `SkinnedMeshData` with no `PhysicsData` at
  all, relying purely on `Bone::deformAfterPhysics` to get a chain
  auto-detected via the OLD algorithm — under the new algorithm this is
  `physics == nullptr` → Step A → an empty result, so `AttachDynamicChainRigIfNeeded()`
  never attaches a `DynamicChainRig` and `ASSERT_NE(rig, nullptr)` failed).
  Fixed by giving the fixture real `PhysicsData`: one `RigidBody`
  (`RigidBodyMotionType::Static`, `boneIndex = 1`, the chain's anchor) and two
  `RigidBody`s (`RigidBodyMotionType::Dynamic`, `boneIndex = 2`/`3`), jointed
  anchor→joint1→joint2 via two `Joint` entries — the skeleton/geometry
  (perpendicular-to-gravity 4-bone rig) is otherwise unchanged. The test's own
  intent (a registered dynamic chain visibly diverges from a pure-FK pose
  under gravity) is preserved exactly, now driven by the same RigidBody/Joint
  graph data every other model in this campaign uses, instead of the retired
  `Bone::deformAfterPhysics` flag.

## Compile check

```
cmake --build build --target GreatTamanaEngineTests
```
completed with **zero errors** — `gte_core` (including the rewritten
`PhysicsSystem.cpp`) and `GreatTamanaEngineTests` both build cleanly, and only
the files actually touched this phase needed to recompile
(`PhysicsSystem.cpp`, `Game.cpp` and a handful of Editor panels that
transitively include `DynamicChainRigCache.h`, plus the three touched test
files).

## Test verification (scoped, not a full regression run — per this campaign's
own "no full build/full regression test yet" rule)

```
tests\GreatTamanaEngineTests.exe --gtest_filter=DynamicChain*:RigidBodyJointGraph*:BoneChainPhysicsResolver*:PhysicsSystem*
[==========] 57 tests from 10 test suites ran. (159 ms total)
[  PASSED  ] 57 tests.
```

This covers every test suite this phase touches or depends on:
`DynamicChainDetectionTests`/`DynamicChainDetectionDeathTest` (Phase 3),
`RigidBodyJointGraphTests` (Phase 2), `DynamicChainDefinitionTests`/
`BoneChainPhysicsResolverTests`/`DynamicChainSolverTests`/
`DynamicChainSolverDeathTest` (Phase 1), and this phase's own
`DynamicChainRigCacheTests`/`PhysicsSystemParallelTests`/`PhysicsSystemTests`
— including the previously-failing `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity`,
now passing with real `PhysicsData`.

## Next

Proceed to `PHASE5_EDITOR_VISUALIZATION_TREE_WEB_AND_ORPHAN_GIZMO.md` — update
`BoneViewerWindow.cpp`'s Verlet-mode gizmo to draw the new tree/branch shape,
`extraConstraints` as a "web brace" line style, and orphaned rigid bodies as a
red marker (with a hover tooltip explaining why), plus surface
`crossChainJointsDropped`/`duplicateBoneRigidBodyAssignmentsDropped` as simple
text in the tree pane — all now readable straight off
`DynamicChainRigCache::ModelEntry::diagnostics`, which this phase threaded
through with zero gaps.
