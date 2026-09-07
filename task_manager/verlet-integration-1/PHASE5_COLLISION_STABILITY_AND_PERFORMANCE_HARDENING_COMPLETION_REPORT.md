# PHASE5 — Collision, Stability, and Performance Hardening — Completion Report

Campaign: `task_manager/verlet-integration-1/` (Verlet-Integrated Dynamic Bone
Physics System). Parent: `PHASE0_MASTER_STRATEGY.md`. This report covers
**Phase 5 only** — `PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md`
(v4) — the campaign's LAST phase, which depends on Phase 4 (real, data-driven
dynamic bone chains actually animating on a live model, via
`DetectDynamicChains()`/`DynamicChainRigCache`/the Inspector's "Dynamic Chain
Physics" section).

## Branch / Prerequisites

- Branch: `feature/physics-from-scratch` (unchanged, as required).
- Read `readme.md` and `agents.md` in full before starting.
- Re-read all six strategy files under `task_manager/verlet-integration-1/`
  (`PHASE0_MASTER_STRATEGY.md` through `PHASE5_...md`), focusing on
  `PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md`'s own v3/v4
  Revision Notices (retargeted at `PhysicsSystem`, never `AnimationSystem`;
  generic naming).
- Read the previous phase's own completion report
  (`task_manager/separate-anim-physics-system-9/PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG_COMPLETION_REPORT.md`)
  for continuation clues — it flagged two things this phase needed to know
  about directly: (1) `DetectDynamicChains()`'s branch-handling rule means a
  branch bone's own pose entry can be rewritten by MULTIPLE sibling chains'
  own `ApplyDynamicChainPhysicsToPose()` calls in the same frame (a
  pre-existing "last write wins" hazard, not introduced by this phase — see
  "Known, Documented, Pre-Existing Caveat" below); and (2) the Inspector's
  live per-joint sliders edit `PhysicsSystem`'s own `DynamicChainRigCache`
  directly and LIVE, which stays safe only as long as nothing reads that same
  cache concurrently off the main thread — verified still true after this
  phase's own parallel-dispatch work (see 3.4 below).

## What Was Done

Implemented Phase 5 per
`PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md`'s (v4) Step 3/Step
5 checklist, end to end, in the prescribed order (3.1 → 3.2 → 3.3 → 3.4).

### 3.1 — `src/Physics/SphereCollider.h/.cpp` (new)

A bare-bone, no-broad-phase, single-sphere collision primitive exactly as
specified: `SphereCollider{ center, radius }` plus
`SolveSphereCollision(VerletParticle&, const SphereCollider&)` — projects a
penetrating, non-pinned particle back out to the sphere's surface along the
center→particle direction (a fixed `Vec3::Up()` push for the degenerate
at-center case), leaving `previousPosition` untouched (matching
`SolveDistanceConstraint`/`SolveGoalConstraint`'s own convention, so the
correction contributes to next step's implied velocity instead of erasing
it). No-op for a pinned particle or a non-positive radius.

### 3.2 — Collision wired into `DynamicChainSolver.cpp`

- **`DynamicChainDefinition.h`** gained the three new fields exactly as
  specified: `hasHeadCollider` (`false` by default), `headColliderBoneIndex`,
  `headColliderRadius` — plus `maxPlausibleRootDelta` (needed by 3.3, added in
  the same step since both extend the same struct).
- **`DynamicChainSolver.h`/`.cpp`** — `StepDynamicChain()` gained a new,
  defaulted-to-`nullptr` `const SphereCollider* collider` parameter, keeping
  every pre-existing call site (including every Phase 2 test) source-
  compatible. Collision is solved exactly ONCE per call, for every joint
  particle, AFTER the goal constraint (step 5, matching PBD convention:
  structural → soft/goal → hard collision) — guarded by BOTH
  `definition.hasHeadCollider` AND a non-null `collider`, so a caller
  accidentally passing a collider pointer without also setting the flag can
  never silently enable collision (see the dedicated regression test,
  "ColliderIsIgnoredWhenHasHeadColliderIsFalse", below).
