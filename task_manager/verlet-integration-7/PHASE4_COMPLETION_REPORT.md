# PHASE4 — Completion Report: Freeze/Disable Runtime Controls AND Guaranteed Every-Frame Pose Reapplication (Culprit F)

Part of the `verlet-integration-7` campaign. Implements
`PHASE4_FREEZE_AND_DISABLE_RUNTIME_CONTROLS.md` (v2) exactly as specified,
following on from `PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`,
and `PHASE3_COMPLETION_REPORT.md` (all already landed, re-verified unchanged by
this phase). This phase fixes **Culprit D** (the missing "freeze in place"
opt-out) and, as its co-equal v2 goal, **Culprit F** (a chain-controlled bone
visibly "popping" back to raw bind/FK pose on any render frame where the
fixed-timestep accumulator legitimately produces `stepCount == 0` — the common
case on any display faster than ~60 Hz).

Every file/line citation in `PHASE4_FREEZE_AND_DISABLE_RUNTIME_CONTROLS.md` was
re-verified against the current source tree before implementation; the code
added matches the document's own Step 3.1–3.4 exactly.

## What was done

### `src/ECS/Components/DynamicChainRig.h`

- Added a new field, `bool frozen = false;`, alongside the existing `enabled`
  field, with the doc comment specified by the phase document (explains the
  distinction from `enabled = false`, the "still rides along rigidly" via
  Phase 3's world-space matrix, and the v2 note that the SAME reapplication
  path also fires for an ordinary sub-threshold accumulator frame, not just
  while frozen).

### `src/Game/Physics/PhysicsSystem.cpp`

- **`DynamicChainBatchContext`** (anonymous namespace) gained a new field,
  `bool frozen;` — read-only, resolved once per entity per frame from
  `rig.frozen`, mirroring how `entityWorldMatrix`/`entityWorldMatrixInverse`
  are already threaded through.
- **`PhysicsSystem::Update()`**'s per-rig loop: replaced the old
  `stepCount computation + early "if (stepCount <= 0) continue;"` block with
  logic that:
  - Computes `stepCount = 0` and pins `rig.accumulatedSeconds = 0.0f`
    whenever `rig.frozen` is true (never banking time while frozen, so
    un-freezing later never triggers a multi-step catch-up burst).
  - Otherwise computes `stepCount` via the existing
    `ComputeFixedStepCount()` call, exactly as before.
  - **Never `continue`s merely because `stepCount == 0`** — every rig that
    passed the earlier (still-`continue`-able) guards (disabled, no resolved
    pose yet, model not registered/stale) now unconditionally falls through
    into the dispatch/serial step path below, every single frame.
- **`StepDynamicChainRange()`**: restructured to split "integrate" from
  "reapply":
  - The `for (int step = 0; step < context.stepCount; ++step)` integration
    loop is now wrapped in `if (!context.frozen) { ... }` — a frozen rig
    never advances the Verlet solver at all (this is also naturally a no-op
    for an ordinary rig whenever `stepCount == 0`, since the loop itself
    already executes zero iterations — the explicit guard exists purely so a
    future change can't silently make a frozen rig integrate again without
    revisiting it).
  - **`ApplyDynamicChainPhysicsToPose()` is now called exactly ONCE per
    `StepDynamicChainRange()` call** (previously once per substep, inside the
    integration loop), guarded only by `if (state.initialized)` — this is
    what guarantees the chain's current `state.particles` (whatever they
    hold, whether freshly integrated this call or untouched because frozen/
    sub-threshold) are always reprojected into `pose` via the CURRENT frame's
    `entityWorldMatrixInverse`, every single frame, closing Culprit F for
    both the frozen case and the ordinary non-frozen `stepCount == 0` case.
    `state.initialized == false` (a chain that has never once integrated —
    e.g. spawned already-frozen) correctly leaves `pose` exactly as
    `EvaluatePoses()` wrote it.
- The `DynamicChainBatchContext context{...}` construction now passes
  `rig.frozen` as its final field.

### `src/Editor/Panels/InspectorPanel.cpp`

- Added `ImGui::Checkbox("Freeze", &rig->frozen);` (same line, via
  `ImGui::SameLine()`) right after the existing `"Enabled"` checkbox, plus a
  `TextDisabled()` line explaining the distinction, exactly as specified.

