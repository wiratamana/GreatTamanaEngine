# PHASE3 — Completion Report: World-Space, Root-Motion-Aware Dynamic Chain Simulation

Part of the `verlet-integration-7` campaign. Implements
`PHASE3_WORLD_SPACE_ROOT_MOTION_AWARE_CHAIN_SIMULATION.md` (v2) exactly as
specified, following on from `PHASE1_COMPLETION_REPORT.md` (baseline
`ResolvedAnimationPose` for non-animated `DynamicChainRig` entities) and
`PHASE2_COMPLETION_REPORT.md` (skin/upload visibility for physics-only
entities), both already landed and re-verified as unchanged by this phase.
This phase fixes **Culprit C**: `PhysicsSystem` used to simulate entirely in
bone-local "model space," with zero knowledge that the owning ECS entity even
has a `Transform` — dragging/rotating a spawned model produced zero inertial
lag in its hair/skirt, because physics never learned the entity moved. This is
the phase that directly satisfies the user's own concrete acceptance test:
*"i want to hair or skirt get physically move by simulation if i drag model
position left and right without to actually run animation on it."*

Every file/line-number citation in `PHASE3_WORLD_SPACE_ROOT_MOTION_AWARE_CHAIN_SIMULATION.md`
was re-verified against the current source tree before implementation (the
document's own v2 "scale-free physics-space matrix" design was followed
exactly, superseding the original v1 "full TRS + generic inverse" approach).

## What was done

### `src/Game/Physics/PhysicsSystem.cpp`

- Added `#include "../../ECS/Components/Transform.h"` and
  `#include "../../ECS/TransformHierarchy.h"`.
- **`DynamicChainBatchContext`** (anonymous namespace) gained two new fields:
  `Mat4 entityWorldMatrix` (the owning entity's real, fully-resolved world
  **position + rotation only** — scale deliberately excluded) and
  `Mat4 entityWorldMatrixInverse` (its inverse, used to convert a simulated
  world-space position back into bone-local space).
- **`StepDynamicChainRange()`** now composes `context.entityWorldMatrix` with
  every bone-local matrix *before* extracting a position — for the chain
  root, every joint, and the optional head collider — so
  `rootWorldPos`/`animatedJointWorldPositions`/`collider.center` are now
  genuine world-space quantities instead of bone-local "model space" ones
  coincidentally treated as world space. After each `StepDynamicChain()`
  call, the resulting `state.particles` positions are converted back into
  bone-local space via `context.entityWorldMatrixInverse` before being handed
  to `ApplyDynamicChainPhysicsToPose()` — which remains **completely
  untouched**, exactly like `StepDynamicChain()`/`ChainConstraints.cpp`/
  `VerletIntegration.cpp` themselves; every change in this phase lives
  entirely in `PhysicsSystem.cpp`'s own call-site math.
- **`PhysicsSystem::Update()`** now resolves, once per entity per frame (on
  the main thread, *before* any per-chain parallel dispatch), the entity's
  scale-free world matrix:
  ```cpp
  const Transform entityWorldTransform = ComputeWorldTransform(registry, entity);
  const Mat4 entityWorldMatrix = Mat4::TRS(entityWorldTransform.position, entityWorldTransform.rotation, Vec3::One());
  ```
  followed by a `TryInverse()` call (debug-asserting, `Mat4::Identity()`
  last-resort fallback — expected to never actually trigger, since a pure
  rotation+translation matrix is algebraically never singular), both passed
  into the `DynamicChainBatchContext{...}` construction alongside the
  pre-existing fields. `ComputeWorldTransform()` returns a default (identity)
  `Transform` for an entity with no `Transform` component at all, which is
  exactly what guarantees every pre-existing Transform-less test fixture
  (`PhysicsSystemTests.cpp`, `PhysicsSystemParallelTests.cpp`) keeps producing
  byte-identical results.

### `src/Physics/VerletParticle.h` / `src/Physics/DynamicChainRuntimeState.h`

- Updated `VerletParticle`'s own doc comment and
  `DynamicChainRuntimeState::lastRootWorldPosition`'s doc comment to state
  explicitly that `position`/`previousPosition`/`lastRootWorldPosition` are
  now genuine **world-space (rotation + translation only, scale excluded)**
  quantities as of this phase, per the phase plan's own Step 3.2 — no
  behavioral change, documentation only.

## Scope discipline (confirmed)

- `StepDynamicChain()`, `ApplyDynamicChainPhysicsToPose()`,
  `ChainConstraints.cpp`, `VerletIntegration.cpp` — **not touched at all**.
- `maxPlausibleRootDelta`'s teleport guard — **not weakened or removed**; its
  behavior is now genuinely scale-invariant (a real improvement over the
  original v1 plan, which would have made the guard trip differently for a
  scaled-up vs. scaled-down instance of the same drag gesture).
- The parallel-dispatch disjoint-chain invariant — **unchanged**;
  `entityWorldMatrix`/`entityWorldMatrixInverse` are resolved once, read-only,
  per entity, on the main thread, before any chain-level `Dispatch()` begins,
  and shared by const reference by every chain in that entity's own batch,
  exactly like `skeleton`/`pose` already were.
