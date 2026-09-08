# PHASE5 Completion Report — End-to-End Regression Test and Full Build-Registration Sweep

Campaign: `task_manager/verlet-integration-10/` (parent: `PHASE0_MASTER_STRATEGY.md`)
Phase document executed: `PHASE5_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md` (v2)
Date: 2026-09-08

## Summary

Implemented PHASE5 of the verlet-integration-10 campaign — the campaign's
final phase. Added the new full-pipeline end-to-end regression test proving
PHASE1 (PMX collision-group/layer filtering), PHASE2 (a joint's own PMX
rigid-body shape as its collision radius), and PHASE3 (collision enabled ON
by default) all work correctly TOGETHER through the REAL
`PhysicsSystem::RegisterDynamicChains()` -> `AttachDynamicChainRigIfNeeded()`
-> `Update()` pipeline; fixed the pre-existing PHASE1 test defect flagged by
PHASE2/PHASE3/PHASE4's own completion reports
(`DynamicChainSolverCollisionGroupFilterTests.JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide`);
performed the full `CMakeLists.txt`/documentation audit sweep this campaign's
own plan requires; and — since this is explicitly the phase the task
instructions call out as the one allowed to run a full build/regression —
**ran a full clean build of the entire project AND the full `ctest` suite**,
which uncovered and required fixing **two genuine pre-existing-test
regressions** that none of PHASE1-4's own compile-only/partial-subset
verification had caught. All 961 tests now pass (100%).

**The original user request — "right now collision only occurs against
static rigid bodies... please make it follow the pmx defined rigid body...
use pmx defined rigid body layer rule with collision map... also by default,
turn collision to ON" — is now fully resolved and verified end to end,
through a real, full clean build and the complete regression suite.**

## Files Changed

### Tests (new)