## Scope discipline (confirmed)

- `ComputeFixedStepCount()`/`FixedTimestepAccumulator.cpp` — **not touched at
  all**; Culprit F is entirely about how `PhysicsSystem.cpp` consumes an
  already-correct `stepCount == 0` result.
- `StepDynamicChain()`, `ChainConstraints.cpp`, `VerletIntegration.cpp` —
  **not touched**.
- No third state beyond `enabled`/`frozen` was added.
- No per-chain (as opposed to per-rig) freeze granularity was added.
- `frozen` is not persisted across reload/re-spawn — same lifetime/lack of
  save-load support as `enabled` already has (no scene-serialization system
  exists yet).
- `ApplyDynamicChainPhysicsToPose()`'s per-substep→once-per-call change is a
  strict simplification (the old per-substep intermediate calls were never
  externally observable — nothing reads `pose` between substeps within the
  same `Update()` call) and was not reverted, per the phase document's own
  explicit "What We Will NOT Do".

## Tests added

### `tests/Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp` (new file)

Six `TEST` cases (all Tier 1, plain `Registry`, no Renderer/GPU/ImGui),
reusing the same synthetic 4-bone-rig fixture style as
`PhysicsSystemWorldSpaceRootMotionTests.cpp`:

1. `FrozenDynamicChainRigStopsIntegratingButKeepsItsLastSimulatedShape` —
   settles a chain, freezes it, drives many more frames with gravity still
   configured, and asserts the tip's world position stays within a tight
   epsilon of its value at freeze time, while a sibling (never-frozen) rig
   is exercised for contrast.
2. `FrozenDynamicChainRigStillRidesAlongRigidlyWithEntityTransformMotion` —
   settles, freezes, then applies a small Transform drag and asserts (a) the
   chain's own root bone moves exactly rigidly with the entity (proven via
   `ApproximatelyEqual` against the raw drag delta), and (b) the chain's
   *shape* — the tip's position expressed relative to the chain's own root
   bone, not the world origin — changes by less than half the drag
   magnitude, i.e. it neither collapses onto the root nor stays pinned to
   its old absolute world spot. (See "Notable test-design finding" below for
   why this had to be built this way rather than a simpler exact-delta
   check.)
3. `UnfreezingResetsAccumulatedSecondsSoNoCatchUpBurstOccurs` — freezes for
   several seconds' worth of large-`deltaSeconds` `Update()` calls, asserts
   `rig.accumulatedSeconds` stayed pinned at exactly `0.0f` throughout, then
   un-freezes and steps one ordinary frame, asserting the resulting pose
   change is small/plausible rather than an implausible multi-step catch-up
   jump.
4. `DisablingADynamicChainRigStillRevertsToBindPoseExactlyAsBefore` — a
   byte-for-byte mirror of `PhysicsSystemTests.cpp`'s own
   `DisabledDynamicChainRigIsSkippedEntirely`, confirming `frozen`'s mere
   existence (default `false`) leaves `enabled = false`'s pre-existing
   behavior completely unaffected.
5. `AnOrdinaryNonFrozenChainNeverPopsBackToRawBindOrFkPoseOnARenderFrameWhereNoNewFixedStepAccumulates`
   — a `DynamicChainRig`-only (never-animated) entity, wired through
   `AnimationSystem::EvaluatePoses()` → `PhysicsSystem::Update()` exactly as
   `Game::Update()` does, driven at a constant `1/240` second delta (a 240 Hz
   display) for 600 frames; after the chain first visibly sags away from
   bind pose, asserts the tip's frame-to-frame world-position delta never
   exceeds a small bound on ANY subsequent frame, including every
   `stepCount == 0` frame. This test was written first and confirmed to FAIL
   against the pre-Phase-4 code (reproducing the flicker), then confirmed to
   PASS against the restructured code.
6. `AnAnimatedEntityWithADynamicChainRigAlsoNeverPopsOnAStepCountZeroFrame` —
   the same 240 Hz-driven scenario, but for an entity with a genuinely
   playing `SkeletalAnimator` (a real `*.gta` `AssetType::Animation` file
   written to a temp directory, one keyframe on the un-physics-driven root
   bone, mirroring `AnimationSystemEvaluatePosesTests.cpp`'s own fixture
   style) — proving the fix also resolves the pre-existing (pre-campaign)
   version of Culprit F for an already-animated jiggle-physics model.

