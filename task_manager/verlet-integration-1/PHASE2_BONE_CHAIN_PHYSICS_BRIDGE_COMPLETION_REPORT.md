# PHASE2 — Bone-Chain Physics Bridge — Completion Report

Campaign: `task_manager/verlet-integration-1/` (Verlet-Integrated Dynamic Bone
Physics System). Parent: `PHASE0_MASTER_STRATEGY.md`. This report covers
**Phase 2 only** — `PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md` — the bone↔particle
bridge, which depends on Phase 1 (`src/Physics/VerletParticle.h`,
`VerletIntegration.h`, `ChainConstraints.h`, `WindField.h`).

## Branch / Prerequisites

- Branch: `feature/physics-from-scratch` (unchanged, as required).
- Read `readme.md` and `agents.md` in full before starting.
- Re-read all six strategy files under `task_manager/verlet-integration-1/`
  (`PHASE0_MASTER_STRATEGY.md` through `PHASE5_...md`).
- Read the previous phase's own completion report
  (`task_manager/separate-anim-physics-system-9/PHASE1_CORE_VERLET_PHYSICS_FOUNDATION_COMPLETION_REPORT.md`)
  for continuation clues — it confirmed Phase 1's `VerletParticle`/
  `IntegrateParticle`/`SolveDistanceConstraint`/`SolveGoalConstraint`/
  `ComputeWindAcceleration` all compile/test exactly as documented, so Phase 2
  could call them directly with no signature mismatches, and that the
  `IkSolver.cpp` → `Animation/BoneWorldMatrixQuery.h` promotion (Culprit E)
  was still outstanding.

## What Was Done

Implemented Phase 2 per `PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md`'s Step 3/Step 5
checklist, **with one deliberate, verified correction to the phase document's
own pseudocode** (see "A Genuine Bug Found And Fixed" below) — the rest
follows the document exactly.

### 3.1 — Promoted the shared bone-world-matrix query (Culprit E)

- **`src/Animation/BoneWorldMatrixQuery.h`/`.cpp`** (new) — `ComputeBoneWorldMatrix()`,
  extracted **verbatim** out of `IkSolver.cpp`'s anonymous namespace into a
  public header/`.cpp` pair, exactly per Step 3.1.
- **`src/Animation/IkSolver.cpp`** (modified) — the local, `static`-scoped
  copy was deleted; the file now `#include`s the new header instead. Every
  existing call site inside `SolveIkChains()` compiles unchanged (same
  function name/signature, just no longer file-local). `IkSolverTests.cpp`
  was **not modified at all** and passes byte-for-byte unchanged (4/4 tests
  green), proving the extraction altered no behavior.

### 3.2/3.3 — Plain data (no logic)

- **`src/Physics/DynamicChainDefinition.h`** (new) — `DynamicJointSettings`
  (damping/stiffness/mass) and `DynamicChainDefinition` (rootBoneIndex,
  jointBoneIndices, jointSettings, restLengths, gravityScale, windScale,
  constraintIterations), exactly per spec.
- **`src/Physics/DynamicChainRuntimeState.h`** (new) — `DynamicChainRuntimeState`
  (particles, initialized, simulationTimeSeconds) — the first genuinely
  mutable, frame-to-frame-persistent state in the animation stack (Culprit C),
  exactly per spec.

### 3.4 — Chain-level orchestration

- **`src/Physics/DynamicChainSolver.h`/`.cpp`** (new) — `StepDynamicChain()`,
  implementing exactly the five documented steps: lazy init (seeds particles
  onto the animated FK target with zero implied velocity), integrate (gravity
  + wind, scaled locally), constrain-structural (repeated
  `constraintIterations` times, root-anchor-first then chain-to-chain),
  constrain-goal (**exactly once**, after the structural loop — the v2 fix
  from `PHASE0`'s Revision Notes, finding #3, decoupling `stiffness` from
  `constraintIterations`), and advancing the simulation clock. Degrades
  gracefully (early return, no out-of-bounds access) whenever the
  index-aligned arrays disagree in size.