- `tests/Game/Physics/PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp`
  — the campaign's own final full-pipeline proof, mirroring
  `PhysicsSystemMultiShapeColliderTests.cpp`'s own "deep initial penetration,
  zero gravity/stiffness, deterministic outcome" fixture-construction
  discipline. Two tests:
  - `GroupMaskFilteringAndJointRadiusWorkTogetherWithDefaultOnCollision` — a
    synthetic model with a Static anchor and TWO independent Dynamic joints
    (both direct children of the anchor, so neither's own collision
    resolution can tug the other via a shared distance constraint — see
    "Notable Incident" below for why this shape was necessary), one Static
    Sphere collider (group 6) and one Static Box collider (group 0, an
    ordinary "sanity control" both joints must avoid normally). Joint 1's own
    PMX rigid body refuses group 6 (blocks Collider A) while joint 2's is
    unrestricted. Asserts, in order: (1) the freshly-detected chain's own
    `collisionEnabled` is already `true` with zero explicit opt-in anywhere
    in the fixture (PHASE3's default proven end to end); (2) joint 2 ends up
    outside BOTH colliders; (3) joint 1 ends up outside Collider B but
    GENUINELY still penetrates Collider A (the group/mask filter's own
    "this actually changed the outcome" regression guard, mirroring
    `PhysicsSystemMultiShapeColliderTests.cpp`'s own
    `SameFixtureWithCollisionDisabledDoesPenetrate...` precedent); (4) both
    joints' own push-out distance from Collider B is inflated by their own
    PMX-derived `collisionRadius` (0.2), confirmed numerically against the
    bare zero-radius half-extent.
  - `OutOfRangeGroupValueProducesIdenticalOutcomeThroughFullPipeline` — the
    v2-mandated (`PHASE0_MASTER_STRATEGY.md` Revision Notes finding #2)
    full-pipeline masking regression: runs the same fixture twice (once with
    a legitimate `group = 5`, once with an out-of-documented-range
    `group = 21`, whose low 4 bits alias the same value) and confirms the
    final resolved joint position is IDENTICAL between the two runs (and
    finite) — this closes the gap PHASE1's own solver-level unit test could
    never fully prove: that masking is applied consistently through the
    ENTIRE data path (`PhysicsData::rigidBodies` -> `DetectDynamicChains()`
    -> `PhysicsSystem::Update()`'s own resolved `Collider`s), not just inside
    `DynamicChainSolver.cpp`'s own `GroupBit()`.

### Tests (fixed)

- `tests/Physics/DynamicChainSolverCollisionGroupFilterTests.cpp` —
  `JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide` (flagged as a
  pre-existing, PHASE1-introduced geometry-selection defect by PHASE2/3/4's
  own completion reports) rebuilt using the same "deep initial penetration,
  zero gravity, zero stiffness" deterministic technique
  `PhysicsSystemMultiShapeColliderTests.cpp` already established, instead of
  a settling gravity pendulum whose natural resting position happened to
  already sit outside the test's own collider regardless of whether the
  group filter did anything at all. Now passes, and genuinely proves the
  filter (confirmed both by re-running it and by the fact that its sibling
  `JointAndColliderInSameGroupDoCollide` test, unchanged, still passes).
- `tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp` — **fixed a
  genuine regression this campaign's own PHASE1 introduced but never
  detected** (see "Notable Incident" below): `MakeRigidBody()` now sets
  `body.collisionGroupMask = 0xFFFF` unconditionally, restoring this file's
  own pre-PHASE1 "collides with everything" behavior for every rigid body it
  builds (joints and Static colliders alike).
- `tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp` — same
  root cause, same fix: `jointBody`/`secondJointBody`/`colliderBody` in
  `BuildFixture()` now explicitly set `collisionGroupMask = 0xFFFF`.

### Build registration

- `tests/CMakeLists.txt` — new test file added to `GTE_TEST_SOURCES`
  immediately after `Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp`
  (its own closest sibling/template), plus a matching taxonomy-comment entry.

## Notable Incident — a genuine, previously-undetected regression found by this phase's own full-suite run

While proving "zero regression in any pre-existing test" (this campaign's own
cross-cutting rule), the full `ctest` run (not merely a compile check or a
partial `--gtest_filter` subset — the verification level PHASE1-4 all
deferred to this phase) turned up **4 failing pre-existing tests**, all
traced to the exact same root cause:

`Assets/PhysicsData.h::RigidBody::collisionGroupMask`'s own default
member-initializer is `0` ("collides with nothing" — a real PMX file
legitimately CAN author this, and `ModelColliderDetectionGroupMaskTests.cpp`'s
own `DefaultZeroGroupAndDefaultZeroMaskAreStillCopiedVerbatimNotSilentlyReplaced`
test already correctly proves this must be preserved verbatim, never silently
overridden to `0xFFFF`, when it flows from a REAL `RigidBody`). This is
**intentionally different** from `Collider`/`DynamicJointSettings`'s own
"collides with everything" `0xFFFF` default, which only ever applies to a
struct that was *never* populated from a real `RigidBody` at all (a
hand-built fixture that skips PHASE1's own copy step entirely, or a chain
whose Step G found no matching rigid body). Two pre-existing test files
(`PhysicsSystemMultiShapeColliderTests.cpp`, `PhysicsSystemModelColliderResolutionTests.cpp`
— both from the prior `verlet-integration-9` campaign, predating PHASE1's own
existence) construct real `RigidBody` fixtures for their joints (and, in one
case, their Static collider too) WITHOUT ever setting `collisionGroupMask`,
implicitly relying on collision-group filtering not existing yet. Once
PHASE1's `DynamicChainDetection.cpp` (Step G) and `ModelColliderDetection.cpp`
started copying this field verbatim into `DynamicJointSettings`/
`ModelColliderDefinition` (exactly as designed), these two files' own
never-configured `0` silently became "this joint refuses to collide with
literally anything" — collision resolution simply stopped happening for
their fixtures, without ever throwing, crashing, or being caught by any
compile-only check.

**Why PHASE1's own "Step 4 — Confirmed zero regression" audit missed this:**
that audit (see `PHASE1_PMX_COLLISION_GROUP_LAYER_FILTERING.md`, Step 4)
explicitly swept every `Collider{...}`/`DynamicJointSettings{...}` positional
aggregate-init call site across the test suite — correctly proving THOSE
stay backward-compatible — but never swept `RigidBody{...}`/hand-built
`RigidBody` field-assignment sites feeding through the REAL detection
pipeline, a different construction pattern entirely, used only by the
`Game/Physics/` end-to-end test tier. This is precisely the class of gap this
campaign's own final phase exists to catch — closed here, not deferred
further.

**Fix:** each affected `RigidBody` (joints in both files, plus the one Static
collider in `PhysicsSystemModelColliderResolutionTests.cpp`'s own fixture)
now explicitly sets `collisionGroupMask = 0xFFFF`, reproducing each file's own
original, pre-PHASE1 "collides with everything" behavior byte-for-byte — with
neither file's own actual test *logic* touched at all, only the fixture data
that was silently relying on a default value whose meaning changed underneath
it.

A second, unrelated, self-corrected incident: this phase's own new end-to-end
test's FIRST fixture revision made joint 2 a skeleton CHILD of joint 1 (a
single-strand 2-joint chain) — this coupled their two collision-resolution
outcomes through a shared distance constraint, producing an unstable,
drifting 200-step result instead of the intended clean, deterministic one
(confirmed by a temporary debug print showing a joint ending up ~0.6-0.8
units away from its own bind position along an axis no force in the fixture
should have moved it along). Diagnosed by re-running the isolated failing
test with temporary `std::fprintf` diagnostics, then fixed by making joint 2 a
DIRECT sibling child of the anchor instead (two independent single-joint
branches sharing one anchor) — this is now documented directly in the test
file's own header comment as a warning against reintroducing that coupling.

## Verification

- **Step 3.3 audit (CMakeLists.txt/documentation sweep):**
  - `git status --porcelain` confirms this campaign added **zero new
    production `.h`/`.cpp` files** — the root `CMakeLists.txt` has zero
    changes; only `tests/CMakeLists.txt` and test `.cpp` files were touched.
  - Every new PHASE1/PHASE2/PHASE5 test file (`DynamicChainSolverCollisionGroupFilterTests.cpp`,
    `ModelColliderDetectionGroupMaskTests.cpp`, `DynamicChainDetectionJointRadiusTests.cpp`,
    `PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp`) is present in
    `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, confirmed by direct
    inspection.
  - `search_in_dir` sweep for `1u << group`/`1u << joint.group`/
    `1u << collider.group` (unmasked) across `src/` found only comment
    mentions describing the masking requirement — every real shift site
    (`DynamicChainSolver.cpp`, `InspectorPanel.cpp`, `BoneViewerWindow.cpp`)
    uses the masked `1u << (X.group & 0x0Fu)` form.
  - `RigidBodyEntry{` sweep: still exactly one construction call site
    (`BoneViewerWindow.cpp`), confirmed to pass exactly 9 positional
    arguments in the struct's own current field order
    (`name, translate, rotateRadians, shape, shapeSize, boneIndex, group,
    motionType, collisionGroupMask`).
- **`AGENTS.md` Job System table (Step 3.4):** confirmed "nothing to do" —
  this campaign added no new `.cpp` files to `src/Physics/`; every new
  function (`GroupsMayCollide()`/`GroupBit()`, `DeriveJointCollisionRadius()`)
  lives inside an already-listed file (`DynamicChainSolver.cpp`,
  `DynamicChainDetection.cpp`), and the `src/Physics/*` row already names
  both.
- **Full clean build** (per this phase's own explicit "this is the phase
  allowed to do a full build" instruction): `cmake --build build` (default,
  all targets) — succeeds, zero errors/warnings, including
  `GreatTamanaEngine.exe` itself.
- **Full regression suite**: `cd build && ctest -C Debug` — **100% tests
  passed (961/961)**, ~62 seconds. This is a genuine increase in count from
  the "521 tests" baseline cited in earlier campaign reports (unrelated,
  ongoing engine-wide test growth across other modules since then) — the
  headline result is 0 failures, not the raw count.
- Re-ran the full pre-existing `tests/Physics/{DynamicChainSolverCollisionGroupFilterTests,
  ModelColliderDetectionGroupMaskTests,DynamicChainDetectionJointRadiusTests,
  SphereColliderTests,BoxColliderTests,DynamicChainSolverTests}Tests.cpp` and
  `tests/Game/Physics/{PhysicsSystemMultiShapeColliderTests,
  PhysicsSystemModelColliderResolutionTests,
  PhysicsSystemCollisionGroupAndRadiusEndToEndTests}Tests.cpp` subset directly
  via `--gtest_filter` before the full suite run, to isolate/confirm each fix
  individually as it was made.

## Campaign Completion Checklist (from `PHASE5_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md`, Step 4)

- [x] PHASE1: `Collider`/`ModelColliderDefinition`/`DynamicJointSettings` all
      carry correct trailing `group`/`collisionMask` fields; PMX data flows
      from `RigidBody` through to the resolved `Collider` every frame;
      `DynamicChainSolver.cpp`'s collision loop filters by
      `GroupsMayCollide()` (shift-safe `GroupBit()`) before calling
      `SolveCollision()`; all PHASE1 tests pass, including the out-of-range-
      `group` regression test and the geometry fix this phase applied; zero
      regression in any OTHER pre-existing test.
- [x] PHASE2: `VerletParticle`/`DynamicJointSettings` carry `collisionRadius`;
      `DeriveJointCollisionRadius()` correctly maps Sphere/Capsule/Box PMX
      shapes to a safe scalar radius; `SolveSphereCollision()`/
      `SolveBoxCollision()` correctly inflate by it; `SolveCapsuleCollision()`
      inherits it for free; all PHASE2 tests pass; zero regression.
- [x] PHASE3: `DynamicChainDefinition::collisionEnabled` defaults to `true`;
      the one affected test explicitly sets the field instead of relying on
      the old default; every other `collisionEnabled`-referencing test
      confirmed unaffected; the `anyChainWantsCollision` performance
      consequence remains documented.
- [x] PHASE4: `InspectorPanel.cpp`'s two collision readouts report an
      accurate, group/mask-aware reachable-collider count and each joint's
      own read-only collision radius; `BoneViewerWindow.h`'s
      `RigidBodyEntry::collisionGroupMask` is appended AFTER `motionType`,
      and its one construction call site compiles with exactly 9 positional
      arguments (re-confirmed this phase).
- [x] PHASE5: the new full-pipeline end-to-end test passes, proving (a) a
      freshly-detected chain has `collisionEnabled == true` with no explicit
      opt-in, (b) PMX group/mask rules genuinely change which colliders a
      joint avoids, (c) a joint's own PMX-derived radius genuinely inflates
      its avoidance margin, and (d) an out-of-range `group` value never
      crashes and is masked consistently through the FULL pipeline; the
      `CMakeLists.txt`/documentation audit found zero gaps; **and the full
      clean build + complete regression suite (961/961) passes, including
      two genuine pre-existing-test regressions this phase discovered and
      fixed that no earlier phase's own compile-only verification could have
      caught.**

**The original user request is now fully and correctly resolved and verified
end to end: hair/skirt physics joints now carry their own PMX rigid-body
shape as a real collision radius, respect PMX's own collision-group/layer
rule (including safely against a malformed/out-of-range `group` byte), and
collision is enabled by default for every freshly-detected chain — all
proven together, through the real simulation pipeline, with a full clean
build and a 100%-passing regression suite.**