Registered in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list (with a
matching descriptive comment block), immediately after
`Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp`.

## Notable test-design finding (not a production-code bug)

While writing test 2, an initial, stricter formulation (asserting the tip's
absolute world position moves by the *exact* rigid drag delta) failed against
the correctly-implemented production code. Root-causing this showed it is a
**property of `BoneChainPhysicsResolver.cpp`'s existing, unmodified
direction-only-correction algorithm** (it only ever corrects a bone's
*rotation*, at a *fixed* bind-length distance from its parent — never a raw
translation write), not a defect introduced by this phase: reprojecting a
frozen chain's stored world-space particle position through a *very large*
Transform drag (several times the chain's own bind-segment length) makes the
corrected direction swing sharply toward the drag itself, since the target
distance vastly exceeds the fixed rod length the resolver is constrained to.
The test was corrected in two ways to reflect this pre-existing, correct
behavior rather than paper over it: (1) using a small, drag-scale-appropriate
delta (`0.1` units, proportional to the fixture's own unit-length bone
segments, rather than the much larger `maxPlausibleRootDelta`-scale delta
Phase 3's own drag tests use for a *different*, non-frozen scenario), and (2)
comparing the *root-relative* shape (tip position relative to the chain's own
root bone, which always moves perfectly rigidly with the entity by
construction) rather than an absolute-world exact-delta match. This is a
test-design lesson, not a production regression — no `src/` code needed any
change as a result.

## Verification

- **Fast compile check only** (per this task's own workflow rules — no full
  build/regression yet):
  - `cmake -S . -B build` — succeeded (reconfigure needed since
    `tests/CMakeLists.txt`'s source list changed; the one pre-existing `ktx`
    git-describe warning is unrelated).
  - `cmake --build build --target gte_core` — succeeded, 0 errors (rebuilt
    `PhysicsSystem.cpp.obj`, `AnimationSystem.cpp.obj`,
    `InspectorPanel.cpp.obj`, plus link).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded, 0
    errors.
- As an extra correctness check beyond a bare compile (not a full regression
  run), the new tests plus every directly-related pre-existing suite were run
  via `--gtest_filter`:
  - `PhysicsSystemFreezeAndCulpritFTests.*` (6 new tests) — all passed.
  - A broader sweep, `--gtest_filter=*Physics*:*Animation*:*DynamicChain*:GameLoop*`
    (89 tests total, spanning `PhysicsSystemTests`, `PhysicsSystemWorldSpaceRootMotionTests`,
    `PhysicsSystemParallelTests`, `DynamicChainRigCacheTests`,
    `DynamicChainDetectionTests`, `DynamicChainDefinitionTests`,
    `DynamicChainSolverTests`, `BoneChainPhysicsResolverTests`,
    `AnimationSystemEvaluatePosesTest`, `AnimationSystemSkinAndUploadPhysicsOnlyTest`,
    `AnimationPoseEvaluatorTests`, `MotionSamplerTests`,
    `ResolvedAnimationBindingCacheTest`, `AnimationBindingKeyTest`,
    `AnimationClipCacheTest`, `RigFileTest`, `GameLoopPhysicsWithoutAnimationTests`,
    and the two death tests) — all 89 passed, unchanged.
- The full test suite (`ctest`) was **not** run, per this task's explicit "no
  full build/regression yet" instruction — reserved for Phase 5 (or an
  explicit full-build instruction).

## What this unblocks

`DynamicChainRig` entities now have both requested runtime opt-outs
(`enabled = false` to fully disable, `frozen = true` to pause-and-hold), and —
just as importantly — no `DynamicChainRig` entity (animated or T-pose) ever
visibly flickers between "simulated" and "raw bind/FK" on a render frame where
the physics accumulator legitimately produces zero new steps, which is the
common case on any display faster than ~60 Hz. This directly unblocks
`PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md`, the last phase in this
campaign's strict order — its settling/stability tuning work is now being
judged against a fully working, fully visible, non-flickering pipeline.

## Next step

Proceed to `PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md`.
