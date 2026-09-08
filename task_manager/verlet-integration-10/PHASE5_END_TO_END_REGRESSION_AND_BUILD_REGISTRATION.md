# PHASE5 (v2) — End-to-End Regression Test and Full Build-Registration Sweep

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2, PHASE3, PHASE4 (this is the campaign's final
phase — it proves everything above works together through the REAL
`PhysicsSystem::Update()` pipeline, not just isolated unit math, and performs
the mandatory registration/documentation audit every campaign in this
codebase ends with)

**v2 change summary:** this phase's own plan was re-verified and found
sound; it gains one new end-to-end assertion (Step 3.1, point 5, new) proving
the PHASE1 v2 shift-safety fix holds even through the FULL pipeline (not just
PHASE1's own isolated unit tests), and the completion checklist (Step 4) now
explicitly calls out verifying all four v2 fixes from
`PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)".

---

## Step 1 — The Goal

Every prior phase in this campaign was verified in isolation (PHASE1's
solver-level group/mask tests call `StepDynamicChain()` directly; PHASE2's
tests call individual `Solve*Collision()` functions or `DetectDynamicChains()`
directly). **None of them yet prove the three features work correctly
TOGETHER, through the full spawn -> detect -> resolve -> step pipeline a real
imported model actually goes through** (`PhysicsSystem::RegisterDynamicChains()`
→ `AttachDynamicChainRigIfNeeded()` → `Update()`), the same standard this
codebase's own prior campaigns (verlet-integration-9's PHASE6) always hold
their own final phase to. **Goal of this phase:** one real, full-pipeline
regression test proving a freshly-detected chain (collision ON by default,
per PHASE3) correctly respects PMX collision-group rules (PHASE1) and its own
joint radius (PHASE2) simultaneously, and remains crash-free/well-defined even
under a malformed `group` value — plus the standard final
CMakeLists.txt/documentation audit.

## Step 2 — The Situation

`tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp` (from
verlet-integration-9) is the closest existing precedent and template for this
phase's new test file: it already builds a synthetic model with a Static
anchor, a 3-joint Dynamic chain, and three separate Static Sphere/Box/Capsule
rigid bodies, drives it through the REAL `PhysicsSystem::Update()` loop, and
independently re-derives (never calling `SolveSphereCollision()`/
`SolveBoxCollision()`/`SolveCapsuleCollision()` directly)
`IsOutsideSphere()`/`IsOutsideBox()`/`IsOutsideCapsule()` verification
helpers. This phase's own new test file follows the exact same shape/
discipline, extended with PMX `group`/`collisionGroupMask` values on both the
Dynamic joint bodies and the Static collider bodies, and a deliberately
non-trivial `shapeSize` on the Dynamic joint bodies (so PHASE2's own
radius-derivation genuinely has non-zero effect to verify).

## Step 3 — The Plan

### 3.1 — New file `tests/Game/Physics/PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp`

Write a fresh, local, richer `MakeRigidBody()`/`MakeJoint()` fixture helper
pair (per this campaign's own established "reuse vs. reinvent locally"
convention — do not attempt to import/extend another test file's own private
helpers), supporting `shape`/`shapeSize`/`group`/`collisionGroupMask`
parameters. Build a synthetic model:

- One Static anchor rigid body (bone 0, e.g. `下半身`-equivalent), `group = 0`,
  `collisionGroupMask = 0xFFFF` (irrelevant for an anchor — anchors are never
  themselves collision-tested).
- A 2-joint Dynamic chain (bones 1, 2), each with its own Dynamic rigid body:
  - Joint bone 1's rigid body: `RigidBodyShape::Sphere`, `shapeSize.x = 0.2`
    (a deliberately non-trivial joint radius for PHASE2's derivation to
    matter), `group = 5`, `collisionGroupMask` = every bit EXCEPT bit 6 set
    (i.e. this joint explicitly refuses to collide with anything in group 6).
  - Joint bone 2's rigid body: `RigidBodyShape::Sphere`, `shapeSize.x = 0.2`,
    `group = 5`, `collisionGroupMask = 0xFFFF` (collides with everything,
    unlike joint bone 1 — the deliberate contrast this test needs).
- Two separate Static collider rigid bodies, both placed directly in the
  chain's own gravity-driven sag path (mirror
  `PhysicsSystemMultiShapeColliderTests.cpp`'s own "deep initial penetration,
  zero gravity/stiffness, deterministic outcome" fixture-construction
  technique — see that file's own PHASE6 Completion Report `"Deviation and
  why"` section for exactly why a gravity-convergence fixture is unreliable
  and a deep-initial-penetration one is preferred):
  - Collider A: `RigidBodyShape::Sphere`, `group = 6`, `collisionGroupMask
    = 0xFFFF` — group 6 is EXACTLY the group joint bone 1 refuses to collide
    with, but joint bone 2 (unrestricted mask) does not refuse.
  - Collider B: `RigidBodyShape::Box`, `group = 0`, `collisionGroupMask =
    0xFFFF` — a perfectly ordinary, unrestricted collider both joints should
    hit normally, used as this test's own "collision genuinely still works at
    all" sanity control.

