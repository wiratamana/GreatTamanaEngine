# PHASE3 — Completion Report: Skeleton-Aligned Chain Detection Rewrite

Status: **DONE**. Branch: `feature/physics-from-scratch`. Scope executed per
`PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md` (v2) — `DetectDynamicChains()`
(`src/Physics/DynamicChainDetection.h/.cpp`) is fully deleted and rewritten to
traverse the real RigidBody/Joint graph (Phase 2) combined with the skeleton's
own real bone ancestry (never the raw joint graph) to build tree-shaped
`DynamicChainDefinition`s (Phase 1's data model), per this campaign's own
`PHASE0_MASTER_STRATEGY.md`. Its entire old test file was deleted and replaced.
Per this phase's own scope, **only** `DynamicChainDetection.h`, `.cpp`, and its
test file were touched — no other file in the repository was modified.

## What changed

### `src/Physics/DynamicChainDetection.h`
- New `DynamicChainDetectionDiagnostics` struct: `orphanedDynamicBoneIndices`,
  `crossChainJointsDropped`, `duplicateBoneRigidBodyAssignmentsDropped`.
- New `DynamicChainDetectionResult` struct: `chains` + `diagnostics`.
- `DetectDynamicChains()`'s return type changed from
  `std::vector<DynamicChainDefinition>` to `DynamicChainDetectionResult` (its
  parameter list is unchanged).
- Full Step A–H algorithm write-up moved into the header's own doc comment
  (mirrored, in more detail, at the top of the `.cpp`), including the "Known
  Behavior Change" (a model with no PMX RigidBody/Joint data now detects ZERO
  chains, regardless of `Bone::deformAfterPhysics`) and "Known Limitation"
  (a shared-hub bone's last-processed child wins that frame's FK rotation)
  notices carried over from the strategy document.

### `src/Physics/DynamicChainDetection.cpp` — full rewrite
Implements the plan's Steps A–H exactly:
- **A**: `physics == nullptr` → empty result immediately.
- **B**: builds `RigidBodyJointGraph` + `ComputeReachabilityFromStaticAnchors()` (Phase 2).
- **C**: three passes — (1) seeds `orphanedBoneIndices` directly from
  `reach.orphanedDynamicRigidBodyIndices` (the Culprit F fix — this is the
  *only* place a true graph-orphan is ever recorded), (2) builds
  `boneIndexToRigidBodyIndex` (Static bodies, or reachable Dynamic bodies,
  each requiring a valid in-range `boneIndex` — unattached/out-of-range bodies
  are excluded *before* the tie-break), (3) the ascending-iteration-order
  "keep the lowest rigid-body index" tie-break, diagnosing every dropped
  duplicate.
- **D**: an iterative (not naively-recursive) per-bone ancestor walk,
  memoized via `std::unordered_map<int32_t, MemberResolution>`, with a
  per-walk `visitedRealBones` set for cycle detection (`assert(false)` in a
  debug build, mirroring `DynamicChainSolver.cpp`'s own established
  NaN-guard-assert / death-test precedent — see Testing notes below).
  Correctly handles the case where an ancestor bone is itself an
  **unmemoized** Dynamic member (not yet processed by the ascending
  top-level loop, since a real ancestor can have a *higher* bone index than
  its descendant) by folding it into the *same* walk sequence rather than
  requiring true recursion.
- **E**: groups every resolved bone by its own anchor bone index, then
  assembles each anchor's own tree via a breadth-first walk (children visited
  in ascending bone-index order at every level) so a parent always lands at
  an earlier `jointBoneIndices` position than any of its children —
  satisfying `DynamicChainDefinition`'s own `parentJointIndex[i] < i` invariant
  by construction.
- **F**: folds every remaining PMX Joint into an `ExtraStructuralConstraint`
  (de-duplicated per unordered bone-pair) when both endpoints land in the
  same chain but aren't already a tree edge, or records it in
  `crossChainJointsDropped` when its two endpoints land in different chains.
