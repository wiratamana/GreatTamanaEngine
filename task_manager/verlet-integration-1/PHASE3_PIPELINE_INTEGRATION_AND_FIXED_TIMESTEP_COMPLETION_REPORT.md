# PHASE3 — Pipeline Integration and Fixed Timestep — Completion Report

Campaign: `task_manager/verlet-integration-1/` (Verlet-Integrated Dynamic Bone
Physics System). Parent: `PHASE0_MASTER_STRATEGY.md`. This report covers
**Phase 3 only** — `PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md` (v4) —
which depends on Phase 1 (`src/Physics/VerletParticle.h`, `VerletIntegration.h`,
`ChainConstraints.h`, `WindField.h`) and Phase 2 (`DynamicChainDefinition.h`,
`DynamicChainRuntimeState.h`, `DynamicChainSolver.h`,
`BoneChainPhysicsResolver.h`, `Animation/BoneWorldMatrixQuery.h`).

## Branch / Prerequisites

- Branch: `feature/physics-from-scratch` (unchanged, as required).
- Read `readme.md` and `agents.md` in full before starting.
- Re-read all six strategy files under `task_manager/verlet-integration-1/`
  (`PHASE0_MASTER_STRATEGY.md` through `PHASE5_...md`), with particular focus
  on `PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md`'s own v3/v4 Revision
  Notices (the ECS-independence rewrite and the generic-naming pass).
- Read the previous phase's own completion report
  (`task_manager/separate-anim-physics-system-9/PHASE2_BONE_CHAIN_PHYSICS_BRIDGE_COMPLETION_REPORT.md`)
  for continuation clues — it confirmed Phase 2's `StepDynamicChain()`/
  `ApplyDynamicChainPhysicsToPose()` both compile/test exactly per their own
  documented signatures (so Phase 3 could call them directly with no
  signature mismatches), and specifically flagged that
  `ApplyDynamicChainPhysicsToPose()` rewrites `pose[parentBoneIndex]` —
  meaning a chain's `rootBoneIndex` is a WRITE target, not merely a read-only
  anchor — a fact Phase 4's own `DetectDynamicChains()` will need to account
  for later, not something Phase 3 itself needed to change.
- Verified against the CURRENT source tree (not just the phase document in
  isolation) that the real `AnimationSystem`/`Game` had evolved since the
  phase document was written — most notably the GPU Vertex Skinning campaign
  (CPU/GPU `SkinningMode` branch, `GpuSkinningRigCache`, per-model bone-matrix
  upload) — so the split described below adapts the document's pseudocode to
  the REAL, current `AnimationSystem::Update()` body rather than a stale
  sketch of it.

## What Was Done

Implemented Phase 3 per `PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md`'s
(v4) Step 3/Step 5 checklist, end to end.

### 3.1/3.2 — New ECS components (plain data, no logic)

- **`src/ECS/Components/ResolvedAnimationPose.h`** (new) — `pose` (a
  `std::vector<BoneLocalOffset>`), the entire hand-off contract between the
  three independent stages `Game::Update()` now calls in a fixed order:
  `AnimationSystem::EvaluatePoses()` writes it wholesale, `PhysicsSystem::
  Update()` optionally overwrites individual bone entries in place, and
  `AnimationSystem::SkinAndUpload()` only ever reads it.
