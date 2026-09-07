# PHASE1 — Completion Report: Tree-Based Chain Data Model + Solver/Resolver Generalization

Status: **DONE**. Branch: `feature/physics-from-scratch`. Scope executed exactly
per `PHASE1_TREE_BASED_CHAIN_DATA_MODEL_AND_SOLVER.md` (v2) — this phase touched
ZERO detection-algorithm code (`DynamicChainDetection.h/.cpp` untouched), only the
DATA MODEL and its two runtime consumers.

## What changed

### `src/Physics/DynamicChainDefinition.h/.cpp`
- Added `ExtraStructuralConstraint` (new struct): `jointIndexA`/`jointIndexB`
  (positions within `jointBoneIndices`) + `restLength` — a pure Verlet
  `SolveDistanceConstraint()` pairing that never drives bone rotation (the
  "web brace" edges an MMD skirt's authored Joints add between sibling/
  unrelated bones).
- `DynamicChainDefinition` gained:
  - `std::vector<std::int32_t> parentJointIndex` — index-aligned with
    `jointBoneIndices`; `parentJointIndex[i]` is the POSITION (within
    `jointBoneIndices`) of joint `i`'s own tree-parent joint, `-1` meaning
    "my parent is `rootBoneIndex` directly". This is what replaces the old
    implicit `jointBoneIndices[i-1]` assumption with an explicit tree, so a
    single chain can represent genuine branching (a spider-web skirt hub)
    instead of being split one-chain-per-branch.
  - `std::vector<ExtraStructuralConstraint> extraConstraints` — may be empty
    (the common case for a plain linear hair/tail chain).
  - `static std::vector<std::int32_t> MakeLinearParentIndices(std::size_t jointCount)`
    — convenience helper producing the old flat-list shape
    (`{-1, 0, 1, ..., jointCount-2}`), for hand-built test fixtures and any
    future single-strand-only caller. Implemented in the `.cpp`.
  - `restLengths`' doc comment updated to describe the new
    `parentJointIndex`-relative meaning instead of the old `i-1` wording.
- `FindDynamicChainJointByBoneIndex()` needed no change (operates only on
  `jointBoneIndices`).
- **Note on process**: while inserting the new fields I made two consecutive
  `edit_line` line-count miscalculations that briefly deleted the
  `hasHeadCollider`/`headColliderBoneIndex`/`headColliderRadius` members from
  the struct. Caught immediately by re-reading the file after each edit and
  repaired before moving on — the final file (verified by `read_file` and by
  the successful compile below) is correct and complete.

### `src/Physics/BoneChainPhysicsResolver.h/.cpp`
- Header's "IMPORTANT DESIGN NOTE" and per-step prose (step 1) now describe
  parent resolution via `definition.parentJointIndex[i]` instead of the old
  `i==0 ? rootBoneIndex : jointBoneIndices[i-1]` wording.
- `.cpp`: top-of-function guard extended to
  `simulatedJointWorldPositions.size() != jointCount || definition.parentJointIndex.size() != jointCount`
  (malformed/stale data degrades to a no-op instead of an out-of-bounds read).
  Step 1's parent resolution rewritten to read `definition.parentJointIndex[i]`
  and resolve to `rootBoneIndex` (if `< 0`) or
  `jointBoneIndices[parentJointIndex[i]]` — exactly the corrected v2 form
  specified by the strategy doc (no per-iteration `continue` guard — the
  top-of-function size check already guarantees every `i` is in range).

### `src/Physics/DynamicChainSolver.h/.cpp`
- Header's step-3 prose rewritten to describe tree-parent resolution +
  `extraConstraints` relaxation instead of the old `i-1` structural loop.
  Degrade-gracefully doc comment updated to include `parentJointIndex` in the
  four size-matched arrays.
- `.cpp`: top-of-function guard extended with
  `definition.parentJointIndex.size() != jointCount`. The structural-constraint
  loop (`iterations` times) now walks `jointBoneIndices` in ascending order,
  resolving each joint's tree parent via `parentJointIndex[i]` (anchor particle
  if `< 0`, otherwise the parent joint's own particle) — then, AFTER every tree
  edge, relaxes every `extraConstraints` entry via a plain
  `SolveDistanceConstraint()` between its own two referenced joint particles
  (with an out-of-range guard, skipped rather than crashing on malformed data).

## Tests updated/added
- `tests/Physics/DynamicChainDefinitionTests.cpp` — both `MakeTwoChains()`
  fixtures now set `parentJointIndex` via `MakeLinearParentIndices()`; added
  `MakeLinearParentIndicesProducesExpectedShapeForVariousJointCounts` (covers
  jointCount 0/1/3).