- **G**: discards any assembled chain shorter than
  `defaults.minimumChainLength` (never retroactively orphans its own bones),
  then seeds `jointSettings`/`maxPlausibleRootDelta`/head-collider defaults,
  overriding `mass`/`damping` from each joint's own matched `RigidBody` —
  reusing the old algorithm's own ending logic almost verbatim.
- **H**: sorts the final chain list (ascending `rootBoneIndex`, tie-broken by
  `jointBoneIndices[0]`) and every diagnostic list, for full determinism
  regardless of `PhysicsData::rigidBodies`/`joints`' own storage order.

### `tests/Physics/DynamicChainDetectionTests.cpp` — full rewrite
Every old (deformAfterPhysics-driven) test deleted; 14 new tests added,
covering every scenario the strategy document's own Step 3.3 lists:
linear rig + one anchor; spider-web skirt (4 branches + 1 cross-brace →
1 single tree chain, never 4); a non-hierarchy-aligned sibling joint (proves
Culprit A's hierarchy constraint is enforced, not violated); two independent
anchors (proves disjointness, Culprit E); a true graph-orphan island (the
direct Culprit F regression test) plus the original "hierarchy-unreachable"
orphan test (deliberately redundant, per the strategy doc's own reasoning);
a cross-chain joint (dropped + diagnosed); a duplicate bone/rigid-body
assignment (both the tie-break itself and its diagnostic); unattached
rigid bodies never spuriously colliding; a cyclic skeleton ancestry (split
into an `#ifdef NDEBUG` graceful-result test and an `EXPECT_DEATH` test in
the `#else` branch, mirroring `DynamicChainSolverTests.cpp`'s own established
convention for a debug-only `assert()`); an out-of-range parent index treated
identically to `-1`; the ported "too short" test (with an explicit assertion
that a too-short chain's bones are *not* retroactively orphaned); the null
physics-data case; and a full order-independence test (permutes the
spider-web fixture's `rigidBodies`/`joints` array storage order and asserts
byte-identical chains + diagnostics-as-sets).

One test-authoring bug was found and fixed **during my own verification**
(not a bug in the production algorithm): my first draft of the duplicate-
rigid-body fixture never connected the *duplicate* rigid body to the graph at
all, so it was excluded as unreachable before ever reaching the tie-break —
the test failed with an empty `duplicateBoneRigidBodyAssignmentsDropped`
instead of `{2}`. Fixed by adding a joint connecting the duplicate body to the
anchor (making it graph-reachable, and therefore actually eligible for the
tie-break) — see the fixture's own updated comment. All 14 tests pass after
this fix.

## Compile check

Per this phase's own file scope, only `DynamicChainDetection.h`, `.cpp`, and
its test file were changed. `DetectDynamicChains()`'s return-type change is
a **documented, cross-phase-anticipated breaking change**: `PhysicsSystem.cpp`
(`src/Game/Physics/PhysicsSystem.cpp`, `RegisterDynamicChains()`) is the one
real call site, explicitly assigned to Phase 4
(`PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md`) to update — both
`PHASE0_MASTER_STRATEGY.md` and the Phase 4 document itself state, in
advance, that this exact file "no longer compiles once Phase 3 changes
`DetectDynamicChains()`'s return type." Building
`cmake --build build --target GreatTamanaEngineTests` confirms **exactly**
this, and nothing else:

```
[1/5] Building CXX object CMakeFiles/gte_core.dir/src/Physics/DynamicChainDetection.cpp.obj   -> OK
[2/5] Building CXX object CMakeFiles/gte_core.dir/src/Game/Physics/PhysicsSystem.cpp.obj      -> FAILED (expected - Phase 4's job)
[3/5] Building CXX object tests/.../Physics/DynamicChainDetectionTests.cpp.obj                -> OK
```

The single compile error is precisely the one the strategy documents
predicted (`conversion from 'gte::DynamicChainDetectionResult' to non-scalar
type 'std::vector<gte::DynamicChainDefinition>' requested` at
`PhysicsSystem.cpp:141`) — confirming this phase's own new code (the header,
the `.cpp`, and its test file) compiles cleanly, and the only failure is the
already-anticipated, explicitly-out-of-scope-for-this-phase call site.

**To actually verify the new algorithm's runtime behavior (not just that it
type-checks)**, I temporarily patched `PhysicsSystem.cpp`'s call site
in-memory (extracting `.chains` from the new result, a two-line change) so
the full test binary could link, ran the scoped test suite, found and fixed
the one test-fixture bug described above, then **reverted `PhysicsSystem.cpp`
back to its exact original content** (confirmed via `git diff` showing zero
changes) before finishing — the committed diff for this phase touches only
the three files listed at the top of this report, exactly per this phase's
own scope.

With that temporary patch in place, the full scoped run passed cleanly:

```
tests\GreatTamanaEngineTests.exe --gtest_filter=DynamicChain*:RigidBodyJointGraph*:BoneChainPhysicsResolver*
[==========] 48 tests from 8 test suites ran. (158 ms total)
[  PASSED  ] 48 tests.
```

This includes every pre-existing `DynamicChainDefinitionTests`,
`RigidBodyJointGraphTests`, `BoneChainPhysicsResolverTests`,
`DynamicChainSolverTests`, and `DynamicChainRigCacheTests` test (Phases 1/2's
own suites) still passing unchanged, alongside all 14 new
`DynamicChainDetectionTests`.

## Additional fallout discovered (not fixed here — flagged for Phase 4)

While the temporary patch was in place, I also ran
`--gtest_filter=PhysicsSystem*` as an extra sanity check (beyond this phase's
own required scope) and found **one additional pre-existing test regression
not called out anywhere in the campaign's phase documents**:
`tests/Game/Physics/PhysicsSystemTests.cpp`'s
`RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity` builds a
`SkinnedMeshData` with **no `PhysicsData` at all**, relying purely on
`Bone::deformAfterPhysics` to get a chain auto-detected via the OLD algorithm.
Under the new algorithm this is now `physics == nullptr` → Step A → an empty
result, exactly matching this phase's own explicitly-intentional "Known
Behavior Change" — so `AttachDynamicChainRigIfNeeded()` never attaches a
`DynamicChainRig`, and the test's own `ASSERT_NE(rig, nullptr)` fails.

This is **not a bug in Phase 3's algorithm** — it is the documented,
deliberate consequence of the user's own "fully replace" answer, working
exactly as designed. However, unlike the five test files
`PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md` explicitly lists for
repair (`DynamicChainDefinitionTests.cpp`, `BoneChainPhysicsResolverTests.cpp`,
`DynamicChainSolverTests.cpp`, `DynamicChainRigCacheTests.cpp`,
`PhysicsSystemParallelTests.cpp`), this specific test
(`tests/Game/Physics/PhysicsSystemTests.cpp`) was not named anywhere in the
master strategy's own file survey. **Phase 4 (or a follow-up) will need to
give this test fixture real `RigidBody`/`Joint` `PhysicsData` (instead of
relying on `deformAfterPhysics` alone) for it to keep testing what it was
originally meant to test** — flagging this now so it isn't mistaken for a
surprise regression when Phase 4 runs the fuller test suite.

## Known, expected, temporary side-effect (not a bug)

Exactly as `PHASE0_MASTER_STRATEGY.md`/`PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md`
predict: `src/Game/Physics/PhysicsSystem.cpp` does not compile against this
phase's new `DetectDynamicChains()` signature until Phase 4 updates its one
call site (and threads `DynamicChainDetectionDiagnostics` through
`DynamicChainRigCache::ModelEntry`). This is an accepted, intentional,
temporary state of the tree between Phase 3 and Phase 4 landing — not a
defect in this phase's own deliverable.

## Next

Proceed to `PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md` — rewire
`PhysicsSystem::RegisterDynamicChains()`/`DynamicChainRigCache::ModelEntry`
to the new return type, add the defensive disjointness assertion, repair the
five (now six, see "Additional fallout" above) affected test files, and
re-run the full suite.