- Gravity/wind remain genuine world-space (never rotated relative to the
  character's own local down/forward) — untouched, per the phase's own "What
  We Will NOT Do".
- Scale is **excluded entirely** from the physics-space matrix, per v2's own
  design — never a partial/uniform-only compromise.

## Tests added

### `tests/Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp` (new file)

A new, dedicated Tier-1 test file mirroring `PhysicsSystemTests.cpp`'s own
synthetic 4-bone-rig fixture (a `Static` anchor + two `Dynamic`-body,
perpendicular-to-gravity chain) via a shared `BuildSyntheticChainFixture()`
helper. Five `TEST` cases, all Tier 1 (plain `Registry`, no
Renderer/GPU/ImGui device):

1. `EntityTransformTranslationProducesInertialLagInSimulatedChain` — settles
   the chain under gravity for 60 frames, then applies a single
   `Vec3(2.0f, 0.0f, 0.0f)` `Transform.position` delta (well under the
   default `maxPlausibleRootDelta`) and steps once more. Asserts the tip
   joint's reconstructed world position moved by **strictly less than** the
   full `2.0f` rigid delta, but by **more than zero** — the direct, automated
   version of the user's own drag-test.
2. `ASequenceOfSmallContinuousTransformDeltasNeverTripsTheTeleportGuardWhileALargeSingleFrameJumpStillDoes`
   — (a) ten small `0.05`-unit-per-frame deltas never produce a discontinuous
   per-step tip jump (no re-seed fired); (b) in a fresh scenario, one
   `Vec3(500, 0, 0)` single-frame jump (far beyond `maxPlausibleRootDelta`)
   still re-seeds cleanly — no NaN/Inf, and the tip lands close to the bind
   target at the new position.
3. `IdentityOrMissingTransformProducesByteIdenticalResultsToPreWorldSpaceBehavior`
   — runs the same gravity scenario twice, once with no `Transform` component
   at all and once with an explicit, untouched default `Transform`; asserts
   both produce identical `ResolvedAnimationPose::pose` results — the hard
   proof this phase is fully backward-compatible with every pre-existing test.
4. `ATransformParentedUnderAMovingAncestorStillProducesCorrectlyComposedWorldSpaceSimulation`
   — parents the physics entity under a second "vehicle" entity
   (`TransformHierarchy.h::SetParent()`), moves the vehicle's own `Transform`
   across 5 frames, and asserts the chain's tip reacts (nonzero motion, but
   lagging behind the vehicle's own total rigid displacement) — proving
   `ComputeWorldTransform()`'s full recursive parent-chain walk feeds this
   phase's math, not just a one-level lookup.
5. `EntityTransformScaleNeverAffectsTheSimulatedPoseOrTheTeleportGuardThreshold`
   — runs test 1's own scenario (settle + drag) on three separate
   entities/registries with `Transform::scale` = `(1,1,1)`, `(2,2,2)`, and
   `(0.25,0.25,0.25)` respectively; asserts all three produce **identical**
   `ResolvedAnimationPose::pose` results — the direct, automated proof of the
   v2 design's scale-invariance guarantee.

Registered in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list (with a
matching descriptive comment block, following this file's own existing
per-entry documentation convention), immediately after
`Game/Physics/PhysicsSystemParallelTests.cpp`.

## Verification

- **Fast compile check only** (per this task's own workflow rules — no full
  build/regression yet):
  - `cmake --build build --target gte_core` — succeeded, 0 errors (rebuilt
    `PhysicsSystem.cpp.obj` plus everything depending on it).
  - `cmake -S . -B build` (reconfigure, needed since `tests/CMakeLists.txt`'s
    source list changed) — succeeded (the one `ktx` git-describe warning
    printed is pre-existing/unrelated, not a new failure).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded, 0
    errors.
- As an extra correctness check beyond a bare compile (not a full regression
  run), the newly-added tests plus every directly-related pre-existing suite
  were executed via `--gtest_filter`:
  - `PhysicsSystemWorldSpaceRootMotionTests.*` (5 new tests) — all passed.
  - `PhysicsSystemTests.*` (6 tests) — all passed, unchanged (test 3's own
    backward-compatibility guarantee confirmed directly).
  - `PhysicsSystemParallelTests.*` (2 tests) — all passed, unchanged.
  - `GameLoopPhysicsWithoutAnimationTests.*` (1 test, Phase 1's own
    end-to-end T-pose-sags-under-gravity proof) — passed, unchanged.
  - `AnimationSystemEvaluatePosesTest.*` (8 tests, Phase 1's own suite) — all
    passed, unchanged.
  - `AnimationSystemSkinAndUploadPhysicsOnlyTest.*` (4 tests, Phase 2's own
    suite) — all passed, unchanged.
  - `DynamicChainRigCacheTests.*` (5 tests) — all passed, unchanged.
  - `DynamicChainSolverTests.*` (8 tests — `StepDynamicChain()` itself,
    completely untouched by this phase) — all passed, unchanged.
  - `BoneWorldMatrixQueryTests.*` (4 tests — untouched) — all passed.
  - `TransformHierarchyTest.*` (19 tests — untouched; `ComputeWorldTransform()`
    is a pre-existing function this phase newly consumes, not one it
    modifies) — all passed.
  - Total: 62/62 passed.
- The full test suite (`ctest`) was **not** run, per this task's explicit
  "no full build/regression yet" instruction — reserved for a later phase in
  this campaign (or an explicit full-build instruction).

## What this unblocks

`PhysicsSystem` now genuinely simulates in true (scale-free) world space:
translating or rotating a spawned model's own `Transform` (directly, or
through a parent chain) produces real Verlet inertial lag in every simulated
dynamic chain, whether or not the model is animated — the campaign's own
headline acceptance test. This directly unblocks
`PHASE4_FREEZE_AND_DISABLE_RUNTIME_CONTROLS.md`, the next task in this
campaign's strict phase order — its "ride along rigidly while frozen"
behavior, and its Culprit F flicker fix, are both only meaningful now that the
entity's own Transform is genuinely consulted every frame.

## Next step

Proceed to `PHASE4_FREEZE_AND_DISABLE_RUNTIME_CONTROLS.md`.