- `tests/Physics/BoneChainPhysicsResolverTests.cpp` — every hand-built fixture
  (`BuildSingleJointDefinition()`, the 3-joint chain, both out-of-range tests)
  now sets `parentJointIndex`. Added two new tests:
  - `BranchingTreeRotatesTheSharedParentTowardBothChildrenIndependently` — a
    3-bone skeleton where two joints share the same real skeleton parent
    (`parentJointIndex = {-1, -1}`), proving the resolver doesn't crash/
    misbehave on a shared-parent "hub" and documenting the known/accepted
    "last-processed child wins" limitation inline.
  - `MismatchedParentJointIndexSizeIsIgnoredGracefully` — a stale/empty
    `parentJointIndex` leaves `pose` completely untouched (regression test for
    the corrected top-of-function guard, per PHASE0's Revision Notes finding #7).
- `tests/Physics/DynamicChainSolverTests.cpp` — both hand-built fixtures
  (`BuildThreeJointChainDefinition()`, the single-joint `buildDefinition`
  lambda) now set `parentJointIndex`. Added
  `ExtraStructuralConstraintPullsTwoNonAdjacentParticlesTogether` — a 3-joint
  linear chain plus one `ExtraStructuralConstraint{0, 2, 1.5f}` bracing joint 0
  directly to joint 2; seeds particles far apart, steps 600 fixed frames, and
  asserts the final joint-0↔joint-2 distance converges to within 0.2 units of
  the extra constraint's own `restLength` (proves the new extra-constraint
  loop actually executes and converges, alongside the ordinary structural
  chain constraints it coexists with).
- `tests/Game/Physics/DynamicChainRigCacheTests.cpp` and
  `tests/Game/Physics/PhysicsSystemParallelTests.cpp` — their own hand-built
  `DynamicChainDefinition` fixtures also updated to set `parentJointIndex`
  (via `MakeLinearParentIndices()`), even though this wasn't strictly required
  by Phase 1's own listed Step 3.4 file list — necessary because the new
  size-guard added to both `BoneChainPhysicsResolver.cpp`/`DynamicChainSolver.cpp`
  would otherwise silently turn every chain built by these two files' fixtures
  into a no-op (empty default `parentJointIndex` mismatching a non-empty
  `jointBoneIndices`), which — while not a *compile* failure — would have been
  a silent, undetected functional regression in tests that are specifically
  about proving simulation behavior (serial-vs-parallel byte-identical
  results, cache round-trip of chain data used at runtime).

## Compile check
Ran `cmake --build build --target GreatTamanaEngineTests` (Ninja/MinGW) —
clean build, 23/23 steps succeeded, `gte_core` + `GreatTamanaEngineTests.exe`
linked successfully.

Then ran a scoped (not full-suite) regression pass covering every touched
area: `GreatTamanaEngineTests.exe --gtest_filter=DynamicChain*:BoneChainPhysicsResolver*:PhysicsSystemParallel*`
→ **35/35 tests passed** (0 failures), including all 5 newly-added tests. Per
the workflow rules for this campaign, this is a **quick/scoped** compile+test
check, not the full regression suite (`ctest`) — that is reserved for a later
phase per the task instructions.

## Known, expected, temporary side-effect (not a bug)
`DynamicChainDetection.cpp` (Phase 3's territory, untouched here) still builds
every real, production `DynamicChainDefinition` with a default-empty
`parentJointIndex`. Combined with this phase's new size-guards in
`BoneChainPhysicsResolver.cpp`/`DynamicChainSolver.cpp`
(`definition.parentJointIndex.size() != jointCount` → early-return no-op),
this means **any chain detected by the CURRENT production algorithm will not
actually simulate/pose at runtime until Phase 3 lands** (it will silently do
nothing rather than crash — a "degrades gracefully" outcome, not a corruption
one). This is an accepted, expected consequence of this phased campaign
exactly as `PHASE0_MASTER_STRATEGY.md`'s own "Step 5: Their Role" section
describes ("Phase 3 cannot build a tree-shaped `DynamicChainDefinition`
without Phase 1's `parentJointIndex`/`extraConstraints` fields existing") —
Phase 3 is expected to close this gap by having `DetectDynamicChains()` emit
real `parentJointIndex` values. No behavior change is expected/desired to
`DynamicChainDetection.cpp` in this phase.

## Next
Proceed to `PHASE2_RIGIDBODY_JOINT_GRAPH_ANALYSIS.md` — the pure
`RigidBodyJointGraph` module Phase 3 will consume to decide chain membership/
anchors.