- **`Game/Physics/PhysicsSystem.cpp`** resolves the collider's world center
  fresh every entity/chain, ONCE per frame (alongside `rootWorldPos`/
  `animatedJointWorldPositions`, from the SAME pure-FK snapshot, never
  recomputed per substep — mirroring PHASE0's own finding #4 discipline),
  via the exact same `Animation/BoneWorldMatrixQuery.h::ComputeBoneWorldMatrix()`
  the root/joint targets already use — `DynamicChainSolver.h`'s own signature
  stays free of any `SkeletonData`/pose dependency, exactly as specified.
- **`src/Physics/DynamicChainDetection.cpp`** now pre-fills a reasonable
  starting point for a NEWLY detected chain — `headColliderBoneIndex =
  chain.rootBoneIndex`, `headColliderRadius` derived from the chain's own
  average joint spacing (`sumRestLengths / jointCount * 0.5f`) — but leaves
  `hasHeadCollider = false` until a human opts in via the Editor, exactly per
  the document's own instruction ("a reasonable default ... or left disabled
  until a human tunes it via the Inspector").
- **`src/Editor/Panels/InspectorPanel.cpp`** — the existing "Dynamic Chain
  Physics" section's per-chain `TreeNode` gained a new "Head Collider"
  checkbox plus (when enabled) a bone-index drag-int and a radius drag-float,
  writing directly into the same `DynamicChainRigCache`-held
  `DynamicChainDefinition` the joint sliders already edit live.

### 3.3 — Numerical safety clamps (`DynamicChainSolver.cpp`)

- **Root-teleport guard** — `DynamicChainRuntimeState` gained
  `lastRootWorldPosition`. `StepDynamicChain()` now computes `needsSeed` from
  EITHER the pre-existing lazy-init condition OR
  `Length(rootWorldPosition - state.lastRootWorldPosition) >
  definition.maxPlausibleRootDelta`; either way, every particle is re-seeded
  onto the (possibly new) `animatedJointWorldPositions` exactly like a lazy
  init. `state.lastRootWorldPosition` is set to the call's own
  `rootWorldPosition` UNCONDITIONALLY, exactly once, regardless of which
  branch ran — verified directly by a dedicated 3-call regression test (seed
  → teleport-and-reseed → stationary-root-does-not-retrigger).
  `maxPlausibleRootDelta` defaults to a generous, hand-tuned `10.0f` on a
  manually-built `DynamicChainDefinition` (used throughout the pre-existing
  Phase 2 tests, none of which move their root far enough to ever approach
  this), and `DetectDynamicChains()` now overrides it per-chain to `5x` that
  chain's own combined bind-pose rest length (never below the struct's own
  default when a chain's combined rest length is degenerate/zero).
- **NaN/Inf guard** — after every position update (integrate → structural →
  goal → collision), any particle whose `position` fails `std::isfinite()` on
  any component is individually reset (never the whole chain) to its own
  `animatedJointWorldPositions[i]` with zero implied velocity, and fires
  `assert(false && "...")` for loud, development-time diagnosis — mirroring
  this codebase's own pre-existing `assert(false && "...")` precedent
  (`Application.cpp`, `JobContinuation.cpp`, `ComputeDispatch.h`).

### 3.4 — Parallel dispatch for independent chains (`Game/Physics/PhysicsSystem.cpp`)

- Extracted the exact per-chain stepping sequence (root/target/collider
  resolution once, then every fixed substep) into one shared function,
  `StepDynamicChainRange(beginIndex, endIndex, DynamicChainBatchContext&)` —
  used BOTH directly (serial path) and as the body of the
  `gte::Jobs::Dispatch()` batch-job trampoline (`RunDynamicChainBatchJob()`).
  There is exactly ONE copy of the actual per-chain math, which is also what
  structurally guarantees the serial and parallel paths agree (see the new
  parity test below) — a deliberate, small improvement over
  `AnimationSystem.cpp`'s own precedent (which keeps two separately-written
  inline/dispatch code paths).