Register through the real pipeline (`RegisterDynamicChains()`,
`AttachDynamicChainRigIfNeeded()`, a seeded bind-pose
`ResolvedAnimationPose`), **leaving `collisionEnabled` COMPLETELY UNTOUCHED
on every detected chain** (i.e. do NOT call any helper that force-sets it,
unlike verlet-integration-9's own precedent test helpers, which always did) —
this is the ONE deliberate, load-bearing difference from the
`PhysicsSystemMultiShapeColliderTests.cpp` template, and it is what actually
proves PHASE3's default-true flip end to end: if `DetectDynamicChains()` ever
regressed back to defaulting `collisionEnabled` to `false`, this test would
fail even though its own fixture never explicitly enables anything.

Step the simulation (`physicsSystem.Update(...)`, ~200 fixed steps, zero
gravity, `constraintIterations = 0`, `stiffness = 0`/`damping = 1` on every
joint — the exact same "isolate collision from everything else" recipe both
prior precedent files already established) and assert, independently
re-derived (never calling production `Solve*Collision()`/`GroupsMayCollide()`/
`GroupBit()` directly):

1. **`ASSERT_TRUE` every detected chain's own `collisionEnabled` is `true`**
   BEFORE stepping anything — the explicit, first-class proof that PHASE3's
   default actually took effect for a freshly-detected chain, not merely an
   implicit side effect inferred from the rest of the test passing.
2. After stepping: joint bone 2 (unrestricted mask) ends up outside BOTH
   Collider A and Collider B.
3. After stepping: joint bone 1 (refuses group 6) ends up outside Collider B
   but is NOT required to be outside Collider A (its own group/mask rule
   deliberately allows it to sit anywhere relative to Collider A, including
   penetrating it) — assert this is GENUINELY the case (i.e. joint bone 1
   actually DOES end up penetrating Collider A in this fixture, not merely
   "no assertion was written about it") using the same independently
   re-derived `IsOutsideSphere()`-style helper this campaign's own PHASE1
   document already specifies writing locally — this is the test's own
   "the filter genuinely changed the outcome, not merely failed to break
   anything" regression guard, mirroring
   `PhysicsSystemMultiShapeColliderTests.cpp`'s own
   `SameFixtureWithCollisionDisabledDoesPenetrateProvingTheFixtureNeedsCollision`
   precedent exactly.
4. Both joints' own final distance from Collider B's surface (the
   unrestricted-mask control collider both joints must avoid) is at least
   approximately their own seeded `collisionRadius` (`0.2`, from `shapeSize.x`
   above) GREATER than a bare zero-radius point would have needed — i.e.
   explicitly confirm PHASE2's radius inflation, not merely "outside the
   collider by some unspecified margin," by comparing against the
   ZERO-radius outside-check distance too (assert the joint sits outside the
   collider's surface by at least `collider.radius/halfExtent + 0.2 -
   epsilon`, not merely `collider.radius/halfExtent - epsilon`).
5. **(NEW in v2 — see `PHASE0_MASTER_STRATEGY.md`'s Revision Notes finding
   #2, and PHASE1 v2's own `GroupBit()` fix) Add ONE further sub-case to this
   same fixture (or a small, separate, focused test in this same file)
   proving the group-masking fix holds through the FULL pipeline, not just
   PHASE1's own isolated `StepDynamicChain()`-level unit tests:** give one
   joint (or Static collider) rigid body in this fixture a deliberately
   out-of-documented-range `group` value (e.g. `group = 21`, whose low 4 bits
   `21 & 0x0F == 5` alias a legitimately-authored group-5 value already used
   elsewhere in this same fixture) flowing all the way from
   `PhysicsData::rigidBodies` through `DetectDynamicChains()`/
   `DetectModelColliders()`/`PhysicsSystem::Update()`'s own resolved
   `Collider`s, and confirm (a) the full pipeline runs to completion with no
   crash/assert/sanitizer failure, and (b) the resulting collision outcome
   for that body is IDENTICAL to what the same fixture would produce if that
   body's `group` had instead been authored as the legitimate, in-range value
   `5` directly — i.e. this proves the masking behavior is consistently
   applied everywhere along the full data-flow path, not merely inside
   PHASE1's own solver-level `GroupBit()` unit tests.

### 3.2 — Register the new test file

Add `Game/Physics/PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp` to
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, immediately after
`Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp` (this file's own
closest sibling/template), plus one taxonomy-comment entry describing exactly
what it proves, matching the detail level of every neighboring entry.

### 3.3 — Final CMakeLists.txt registration audit

Confirm, via direct `search_in_dir` sweep across `src/` and `tests/`:

- This campaign added **zero new production `.h`/`.cpp` files** (every
  production change across PHASE1-4 was an in-place edit of an already-
  registered existing file) — explicitly confirm the root `CMakeLists.txt`'s
  `add_library(gte_core STATIC ...)` list needed NO new entries this
  campaign, and that this is genuinely true (re-diff `git status`/`git diff
  --stat` against the root `CMakeLists.txt` — it should show zero changes).
- Every new TEST file from PHASE1 (`DynamicChainSolverCollisionGroupFilterTests.cpp`,
  `ModelColliderDetectionGroupMaskTests.cpp`), PHASE2
  (`DynamicChainDetectionJointRadiusTests.cpp`), and this phase
  (`PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp`) is present in
  `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`.
- A repository-wide search for the OLD `Collider`/`DynamicJointSettings`/
  `RigidBodyEntry` positional-aggregate-init expectation (i.e. confirm no NEW
  code anywhere in this campaign's own edits accidentally relies on an extra
  positional argument being silently absorbed by an unrelated field — re-read
  every `Collider{...}`/`DynamicJointSettings{...}`/`RigidBodyEntry{...}`
  construction this campaign's own edits touched, confirming each one either
  uses designated-initializer-style explicit field assignment for the new
  trailing fields, or the correct total positional-argument count matching
  the struct's own current, real field order — **pay particular attention to
  `RigidBodyEntry`'s own single construction call site in
  `BoneViewerWindow.cpp`, PHASE4's own demonstrated compile-break risk area
  (see `PHASE0_MASTER_STRATEGY.md`'s Revision Notes finding #1) — confirm it
  now has exactly 9 positional arguments, in the struct's own current
  field order**).
- A repository-wide search for `collisionEnabled` confirms exactly the
  three-file footprint documented in PHASE3, Step 2, still holds (i.e. no
  new test this campaign added introduced a fresh raw-default dependency
  outside of this phase's own deliberate, documented one in Step 3.1, point 1
  above — which explicitly ASSERTS the default rather than silently assuming
  it, which is the correct way to depend on a default value in a test).
- A repository-wide search for `1u << group` / `1u << joint.group` /
  `1u << collider.group` (and the `.group)` variants without an intervening
  `&`) across every file this campaign touched (PHASE1's
  `DynamicChainSolver.cpp`, PHASE4's `InspectorPanel.cpp`/
  `BoneViewerWindow.cpp`) confirms EVERY such shift is masked
  (`group & 0x0Fu`) first — this is the single most important audit item new
  in v2 (see `PHASE0_MASTER_STRATEGY.md`'s Revision Notes finding #2); a
  single missed site anywhere would silently reintroduce the undefined-
  behavior risk this entire v2 revision exists to close.

### 3.4 — `AGENTS.md` Job System table (doc-only, if applicable)

This campaign adds NO new `.cpp` files to `src/Physics/` (every new function —
`GroupsMayCollide()`/`GroupBit()` in `DynamicChainSolver.cpp`,
`DeriveJointCollisionRadius()` in `DynamicChainDetection.cpp` — is a small
addition to an ALREADY-listed file in `AGENTS.md`'s own Job System table, not
a new file). Confirm `AGENTS.md`'s existing row(s) for
`DynamicChainSolver`/`DynamicChainDetection` do not need a new file name
appended (they already name these two files) — no edit expected here; state
this explicitly as a confirmed "nothing to do" finding in this phase's own
completion notes, per this codebase's own "never silently skip a check,
always name what was verified" discipline.

## Step 4 — Campaign completion checklist

- [ ] PHASE1: `Collider`/`ModelColliderDefinition`/`DynamicJointSettings`
      all carry correct trailing `group`/`collisionMask` fields; PMX data
      flows from `RigidBody` through to the resolved `Collider` every frame;
      `DynamicChainSolver.cpp`'s collision loop filters by
      `GroupsMayCollide()` (using the shift-safe `GroupBit()` helper) before
      calling `SolveCollision()`; all new PHASE1 tests pass, INCLUDING the
      new v2 out-of-range-`group` regression test; zero regression in any
      pre-existing test.
- [ ] PHASE2: `VerletParticle`/`DynamicJointSettings` carry
      `collisionRadius`; `DeriveJointCollisionRadius()` correctly maps
      Sphere/Capsule/Box PMX shapes to a safe scalar radius;
      `SolveSphereCollision()`/`SolveBoxCollision()` correctly inflate by it;
      `SolveCapsuleCollision()` inherits it for free via delegation with zero
      code change; all new PHASE2 tests pass; zero regression.
- [ ] PHASE3: `DynamicChainDefinition::collisionEnabled` defaults to `true`;
      the one affected test (`DynamicChainSolverTests.cpp`'s
      `CollidersAreIgnoredWhenCollisionEnabledIsFalse`) explicitly sets the
      field instead of relying on the old default; every other
      `collisionEnabled`-referencing test confirmed unaffected; stale
      doc-comment mentions of the old default updated; the accepted
      `anyChainWantsCollision` performance consequence is documented (v2,
      Step 4.1 of that phase).
- [ ] PHASE4: `InspectorPanel.cpp`'s two collision readouts report an
      accurate, group/mask-aware reachable-collider count (masked safely)
      and each joint's own read-only collision radius; `BoneViewerWindow.h`'s
      `RigidBodyEntry::collisionGroupMask` is appended AFTER `motionType`
      (never in the middle), and its ONE construction call site in
      `BoneViewerWindow.cpp` compiles with exactly 9 positional arguments;
      `BoneViewerWindow.cpp`'s overlay updated consistently (either the full
      per-chain-aware overlay dimming using the confirmed real `verletModel`
      variable name and the `(boneIndex, shape, shapeSize)` matching key, or
      the documented smaller fallback note, per that phase's own explicit
      either/or guidance).
- [ ] PHASE5: the new full-pipeline end-to-end test passes, explicitly
      proving (a) a freshly-detected chain has `collisionEnabled == true`
      with no explicit opt-in, (b) PMX group/mask rules genuinely change
      which colliders a joint avoids, (c) a joint's own PMX-derived radius
      genuinely inflates its avoidance margin, and (d) an out-of-range
      `group` value never crashes and is masked consistently through the
      FULL pipeline — all of the user's own original requests, plus this
      campaign's own v2 robustness fix, verified together, through the real
      pipeline; the CMakeLists.txt/documentation audit found zero gaps,
      including the new v2-specific `1u << group` masking sweep (Step 3.3).

**The original user request — "right now collision only occurs against
static rigid bodies... please make it follow the pmx defined rigid body...
use pmx defined rigid body layer rule with collision map... also by default,
turn collision to ON" — is now fully and correctly resolved and verified end
to end, with this v2 revision closing one compile-breaking defect and one
undefined-behavior risk that the v1 plan would otherwise have introduced.**