- **`src/ECS/Components/DynamicChainRig.h`** (new) — `meshGtaPath`
  (self-contained lookup key, never reaching into `SkeletalAnimator`),
  `chainStates` (one `DynamicChainRuntimeState` per detected chain),
  `accumulatedSeconds` (the fixed-timestep accumulator's own persistent
  state), and `enabled` (for Phase 4's future Inspector toggle).

### 3.3 — `src/Physics/FixedTimestepAccumulator.h/.cpp` (new, pure math)

- `ComputeFixedStepCount()` — exactly the accumulator-pattern math specified:
  increments `accumulatedSeconds` by the frame delta, drains whole
  `fixedTimestep`-sized steps up to `maxStepsPerFrame`, and clamps leftover
  time to at most one fixed timestep once the cap is hit (the
  spiral-of-death guard). Returns `0` immediately for a non-positive
  `fixedTimestep` rather than looping forever/dividing by zero.

### New: `src/Physics/GlobalPhysicsSettings.h`

Not explicitly spelled out as its own numbered step in the phase document
(which only shows it being *used* by `PhysicsSystem`, via
`#include "../../Physics/GlobalPhysicsSettings.h"`), but required to exist
for `PhysicsSystem.h` to compile: `gravity` (defaults to `Vec3::Down() *
9.8f`), `wind` (a `WindSettings`, Phase 1), `fixedTimestepSeconds` (`1/60`),
and `maxStepsPerFrame` (`4`). Owned entirely by `PhysicsSystem`, never by
`AnimationSystem`, per the v3 Revision Notice.

### 3.4 — Extended `Animation/AnimationPoseEvaluator.h/.cpp`

- Added `EvaluateAnimatedPoseBeforePhysics()` — runs the exact same first
  three stages (`SampleAnimationPose()` → `SolveIkChains()` →
  `ApplyAppendInheritance()`) and returns the intermediate `pose`, stopping
  one step short of `ComputeSkinningMatrices()`.
- `EvaluateAnimatedSkinningPose()` is now implemented purely in terms of it
  (`ComputeSkinningMatrices(skeleton, EvaluateAnimatedPoseBeforePhysics(...))`)
  — the existing function's signature/behavior is completely unchanged; every
  pre-existing caller/test keeps compiling and passing unmodified.
- Added the required divergence-guard regression test,
  `AnimationPoseEvaluatorTests.SkinningPoseNeverDivergesFromPreLikelyPhysicsPoseComposition`,
  asserting `EvaluateAnimatedSkinningPose(...) ==
  ComputeSkinningMatrices(skeleton, EvaluateAnimatedPoseBeforePhysics(...))`
  for the existing `BuildLegWithAppendBone()` fixture (IK + append bones
  both present) — proves the two can never silently diverge later.

### 3.5 — Split `AnimationSystem::Update()` into `EvaluatePoses()`/`SkinAndUpload()`

`AnimationSystem.h`'s single `void Update(Registry&, double)` was REPLACED
(not overloaded) by:

- **`EvaluatePoses(Registry&, double deltaSeconds)`** — the frame-advance/
  loop-clamp logic plus `EvaluateAnimatedPoseBeforePhysics()`, writing the
  result into a get-or-add `ResolvedAnimationPose` component. Touches **no**
  `Renderer`/`Mesh`/GPU state at all, and has zero `#include` of anything
  under `src/Physics/`/`src/Game/Physics/`.
- **`SkinAndUpload(Registry&)`** — everything else the old `Update()` did:
  reads `ResolvedAnimationPose::pose` (skipping an entity that doesn't have
  one yet), computes `skinningMatrices` via `ComputeSkinningMatrices()`
  directly (previously computed by `EvaluateAnimatedSkinningPose()` inline in
  the same loop iteration), applies the CPU/GPU `SkinningMode` branch exactly
  as before (mesh-handle swap, bone-matrix upload for `GpuCompute`, or the
  full CPU skin/pack/upload path for `CpuJobSystem`) — byte-for-byte
  unchanged behavior, just reading its skinning-matrix input from the new
  component instead of a same-iteration local variable.
- The **`*** THIS OUTER LOOP MUST REMAIN STRICTLY SEQUENTIAL ***`** rule
  (shared GPU mesh buffers across two instances of the same model) now
  applies only to `SkinAndUpload()`'s own loop — `EvaluatePoses()` is
  explicitly documented as NOT bound by it, since it never touches
  Renderer/Mesh state.
- `AnimationSystem.cpp` gained `#include "../../Animation/SkeletonPose.h"`
  (for `ComputeSkinningMatrices()`, now called directly) and
  `#include "../../ECS/Components/ResolvedAnimationPose.h"` — no
  `src/Physics/`/`src/Game/Physics/` include anywhere in this file, confirmed
  directly (see "Verification" below).
- The one, sole pre-existing call site (`Game.cpp`) was updated to call both
  new methods with `PhysicsSystem::Update()` sandwiched between them (3.7).

### 3.6 — New, fully independent `src/Game/Physics/PhysicsSystem.h/.cpp`

- **`PhysicsSystem`** — a fourth `Game`-layer orchestrator, deliberately
  **not** part of `AGENTS.md`'s "systems allowed to depend on both ECS and
  Renderer" list: it depends only on the ECS `Registry` plus pure
  `Animation/BoneWorldMatrixQuery.h`/`Physics/*` functions — zero
  `#include` of `AnimationSystem.h`, `MotionSampler.h`, `IkSolver.h`,
  `AppendBoneSolver.h`, `AnimationPoseEvaluator.h`, `VertexSkinning.h`,
  `ECS/Components/SkeletalAnimator.h`, `Renderer/*`, `Mesh.h`,
  `RenderSystem.h`, or `MeshInstantiationSystem.h` (confirmed directly — see
  "Verification").
- `RegisterDynamicChains(absoluteGtaPath, SkinnedMeshData)` and
  `AttachDynamicChainRigIfNeeded(Registry&, Entity, absoluteGtaPath)` are
  real, callable methods, but — exactly as the phase document instructs —
  deliberately PROVABLE NO-OPS this phase: real chain detection
  (`Physics/DynamicChainDetection.h`) is explicitly Phase 4's job.
- `Update(Registry&, double deltaSeconds)` is implemented exactly per the
  phase document's own pseudocode: iterates every enabled `DynamicChainRig`,
  skips an entity with no `ResolvedAnimationPose` yet, looks up its model in
  `DynamicChainRigCache` (always `nullptr` this phase — see below), computes
  the fixed step count via `ComputeFixedStepCount()`, and — for however many
  chains a (currently nonexistent) model entry has — captures the pure FK
  root/joint world positions ONCE before the substep loop (PHASE0's own
  Revision Notes finding #4, re-verified still correctly applied here),
  then calls `StepDynamicChain()`/`ApplyDynamicChainPhysicsToPose()` per
  substep.
- **`src/Game/Physics/DynamicChainRigCache.h`** (new, stub) — `Register()`
  is a real, callable method (so `PhysicsSystem::RegisterDynamicChains()` has
  a genuine call site to forward into today) but is never invoked with a
  non-empty chain list this phase; `TryGet()` therefore always returns
  `nullptr` for every model, making `PhysicsSystem::Update()` a **provable
  no-op** this phase — proven directly by this phase's own tests (3.9).

### 3.7 — Wired the three stages into `Game::Update()`/`Game::CreateMeshEntityFromGtaFile()`

- `Game.h` gained a `PhysicsSystem m_physicsSystem;` member (no constructor
  dependencies, unlike `AnimationSystem`).
- `Game::Update()`:
  ```cpp
  m_animationSystem.EvaluatePoses(m_registry, deltaSeconds);
  m_physicsSystem.Update(m_registry, deltaSeconds);
  m_animationSystem.SkinAndUpload(m_registry);
  ```
- `Game::CreateMeshEntityFromGtaFile()` gained two new calls, alongside
  (never instead of) the existing `RegisterSkinnedMesh()`/
  `RegisterGpuSkinnedMesh()` calls:
  `m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin)` and
  `m_physicsSystem.AttachDynamicChainRigIfNeeded(m_registry, root,
  absoluteGtaPath)` — both provable no-ops this phase, real wiring lands in
  Phase 4.

### 3.8 — `AGENTS.md` clarification

Added the one sentence the phase document specifies directly after the
existing "Only `RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem` are
allowed to depend on both ECS and Renderer" rule (Entity-Component-System
section), naming `PhysicsSystem` as a fourth `Game`-layer orchestrator
deliberately excluded from that list, with the rationale (independently
testable/schedulable from `AnimationSystem`'s GPU-touching half).

### 3.9 — Tests for this phase (all new, all Tier 1)

- `tests/Animation/AnimationPoseEvaluatorTests.cpp` — added the divergence
  regression test (see 3.4 above). 4/4 tests in this file pass (1 new).
- `tests/Physics/FixedTimestepAccumulatorTests.cpp` (new, 6 tests) — normal
  ~60fps delta yields one step/zero leftover; exactly-two-timesteps delta
  yields two steps/zero leftover; a huge (5s) delta clamps to
  `maxStepsPerFrame` with bounded leftover; a delta smaller than one
  timestep yields zero steps and accumulates; accumulation across multiple
  calls eventually fires a step; a non-positive fixed timestep returns zero
  steps safely.
- `tests/Game/Animation/AnimationSystemEvaluatePosesTests.cpp` (new, 3
  tests) — a hand-built `Registry` + minimal `SkinnedMeshData` + a real temp
  `*.gta` `AssetType::Animation` file (same convention as
  `Game/AnimationClipCacheTests.cpp`) confirms `EvaluatePoses()` writes a
  `ResolvedAnimationPose` matching an independently-computed
  sample→IK→append pose EXACTLY, and that skinning matrices computed FROM
  that component are byte-identical to the OLD single-call
  `EvaluateAnimatedSkinningPose()`'s own output — proving the split is a
  genuine behavioral no-op; plus two degrade-gracefully cases
  (`SkinAndUpload()` never crashes with no GPU mesh parts registered, and
  correctly skips an entity `EvaluatePoses()` never produced a pose for).
- `tests/Game/Physics/PhysicsSystemTests.cpp` (new, 5 tests) — confirms
  `PhysicsSystem::Update()` is a genuine, safe no-op today: an entity with a
  `DynamicChainRig` AND a `ResolvedAnimationPose` has its pose left
  byte-for-byte unchanged; an entity with a `DynamicChainRig` but no
  `ResolvedAnimationPose` yet is skipped gracefully; an entity with neither
  component, and an entirely empty `Registry`, are both safe no-ops; and a
  DISABLED `DynamicChainRig` is skipped entirely.
- `tests/CMakeLists.txt` — all four new/touched test files registered in the
  unconditional (Tier 1) `GTE_TEST_SOURCES` list, plus a matching descriptive
  paragraph for each in the file's own "Test taxonomy" header comment block
  (PHASE0 v2 Revision Notes finding #6).
- `CMakeLists.txt` — every new source file added to `gte_core`'s source list
  (`ResolvedAnimationPose.h`/`DynamicChainRig.h` alongside the existing ECS
  Components block; `GlobalPhysicsSettings.h`/`FixedTimestepAccumulator.h/.cpp`
  alongside the existing `Physics/*` block; `DynamicChainRigCache.h`/
  `PhysicsSystem.h/.cpp` as a new `Game/Physics/` block right after
  `Game/Animation/AnimationSystem.*`).

## What Was Deliberately NOT Done (per Phase 3's own "Step 4: What We Will
NOT Do", and this task's overall workflow rules)

- The fixed timestep is not per-model — one engine-wide
  `GlobalPhysicsSettings` instance (owned by `PhysicsSystem`) is used, per
  spec.
- `Application::Run()`'s own frame-delta computation is completely untouched
  — the fixed-timestep accumulator is scoped entirely to
  `PhysicsSystem::Update()`.
- **No real chain detection (`Physics/DynamicChainDetection.h`) and no
  populated `DynamicChainRigCache`** — both stubbed exactly as instructed,
  proven by this phase's own tests to be genuine no-ops; Phase 4's explicit
  job.
- `AnimationSystem::SkinAndUpload()`'s outer per-animator loop keeps its
  existing "must stay strictly sequential" rule unchanged — only relocated/
  renamed to the half of the old loop that actually needs it.
- **`AnimationSystem` has zero `#include` of anything under `src/Physics/`,
  and `PhysicsSystem` has zero `#include` of `AnimationPoseEvaluator.h`/
  `MotionSampler.h`/`IkSolver.h`/`AppendBoneSolver.h`/`VertexSkinning.h`/
  `ECS/Components/SkeletalAnimator.h`/anything under `Renderer/`/`Mesh.h`/
  `RenderSystem.h`/`MeshInstantiationSystem.h`** — confirmed directly (see
  "Verification").
- `PhysicsSystem` never calls any `AnimationSystem` method, or vice versa —
  `Game::Update()` is the only place that knows both exist.
- No full build or full regression test was run (per this task's explicit
  workflow rules) — only a fast, targeted incremental compile of `gte_core` +
  `GreatTamanaEngineTests` (which necessarily also recompiled `Game.cpp`/
  `Application.cpp`/`RenderPasses.cpp`, proving the whole engine — not just
  the test binary — still links), plus a filtered run of the new/touched
  test suites only.

## Verification

1. **Reconfigure**: `cmake -S . -B build` — succeeded, picked up every new
   source/test file with no errors (only the same pre-existing, unrelated
   KTX-Software git-describe warning every prior phase report also noted,
   not caused by this change).
2. **Fast compile check** (not a full build): `cmake --build build --target
   GreatTamanaEngineTests` — rebuilt exactly the new/changed object files
   (`FixedTimestepAccumulator.cpp.obj`, `PhysicsSystem.cpp.obj`, the
   rewritten `AnimationPoseEvaluator.cpp.obj`/`AnimationSystem.cpp.obj`/
   `Game.cpp.obj`, plus a handful of Editor/Application files that transitively
   depend on `Game.h`, and the four new test `.cpp.obj` files), re-linked
   `libgte_core.a`, and re-linked `GreatTamanaEngineTests.exe` — **18/18
   build steps succeeded, zero warnings/errors**.
3. **`#include` boundary check**: `search_in_dir` confirmed
   `src/Game/Physics/PhysicsSystem.h/.cpp` never includes
   `AnimationSystem.h`/`MotionSampler.h`/`IkSolver.h`/`AppendBoneSolver.h`/
   `AnimationPoseEvaluator.h`/`VertexSkinning.h`/
   `ECS/Components/SkeletalAnimator.h`/`Renderer/*`/`Mesh.h`/
   `RenderSystem.h`/`MeshInstantiationSystem.h` anywhere, and that
   `src/Game/Animation/AnimationSystem.h/.cpp` never includes anything under
   `src/Physics/`/`src/Game/Physics/`.
4. **Targeted test run**: ran
   `GreatTamanaEngineTests.exe --gtest_filter=FixedTimestepAccumulatorTests.*:AnimationPoseEvaluatorTests.*:AnimationSystemEvaluatePosesTest.*:PhysicsSystemTests.*`
   directly — **18/18 tests passed** (0 failures: 6 new
   `FixedTimestepAccumulatorTests`, 4 `AnimationPoseEvaluatorTests` including
   the 1 new divergence-guard regression, 5 new `PhysicsSystemTests`, 3 new
   `AnimationSystemEvaluatePosesTest`). No full `ctest` regression run was
   performed, per this task's workflow rules (reserved for the campaign's
   final phase).

## Next Steps (for whoever picks up Phase 4)

Proceed to `PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md`. It depends
directly on this phase's `ResolvedAnimationPose`/`DynamicChainRig` ECS
components, `PhysicsSystem`/`DynamicChainRigCache`, and
`GlobalPhysicsSettings` (all implemented exactly per this phase's own
signatures — Phase 4 should be able to extend them with no signature
mismatches to the rest of the pipeline). **Two things Phase 4 must know
about, directly resulting from this phase's own implementation choices:**

1. `DynamicChainRigCache::ModelEntry` (this phase's stub) already has the
   exact shape Phase 4 needs (`std::vector<DynamicChainDefinition> chains`
   plus a `SkeletonData skeleton` copy) — Phase 4's real
   `DetectDynamicChains()` should populate it via the existing `Register()`
   method; no new cache shape is needed.
2. `PhysicsSystem::RegisterDynamicChains()`/`AttachDynamicChainRigIfNeeded()`
   are currently unconditional no-ops (their bodies are literally
   `(void)parameter;` casts) — Phase 4 replaces those bodies with real
   detection/attachment logic; the call sites in `Game.cpp` are already
   wired and do not need to change.