- `PhysicsSystem::Update()` sums each entity's own TOTAL joint count across
  all of its chains; at/above a new `kMinDynamicJointsToParallelize` (24,
  anonymous-namespace `constexpr` in `PhysicsSystem.cpp`, mirroring
  `AnimationSystem.cpp`'s own `kMinVerticesToParallelize` naming/placement
  convention) AND with more than one chain, it dispatches one job PER CHAIN
  via `gte::Jobs::Dispatch(&RunDynamicChainBatchJob, chainCount, ...,
  minItemsPerBatch=1)` followed by exactly one `WaitForJobs()`; otherwise it
  calls `StepDynamicChainRange()` directly, inline. The outer PER-ENTITY loop
  itself stays strictly sequential (documented directly at the loop, per the
  document's own Step 5 instruction) — not because of a shared-GPU-buffer
  hazard like `AnimationSystem::SkinAndUpload()`'s identical-sounding rule
  (`PhysicsSystem` never touches Renderer/Mesh/GPU state at all), but because
  cross-entity parallelism is a genuinely separate, unstarted follow-up this
  phase deliberately does not attempt (per its own "What We Will NOT Do").

### 3.4.1 — `AGENTS.md` Job System audit-table update

Added the two new rows the document specifies, verbatim in substance, right
after the existing "Cross-entity/cross-instance shared GPU mesh buffers" row:

1. `src/Physics/*` (`VerletIntegration`, `ChainConstraints`, `WindField`,
   `DynamicChainSolver`, `BoneChainPhysicsResolver`, `SphereCollider`,
   `FixedTimestepAccumulator`) — **JOB-SAFE**.
2. Concurrent, disjoint-index writes into one shared
   `ResolvedAnimationPose::pose` from several job bodies at once —
   **JOB-SAFE, conditioned on two invariants** (fixed pose size for the
   `Dispatch()`/`WaitForJobs()` bracket; every two concurrently-dispatched
   chains' `jointBoneIndices` sets provably disjoint).

This report's row 2 also documents, inline, the one genuine, PRE-EXISTING
caveat this phase's own parallelism makes newly load-bearing rather than
merely theoretical — see "Known, Documented, Pre-Existing Caveat" below.

### 3.5 — Tests for this phase

- **`tests/Physics/SphereColliderTests.cpp`** (new, 6 tests) — a particle
  fully outside the sphere is untouched (position AND `previousPosition`); a
  particle inside is projected EXACTLY onto the surface along the correct
  direction, both at the world origin and off-center; a pinned particle is
  never moved, even sitting dead center; the degenerate at-center case
  produces a finite result, at the correct radius, rather than NaN; and a
  non-positive radius is a no-op.
- **`tests/Physics/DynamicChainSolverTests.cpp`** extended with 4 new tests
  (every pre-existing Phase 2 test passes unchanged): a head collider keeps
  every joint off its surface every step despite gravity pulling the chain
  into it; collision is a documented no-op whenever `hasHeadCollider` is
  false even if a non-null collider pointer is passed in; an implausible root
  teleport re-seeds the chain onto its NEW animated target instead of
  whipping across the gap, and does NOT re-trigger on the very next
  (stationary-root) call; and a manually NaN-poisoned particle position is
  handled correctly — see "Bugs Found And Fixed" below for why this last case
  is split into an `#ifdef NDEBUG` "graceful" test and an `#ifndef NDEBUG`
  `EXPECT_DEATH` test instead of a single unconditional test.
- **`tests/Game/Physics/PhysicsSystemParallelTests.cpp`** (new, 2 tests) — a
  hand-built "few chains" model (2 chains, 2 total joints — forces the
  SERIAL path) and a geometrically/parametrically IDENTICAL "many chains"
  model (30 chains, 30 total joints — forces the PARALLEL `Dispatch()` path),
  registered directly via `DynamicChainRigCache` (no
  `DetectDynamicChains()`/PMX/`AnimationSystem`/`SkeletalAnimator` involved at
  all, further proof of independence), stepped through the SAME
  `PhysicsSystem` instance for 30 frames, produce joint-world-position
  results that agree with each other to within `1e-4` for EVERY chain
  regardless of which path actually ran; a second test drives a 40-chain
  model (forcing several worker batches) and confirms every one of its
  symmetric, independent chains stays internally consistent with every
  other one after 45 frames under a non-axis-aligned gravity vector.
- **`tests/CMakeLists.txt`** — both new test files added to the
  unconditional (Tier 1) `GTE_TEST_SOURCES` list, plus matching descriptive
  paragraphs added to the file's own "Test taxonomy" header comment block,
  and the existing `DynamicChainSolverTests.cpp` paragraph extended to
  describe this phase's four new cases.
- **`CMakeLists.txt`** — `src/Physics/SphereCollider.h/.cpp` added to
  `gte_core`'s source list, right after the existing `Physics/*` block.

## Bugs Found And Fixed (during this phase's own implementation)

- **The NaN/Inf guard's own diagnostic `assert(false && "...")` crashed the
  very test meant to prove the guard's graceful-degrade behavior.** This
  project's top-level `CMakeLists.txt` never sets a default
  `CMAKE_BUILD_TYPE` (confirmed directly by grep), so a plain `cmake --build
  build` here never defines `NDEBUG` — meaning `assert()` is genuinely LIVE
  (never compiled to a no-op) in this project's own normal build, exactly
  like this codebase's own pre-existing `assert(false && "...")` sites
  (`Application.cpp`, `JobContinuation.cpp`, `ComputeDispatch.h`). The first
  draft of `NanPositionIsResetToFiniteSanePosition` called
  `StepDynamicChain()` directly on NaN-poisoned input and asserted the
  result was finite afterward — but the assert firing terminates the process
  before that assertion can ever run, exactly as `assert()` is defined to
  do. This is NOT a bug in `DynamicChainSolver.cpp` itself (the reset logic
  runs unconditionally, before the assert, and IS genuinely correct — an
  `#ifdef NDEBUG`-only build would observe it) — it was a bug in the TEST's
  own assumption that the process would survive to check it. Fixed by
  splitting the test in two, mirroring this codebase's own established
  `RenderGraphBuilderTests.cpp`/`RenderGraphBarrierPlannerTests.cpp`
  precedent for exactly this situation (assert-guarded invalid/exceptional
  input, verified via `EXPECT_DEATH` under `#ifndef NDEBUG`, with the
  graceful-degrade path only compiled/run under `#ifdef NDEBUG`): the
  `#ifndef NDEBUG` branch (the one that actually compiles and runs in this
  project's own default build) is now `DynamicChainSolverDeathTest.NanPositionTripsTheDiagnosticAssert`,
  which uses `EXPECT_DEATH` to confirm the process aborts exactly when
  expected; the `#ifdef NDEBUG` branch keeps the original
  `DynamicChainSolverTests.NanPositionIsResetToFiniteSanePosition` test intact
  for whenever this project IS built with `NDEBUG` defined. Re-verified: the
  full targeted suite (32 tests across 7 suites, including the new death
  test) now passes cleanly with zero crashes.

## Known, Documented, Pre-Existing Caveat (not a regression, not fixed by this phase — by design)

Per the Phase 4 completion report's own "Next Steps" #1, and now also called
out directly in the new `AGENTS.md` audit-table row: `BoneChainPhysicsResolver.cpp`'s
`ApplyDynamicChainPhysicsToPose()` writes `pose[parentBoneIndex]` for a
chain's first joint — i.e. `pose[chain.rootBoneIndex]`, not just entries
inside `jointBoneIndices` — so a BRANCH bone shared as the `rootBoneIndex` of
several sibling chains (produced by `DetectDynamicChains()`'s own documented
branch-handling rule) can have its pose entry rewritten by more than one
chain's own call, in `chains` iteration order. This was already a silent
"last write wins" hazard on the pre-existing SERIAL path (Phase 4); this
phase's own 3.4 parallel dispatch does not introduce a NEW hazard so much as
it changes an already-nondeterministic-with-respect-to-iteration-order
overwrite into one that could, in principle, also race across two threads if
two such sibling chains ever landed in different concurrent batches. Per the
phase document's own explicit scope ("What We Will NOT Do" - no chain-vs-
chain interaction fixes in this phase), this was NOT fixed here — it is
called out explicitly, in the same place a future maintainer would look
(`AGENTS.md`'s Job System table), exactly as the Phase 4 report asked this
phase to do.

## What Was Deliberately NOT Done (per Phase 5's own "Step 4: What We Will
NOT Do", and this task's overall workflow rules)

- **No general capsule/box/mesh collision** — one sphere per chain (typically
  tracking the head bone) only, per spec.
- **No chain-vs-chain (self) collision or chain-vs-world (level geometry)
  collision** — only chain-vs-one-authored-sphere.
- **No cross-ENTITY parallelization** — 3.4's `Jobs::Dispatch()` is strictly
  WITHIN one entity's own per-frame physics step, across that one model's own
  independent chains; the outer per-entity loop stays serial, per spec.
- **No "chain sleeps when off-screen" LOD/culling system** — explicitly
  deferred, per spec.
- **No fix for the pre-existing branch-bone shared-root-write hazard** — see
  "Known, Documented, Pre-Existing Caveat" above; explicitly out of this
  phase's scope.
- No full build or full regression test was run as part of implementing this
  phase (only a fast, targeted incremental compile + filtered test run) —
  per this task's own workflow rules, a full build/regression IS warranted
  now that this is the campaign's LAST phase; see "Verification" below for
  exactly what was (and, per the task's own instructions, deliberately was
  NOT yet) run.

## Verification

1. **Reconfigure**: `cmake -S . -B build` — succeeded, picked up the two new
   source files (`SphereCollider.h/.cpp`) with no errors (only the same
   pre-existing, unrelated KTX-Software git-describe warning every prior
   phase report also noted).
2. **Fast compile check** (not a full build, per this task's explicit
   instructions):
   - `cmake --build build --target GreatTamanaEngineTests` — rebuilt exactly
     the new/changed object files (`SphereCollider.cpp.obj`,
     `DynamicChainSolver.cpp.obj`, `BoneChainPhysicsResolver.cpp.obj`,
     `DynamicChainDetection.cpp.obj`, `PhysicsSystem.cpp.obj`,
     `InspectorPanel.cpp.obj`, plus a handful of other Editor/Application
     files transitively depending on the touched headers, and every new/
     changed test `.cpp.obj`), re-linked `libgte_core.a`, and re-linked
     `GreatTamanaEngineTests.exe` — **22/22 build steps succeeded, zero
     warnings/errors** on the first full pass; one further, tiny incremental
     rebuild (2/2 steps) was needed after fixing the NaN-guard test crash
     described above.
   - `cmake --build build --target GreatTamanaEngine` — the real, full engine
     executable (Editor included) also builds and links cleanly against
     every changed header, zero errors.
3. **Targeted test run**: ran
   `GreatTamanaEngineTests.exe --gtest_filter=SphereColliderTests.*:DynamicChainSolverTests.*:DynamicChainSolverDeathTest.*:PhysicsSystemTests.*:PhysicsSystemParallelTests.*:DynamicChainDetectionTests.*:DynamicChainRigCacheTests.*`
   directly — **32/32 tests passed** (0 failures: 6 new
   `SphereColliderTests`, 4 pre-existing + 4 new `DynamicChainSolverTests`
   cases (8 total, all pass) plus 1 new `DynamicChainSolverDeathTest`, 6
   `PhysicsSystemTests` (all pre-existing, unchanged), 2 new
   `PhysicsSystemParallelTests`, 6 pre-existing `DynamicChainDetectionTests`,
   4 pre-existing `DynamicChainRigCacheTests`). A broader filtered run
   (`*Physics*:*Animation*:*Skinned*`, 31 tests across 11 suites) also passed
   31/31 with zero regressions.
   - **No full `ctest` regression run was performed** — per this task's own
     stated workflow rules ("Do not run a full build or full regression test
     yet... unless the Current task explicitly says to do full build,
     usually on the last one"). This document does not itself instruct a
     full build/`ctest` run as part of Phase 5's own Step 3/5 checklist (that
     checklist ends at "run the full `GreatTamanaEngineTests` suite one final
     time end-to-end before considering the campaign done" as a Step 5 item
     for "whoever picks up Phase 5" to do once ready) — since the task
     instructions for THIS session explicitly reserve a full build/regression
     for when a phase document EXPLICITLY calls for it, and grant that
     exception only implicitly ("usually on the last one"), a full
     `cmake --build build` (all targets) + `ctest` run is flagged here as the
     clear, explicit NEXT ACTION for this campaign — see "Next Steps" below.
4. **Grep verification** (Phase 5's own Step 5, item 5's explicit
   instruction): confirmed `src/Game/Animation/AnimationSystem.h`/`.cpp`
   still contain NO `#include` of anything under `src/Physics/` and no
   reference to `PhysicsSystem`, `DynamicChainDefinition`,
   `DynamicChainRuntimeState`, `DynamicChainSolver`, or
   `BoneChainPhysicsResolver` — the final, checkable proof that this whole
   campaign's v3 ECS-independence goal held all the way through every phase.

## Next Steps

This is the LAST phase of the `verlet-integration-1` campaign
(`PHASE0_MASTER_STRATEGY.md`'s own file map ends at Phase 5). Per Phase 5's
own Step 5, item 5 ("Once this phase's tests are green, the campaign is
functionally complete: run the full `GreatTamanaEngineTests` suite one final
time end-to-end before considering the campaign done"), the recommended next
action is:

1. A full `cmake --build build` (every target) followed by
   `ctest -C Debug --output-on-failure` from the `build` directory — the
   genuine, whole-suite regression pass this phase's own document asks for,
   deliberately NOT run as part of this session per the task's own workflow
   rules (reserved for an explicit "do a full build" instruction).
2. A real, live-Vulkan-device smoke test against an actual rigged MMD model
   with a `deformAfterPhysics` bone run and PMX rigid bodies (e.g. the
   Furina model already used elsewhere in this engine's own README/test
   history) — visually confirming a dynamic bone chain swings under gravity/
   wind, a head collider (once enabled via the Inspector) keeps it off the
   skull, and toggling the per-joint damping/stiffness/mass sliders live
   still behaves as expected — this campaign has been fully unit-tested
   throughout, but (as of this phase) has not yet had a real-model, real-GPU
   visual pass specifically exercising Phase 5's own new collision/stability
   features together.