### 3.5 — Position → rotation bridge (**bug found and fixed — see below**)

- **`src/Physics/BoneChainPhysicsResolver.h`/`.cpp`** (new) — `ApplyDynamicChainPhysicsToPose()`.

### 3.6 — Tests (Tier 1, all in this same change)

- **`tests/Animation/BoneWorldMatrixQueryTests.cpp`** (new, 4 tests) — bind-pose
  world matrices match each bone's own bind position; a root translation
  offset propagates to every descendant; results agree **exactly** with
  `ComputeSkinningMatrices()` (`SkeletonPose.h`, already independently tested)
  for the same resolved pose (proving the `IkSolver.cpp` extraction changed
  nothing behaviorally); an out-of-range bone index returns `Identity()`.
- **`tests/Physics/DynamicChainSolverTests.cpp`** (new, 4 tests) — (a) full
  stiffness (1.0) stays glued exactly to the animated target every frame,
  regardless of where the chain started or how the target moves frame to
  frame; (b) zero stiffness sags visibly under gravity but never stretches
  past `sum(restLengths) * 1.01`; (c) 300 repeated steps stay finite/bounded
  and settle to a small implied velocity rather than diverging/oscillating
  forever; (d) a dedicated regression proves `stiffness` and
  `constraintIterations` are genuinely decoupled — the goal step's own
  covered-fraction of the remaining (pre-goal → target) distance equals
  `stiffness` exactly, identically for `constraintIterations == 1` and `== 8`,
  cross-checked against an independently hand-computed reference "pre-goal"
  position (PHASE0 Revision Notes finding #3).
- **`tests/Physics/BoneChainPhysicsResolverTests.cpp`** (new, 5 tests) — see
  below; a genuine round-trip test (not just "it compiles"), a
  target-already-aligned no-op case, a 3-joint root-to-tip dependency-order
  case, and two malformed-data graceful-skip cases (out-of-range joint bone,
  out-of-range/absent root bone).
- **`tests/CMakeLists.txt`** — all three new test files registered in the
  unconditional (Tier 1) `GTE_TEST_SOURCES` list, plus a matching descriptive
  paragraph for each in the file's own "Test taxonomy" header comment block
  (PHASE0 v2 Revision Notes finding #6).
- **`CMakeLists.txt`** — all six new/modified source files added to
  `gte_core`'s source list (`BoneWorldMatrixQuery.h/.cpp` alongside the
  existing `Animation/IkSolver.*` entries; `DynamicChainDefinition.h`,
  `DynamicChainRuntimeState.h`, `DynamicChainSolver.h/.cpp`,
  `BoneChainPhysicsResolver.h/.cpp` alongside the existing `Physics/*` block).

## A Genuine Bug Found And Fixed (Step 3.5)

While implementing `ApplyDynamicChainPhysicsToPose()` exactly as literally
specified in `PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md` Step 3.5 (writing the
corrective rotation into `pose[boneIndex]`, i.e. `jointBoneIndices[i]` — the
SAME bone whose position we're trying to match), the phase document's own
**required** round-trip test (Step 3.6: "confirm `ApplyDynamicChainPhysicsToPose()`
produces a `BoneLocalOffset::rotation` that, when fed back through
`ComputeBoneWorldMatrix()`, actually lands the bone at (approximately) the
requested simulated position") **failed outright** — every time, for any
chain, provably by construction:

- Per this engine's own bind-relative local-transform formula
  (`Animation/BonePoseMath.h`'s `ComputeBoneLocalMatrix()`, composed as
  `Translate(localBindOffset) * Rotate(offset.rotation)` — confirmed directly
  against `Math/Mat4.h`'s own `TRS()` doc comment, "Translate * Rotate *
  Scale, in that order"), a bone's world matrix is
  `parentWorld * Translate(bindOffset) * Rotate(rotation)`. Evaluated at the
  origin (`TransformPoint(Vec3::Zero())`, exactly what `ComputeBoneWorldMatrix()`
  is used for throughout this codebase to read "where is this bone"),
  `Rotate(rotation) * Vec3::Zero()` is **always** the zero vector, for
  **any** rotation — so a bone's own `BoneLocalOffset::rotation` can **never**
  move its own reported world position. It can only ever swing its
  **descendants** (a child's world matrix incorporates the parent's full
  local matrix, rotation included, one level down).
- This is exactly the same principle `Animation/IkSolver.cpp`'s own CCD
  solver already depends on and documents implicitly: it rotates a **link**
  bone to swing a **descendant** effector bone toward a target — it never
  writes the effector's own pose entry.
- Verified directly: implementing Step 3.5 literally as written and running
  the required round-trip test produced `[ FAILED ]` for both the 2-bone
  single-joint case and a 3-joint chain case (see evidence below) — not a
  test-authoring mistake, a mathematical impossibility given this engine's
  own TRS composition.

**The fix**: the corrective rotation is instead written into
`pose[parentBoneIndex]` — `definition.rootBoneIndex` for `i == 0`, otherwise
`definition.jointBoneIndices[i - 1]` — i.e. the bone that actually determines
`jointBoneIndices[i]`'s world position. This required one further addition
not in the original pseudocode: converting the new PARENT world rotation back
to its own LOCAL offset needs the parent's own PARENT's ("grandparent",
relative to the joint) world rotation
(`skeleton.bones[parentBoneIndex].parentBoneIndex`, resolved via one more
`ComputeBoneWorldMatrix()` call — gracefully `Identity()` if absent). This
keeps the exact same root-to-tip dependency recurrence the document
describes (iteration `i`'s write is an ancestor of iteration `i+1`'s query
bone, so "each iteration depends on the previous joint's own already-rewritten
pose entry" still holds true), and — as a direct, deliberately verified
consequence — the chain's very last joint (`jointBoneIndices.back()`) never
itself receives a rotation write (nothing needs to swing *its* descendants
for chain purposes); only its ancestors do, which is exactly what's needed to
correctly place every joint including the tip. This is documented in detail
directly in `BoneChainPhysicsResolver.h`'s own header comment ("IMPORTANT
DESIGN NOTE") and in the `.cpp`'s step-by-step comments, and every test in
`BoneChainPhysicsResolverTests.cpp` explicitly documents/asserts against the
PARENT bone's pose entry (never the joint's own) for exactly this reason.

No other phase's deliverable needed re-checking because of this — Phase 1's
`VerletParticle`/`ChainConstraints`/`WindField`/`IntegrateParticle` are
completely unaffected (this bug was isolated entirely to the
position-to-rotation conversion step, never the particle simulation itself),
and `DynamicChainSolver.cpp`'s `StepDynamicChain()` was implemented exactly
per spec and needed no changes — all 4 of its own tests passed on the first
build.

## What Was Deliberately NOT Done (per Phase 2's own "Step 4: What We Will
NOT Do", and this task's overall workflow rules)

- No twist/roll recovery around a bone's own forward axis — a position-only
  particle chain has no roll information; each bone keeps whatever roll its
  bind pose/parent implies (same accepted simplification `IkSolver.cpp`
  already documents for itself).
- `ApplyDynamicChainPhysicsToPose()` does not handle branching chains — a
  `DynamicChainDefinition` stays a single, linear list, exactly per spec.
- Did not re-derive `IkSolver.cpp`'s local-space, many-iteration CCD
  accumulation style — this function applies exactly one corrective
  world-space rotation per bone per frame, per spec.
- **Zero ECS/`AnimationSystem`/`PhysicsSystem` wiring** — every new file in
  this phase is pure, engine-data-only (`SkeletonData`/`BoneLocalOffset`/
  `Math/`), with no `Registry`/`Entity`/GPU dependency at all, exactly per
  Phase 2's own scope (Phase 3 wires this into a live, standalone
  `PhysicsSystem`).
- `Animation/SkeletonPose.cpp`/`MotionSampler.cpp`/`AppendBoneSolver.cpp`'s
  own solving logic, and `IkSolver.cpp`'s solving logic itself (only its
  private helper was extracted, not its algorithm), were not touched.
- No full build or full regression test was run (per this task's explicit
  workflow rules) — only a fast, targeted incremental compile of `gte_core`
  + `GreatTamanaEngineTests`, plus a filtered run of the specific new/touched
  test suites.

## Verification

1. **Reconfigure**: `cmake -S . -B build` — succeeded, picked up every new
   source/test file with no errors (only the same pre-existing, unrelated
   KTX-Software git-describe warning as Phase 1's own report, not caused by
   this change).
2. **Fast compile check** (not a full build): `cmake --build build --target
   GreatTamanaEngineTests` — rebuilt exactly the new/changed object files
   (`BoneWorldMatrixQuery.cpp.obj`, the rewritten `IkSolver.cpp.obj`,
   `DynamicChainSolver.cpp.obj`, `BoneChainPhysicsResolver.cpp.obj`, plus the
   three new test `.cpp.obj` files), re-linked `libgte_core.a`, and re-linked
   `GreatTamanaEngineTests.exe` — succeeded with **zero warnings/errors**
   both before and after the Step 3.5 bug fix.
3. **Targeted test run** (first attempt, confirming the bug): filtering to
   `BoneWorldMatrixQueryTests.*:DynamicChainSolverTests.*:BoneChainPhysicsResolverTests.*:IkSolverTests.*`
   showed **14/16 passing, 2 FAILED**
   (`BoneChainPhysicsResolverTests.ProducedRotationLandsBoneAtRequestedSimulatedPosition`
   and `...ThreeJointChainAppliesRootToTipInDependencyOrder`) — both in
   `BoneChainPhysicsResolver`, confirming the Step 3.5 pseudocode bug
   described above (every other new test — all of `DynamicChainSolverTests`,
   all of `BoneWorldMatrixQueryTests`, and all of the pre-existing
   `IkSolverTests` — passed on the very first build).
4. **After the fix**: same filtered run — **17/17 tests passed** (0
   failures; the fix added one additional regression test,
   `OutOfRangeRootBoneIndexIsSkippedGracefully`).
5. No full `ctest` regression run was performed, per this task's workflow
   rules (reserved for the campaign's final phase).

## Next Steps (for whoever picks up Phase 3)

Proceed to `PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md`. It wires
`StepDynamicChain()` and `ApplyDynamicChainPhysicsToPose()` (both implemented
exactly per this phase's own signatures — Phase 3 should be able to call them
directly with no signature mismatches) into a brand-new, standalone
`PhysicsSystem`, sandwiched between `AnimationSystem::EvaluatePoses()` and
`AnimationSystem::SkinAndUpload()` via the new `ResolvedAnimationPose` ECS
component. **One thing Phase 3 (and Phase 4's chain-building step) must know
about, directly resulting from this phase's own bug fix**: because
`ApplyDynamicChainPhysicsToPose()` rewrites `pose[parentBoneIndex]` — which,
for the first joint, is `definition.rootBoneIndex` itself — a chain's root
bone's `pose` entry for the CURRENT frame **will** be overwritten by this
function whenever the chain has at least one joint needing correction. This
is intentional and safe (per-frame `pose` is rebuilt fresh from
`SampleAnimationPose()`/IK/append every frame before physics ever runs, so
nothing "leaks" across frames), but Phase 4's `DetectDynamicChains()` should
be aware that a chain's `rootBoneIndex` is not purely a read-only anchor from
this resolver's point of view — it is also a write target — when deciding
whether a candidate root bone is safe to assign to more than one chain, or is
shared with unrelated FK-driven siblings that must not be visually affected
by physics.
