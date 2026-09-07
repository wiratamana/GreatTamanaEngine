# PHASE4 — Freeze/Disable Runtime Controls AND Guaranteed Every-Frame Pose Reapplication (Culprit F) — v2

Part of the `verlet-integration-7` campaign — read `PHASE0_MASTER_STRATEGY.md`
first. Depends on `PHASE3_WORLD_SPACE_ROOT_MOTION_AWARE_CHAIN_SIMULATION.md`
already being landed (a frozen chain's "keep riding along rigidly with the
model" behavior, AND this phase's own Culprit F fix, are only meaningful once
the entity's own Transform is actually consulted every frame). This phase
fixes **Culprit D** (as originally planned) **and, as of this v2 revision,
Culprit F** — a second, co-equal, independently-mandatory goal discovered
during this campaign's own second-iteration audit.

**v2 notice — read this before implementing:** the original (v1) version of
this document restructured `StepDynamicChainRange()` for the sake of the
`frozen` feature alone, and its own prose described the restructuring as
something that happens "for a frozen rig." Re-reading that same
restructuring during this audit revealed it is **also, independently,
required for every ORDINARY (non-frozen) rig**, on any render frame where
the fixed-timestep accumulator produces zero new integration steps — which
is the common case, not an edge case, on any display faster than ~60 Hz. See
`PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)", Finding #2, for the full
mechanism. This v2 document makes that generalization explicit, unambiguous,
and just as mandatory as the `frozen` feature itself — an implementation that
only fixes the `frozen` case has NOT actually completed this phase.

## Step 1: The Goal (Where are we going?)

**Goal A (original, Culprit D) — per the user's own explicit answer:** *"auto
animate. but i can opt-in to disable it or freeze it."* Two, separately
meaningful, opt-outs must exist:

- **Disable** (`DynamicChainRig::enabled = false`, already existing,
  untouched by this phase) — physics stops entirely; the affected bones
  revert to whatever the bind/animated FK pose already says for them (Phase
  1's baseline pass, or a genuinely-animated pose, take over completely).
- **Freeze** (`DynamicChainRig::frozen = true`, NEW) — the simulation stops
  ADVANCING (no more gravity/wind integration, no more structural relaxation)
  but the chain's LAST simulated shape is preserved and keeps being
  correctly re-applied to the pose every frame — including correctly riding
  along rigidly with any further motion of the model's own Transform (thanks
  to Phase 3), rather than popping back to the bind/FK pose the way
  `enabled = false` does.

Both must be exposed as simple checkboxes in the existing Inspector "Dynamic
Chain Physics" section.

**Goal B (v2, Culprit F — equally mandatory):** `PhysicsSystem::Update()`
must NEVER let a chain-controlled bone's pose visibly revert to raw
bind/FK data on ANY rendered frame, for ANY reason, once that chain has
simulated at least once — specifically including an ordinary, non-frozen
rig's own render frame where `ComputeFixedStepCount()` legitimately returns
`0` because not enough real time has yet accumulated to cross one fixed
physics timestep. The chain's currently-held `state.particles` must be
re-applied into the pose EVERY frame it has ever been initialized, whether
or not any NEW integration substep ran that same frame.

## Step 2: The Situation / The Problem (Where are we now?)

`DynamicChainRig::enabled` (`src/ECS/Components/DynamicChainRig.h`, line 44)
is the only existing runtime control, and `PhysicsSystem::Update()`
(`PhysicsSystem.cpp`, lines 199-220, after Phase 3 lands) treats BOTH
"disabled" and "not enough accumulated time yet" as the exact same kind of
hard, unconditional `continue`:

```cpp
if (!rig.enabled) {
    continue;
}
...
const int stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
    m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
if (stepCount <= 0) {
    continue; // <-- THE BUG (Culprit F): this ALSO skips re-applying state.particles into `pose` this frame.
}
```

There is no way today to pause the actual stepping while keeping the chain's
current jiggled shape visually "held" in place (Culprit D) — turning
`enabled` off immediately and permanently reverts every affected bone to
whatever Phase 1's baseline pass (or a genuinely-playing animator) writes for
it, since nothing further ever calls `ApplyDynamicChainPhysicsToPose()` for
that rig again until it is re-enabled.

**(v2) But the SECOND `continue` above — `if (stepCount <= 0) { continue; }`
— is not merely "missing a feature," it is an active, ship-blocking
correctness bug for this campaign's own headline scenario (Culprit F, see
`PHASE0_MASTER_STRATEGY.md`'s Step 2 for the full writeup):**
`AnimationSystem::EvaluatePoses()` unconditionally overwrites the ENTIRE
`ResolvedAnimationPose::pose` array every single frame it runs — for an
animated entity, with a fresh, physics-blind FK sample; for a
`DynamicChainRig`-only entity (Phase 1), with an all-default bind pose. This
happens BEFORE `PhysicsSystem::Update()` runs, every frame, unconditionally,
per `Game::Update()`'s own fixed call order. When `PhysicsSystem::Update()`
then hits `stepCount <= 0` and `continue`s, it leaves that just-written,
physics-blind pose completely uncorrected for the ENTIRE remainder of this
one render frame — every chain-controlled bone renders, for exactly one
frame, at its raw bind/FK value, before the next frame that DOES cross the
fixed-step threshold corrects it back. `ComputeFixedStepCount()`
(`Physics/FixedTimestepAccumulator.cpp`) legitimately returns 0 on the
majority of frames whenever the render frame rate exceeds the fixed physics
rate (`fixedTimestepSeconds`, default `1/60`) — i.e. on any display faster
than 60 Hz, which is mainstream today. The result: a persistent, rapid
flicker between "correctly simulated" and "flat bind/FK pose," on every
single `DynamicChainRig` entity, at any frame rate above ~60 Hz — silently
defeating the entire point of Phases 1-3 and making Phase 5's careful
settling-tuning invisible/pointless underneath the flicker.

The Inspector's existing "Dynamic Chain Physics" section
(`src/Editor/Panels/InspectorPanel.cpp`, lines 583-585) already exposes
exactly one checkbox:

```cpp
if (DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity)) {
    if (ImGui::CollapsingHeader("Dynamic Chain Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &rig->enabled);
        ...
```

## Step 3: The Plan (How will we get there?)

### 3.1 — `src/ECS/Components/DynamicChainRig.h`: add the new field

```cpp
struct DynamicChainRig {
    std::string meshGtaPath;
    std::vector<DynamicChainRuntimeState> chainStates;
    float accumulatedSeconds = 0.0f;
    bool enabled = true;

    // task_manager/verlet-integration-7, Phase 4 - pauses simulation
    // stepping (no further gravity/wind integration or structural
    // relaxation) while PRESERVING the chain's own last-simulated shape -
    // distinct from `enabled = false`, which instead reverts every affected
    // bone to whatever the bind/animated FK pose already says. While frozen,
    // PhysicsSystem::Update() still re-applies each chain's last-known
    // simulated WORLD-space (scale-free - see Phase 3, v2) particle
    // positions through the CURRENT frame's entity world matrix - so a
    // frozen chain's shape keeps correctly riding along, rigidly, with any
    // further motion/rotation of the model itself, instead of staying
    // pinned to the exact spot in the world it was frozen at. Ignored
    // entirely when `enabled == false` (disable always wins).
    //
    // NOTE (v2): this same "keep re-applying the last-known simulated shape
    // every frame regardless of new integration" behavior is ALSO now
    // unconditionally applied when this flag is `false` but the
    // fixed-timestep accumulator simply hasn't crossed a whole step yet
    // this frame (Culprit F, PHASE0_MASTER_STRATEGY.md's Revision Notes) -
    // `frozen` and "ordinary sub-threshold frame" are two different REASONS
    // stepping is skipped this call, but both must reach the exact same
    // "still reapply, just don't integrate" code path in
    // StepDynamicChainRange() below. `frozen` is never required merely to
    // avoid Culprit F - it exists purely for the user-facing "pause and
    // hold" feature.
    bool frozen = false;
};
```

### 3.2 — `src/Game/Physics/PhysicsSystem.cpp`: restructure `Update()`'s per-rig loop so it NEVER bails out early just because `stepCount == 0`

Add a `bool frozen;` field to `DynamicChainBatchContext`. In
`PhysicsSystem::Update()`'s existing per-rig loop, replace the
stepCount-computation-and-early-continue block with:

```cpp
// task_manager/verlet-integration-7, Phase 4 (v2) - `stepCount` legitimately
// stays 0 in TWO distinct situations: this rig is explicitly `frozen`
// (Culprit D), or it is simply not yet time for a new fixed step this frame
// (the ordinary, expected accumulator-pattern outcome on most frames at any
// render rate above ~60 Hz). NEITHER case may `continue` out of this loop -
// doing so would leave whatever physics-blind pose EvaluatePoses() just
// wrote this frame (Phase 1's bind pose, or a fresh FK sample) completely
// unconverted, visibly "popping" every chain-controlled bone back to its
// un-simulated value for this one render frame (Culprit F - see
// PHASE0_MASTER_STRATEGY.md's Revision Notes, Finding #2). Every rig that
// reaches this point (enabled, has a resolved pose, has a registered model)
// ALWAYS falls through into StepDynamicChainRange()/the dispatch path below,
// every single frame - that function itself decides internally whether to
// take any NEW integration steps, but ALWAYS reapplies whatever
// state.particles already holds once the chain has been initialized at
// least once (see 3.3 below). Only genuinely unrecoverable conditions
// (disabled, no pose yet, model not registered/stale) may still `continue`
// above this point - never a merely-zero stepCount.
int stepCount = 0;
if (rig.frozen) {
    rig.accumulatedSeconds = 0.0f; // Never bank time while frozen - un-freezing later must not trigger a multi-step catch-up burst.
} else {
    stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, static_cast<float>(deltaSeconds),
        m_globalSettings.fixedTimestepSeconds, m_globalSettings.maxStepsPerFrame);
}
```

Then unconditionally build the batch context and dispatch/step, exactly as
today, just without the removed `if (stepCount <= 0) { continue; }` line in
between:

```cpp
DynamicChainBatchContext context{ &model->skeleton, &model->chains, &rig.chainStates, &resolvedPose->pose,
    stepCount, m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind,
    entityWorldMatrix, entityWorldMatrixInverse, rig.frozen };

std::size_t totalJoints = 0;
for (const DynamicChainDefinition& chain : model->chains) {
    totalJoints += chain.jointBoneIndices.size();
}

if (totalJoints >= kMinDynamicJointsToParallelize && model->chains.size() > 1) {
    Jobs::JobHandle chainHandle;
    Jobs::Dispatch(&RunDynamicChainBatchJob, static_cast<std::uint32_t>(model->chains.size()), &context,
        chainHandle, /*minItemsPerBatch=*/1);
    Jobs::JobSystem::Instance().WaitForJobs(chainHandle);
} else {
    StepDynamicChainRange(0, static_cast<std::uint32_t>(model->chains.size()), context);
}
```

This is unchanged from today's existing dispatch code — the only actual
change in this method is removing the early `continue` and computing
`stepCount`/`rig.frozen`-handling as shown above, in its place.

### 3.3 — `StepDynamicChainRange()`: split "integrate" from "reapply," and always do the latter

```cpp
for (std::uint32_t chainIndex = beginIndex; chainIndex < endIndex; ++chainIndex) {
    const DynamicChainDefinition& chain = (*context.chains)[chainIndex];
    DynamicChainRuntimeState& state = (*context.chainStates)[chainIndex];

    const Mat4 rootWorld = context.entityWorldMatrix
        * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.rootBoneIndex);
    const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

    std::vector<Vec3> animatedJointWorldPositions;
    animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
    for (std::int32_t boneIndex : chain.jointBoneIndices) {
        const Mat4 jointWorld = context.entityWorldMatrix
            * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex);
        animatedJointWorldPositions.push_back(jointWorld.TransformPoint(Vec3::Zero()));
    }

    SphereCollider collider;
    bool hasCollider = false;
    if (chain.hasHeadCollider) {
        const Mat4 colliderWorld = context.entityWorldMatrix
            * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.headColliderBoneIndex);
        collider.center = colliderWorld.TransformPoint(Vec3::Zero());
        collider.radius = chain.headColliderRadius;
        hasCollider = true;
    }

    // task_manager/verlet-integration-7, Phase 4 (v2) - take zero NEW
    // integration steps whenever context.stepCount == 0, for EITHER reason
    // (frozen, or an ordinary sub-threshold accumulator frame - see this
    // file's own Step 3.2 above). `for (int step = 0; step < 0; ...)`
    // already naturally executes zero iterations on its own for the
    // ordinary case - the explicit `if (!context.frozen)` guard exists
    // purely so a frozen rig's `stepCount` (always 0, per 3.2) can never
    // accidentally be made nonzero by a future change without ALSO
    // re-checking this guard.
    if (!context.frozen) {
        for (int step = 0; step < context.stepCount; ++step) {
            StepDynamicChain(chain, rootWorldPos, animatedJointWorldPositions, state, context.fixedTimestepSeconds,
                context.gravity, context.wind, hasCollider ? &collider : nullptr);
        }
    }

    // task_manager/verlet-integration-7, Phase 4 (v2, Culprit F fix) - ALWAYS
    // re-apply whatever `state.particles` currently holds into `pose`, EVERY
    // call, regardless of whether any new integration step ran just above -
    // this is what stops EvaluatePoses()'s own unconditional every-frame
    // pose overwrite (Phase 1's bind pose, or a fresh FK sample) from ever
    // being visible, even for one single rendered frame, once a chain has
    // simulated at least once. `state.initialized` guards the ONLY case
    // with nothing meaningful to reapply yet: a chain that has never once
    // called StepDynamicChain() (e.g. spawned already-frozen, before its
    // very first step) - in that case `pose` is correctly left exactly as
    // EvaluatePoses() wrote it (its own bind/FK value), which is the right
    // "nothing to show yet" behavior.
    if (state.initialized) {
        std::vector<Vec3> simulatedPositions;
        simulatedPositions.reserve(state.particles.size());
        for (const VerletParticle& particle : state.particles) {
            simulatedPositions.push_back(context.entityWorldMatrixInverse.TransformPoint(particle.position));
        }
        ApplyDynamicChainPhysicsToPose(*context.skeleton, chain, simulatedPositions, *context.pose);
    }
}
```

Note this also naturally changes the ORDINARY (non-frozen, `stepCount > 0`)
case's own shape slightly from today: `ApplyDynamicChainPhysicsToPose()` used
to be called once per SUBSTEP, inside the `for (int step...)` loop; it is now
called exactly ONCE per `StepDynamicChainRange()` call, after all substeps
for this call have already run, reading `state.particles`' FINAL post-substep
value. This is a strict improvement, never a regression: the intermediate,
mid-substep pose was never observable anyway (nothing reads `pose` between
substeps within the same `Update()` call — `SkinAndUpload()` only runs AFTER
`PhysicsSystem::Update()` has fully returned, per `Game::Update()`'s fixed
order), and this removes `constraintIterations` * `stepCount` redundant calls
to `ApplyDynamicChainPhysicsToPose()` down to exactly 1, a small, free
performance win alongside the correctness fix.

### 3.4 — `src/Editor/Panels/InspectorPanel.cpp`: expose the new checkbox

At the existing `ImGui::Checkbox("Enabled", &rig->enabled);` line (583-585),
add:

```cpp
ImGui::Checkbox("Enabled", &rig->enabled);
ImGui::SameLine();
ImGui::Checkbox("Freeze", &rig->frozen);
ImGui::TextDisabled(
    "Enabled: physics runs every frame. Freeze: keep the current jiggled shape, stop simulating further, "
    "still rides along rigidly with the model.");
```

## Step 3.5 — Tests

New cases in `tests/Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp`
(the file Phase 3 created) or `tests/Game/Physics/PhysicsSystemTests.cpp`:

1. `FrozenDynamicChainRigStopsIntegratingButKeepsItsLastSimulatedShape` —
   settle a chain under gravity for N frames, record its simulated tip world
   position, set `rig.frozen = true`, keep calling `Update()` for many MORE
   frames (gravity still configured, time still passing); assert the tip's
   world position stays approximately unchanged across those extra frames
   (within a tight epsilon), while a SECOND, sibling, un-frozen chain
   (constructed the same way, in the same test) keeps changing/settling
   differently over those same extra frames — proving `frozen` genuinely
   stops stepping without affecting an unrelated chain.
2. `FrozenDynamicChainRigStillRidesAlongRigidlyWithEntityTransformMotion` —
   settle, freeze, then translate `transform->position` by a plausible drag
   delta and call `Update()` once more; assert the chain's reconstructed
   world position (root-relative offset) is preserved from just before the
   freeze (i.e. the SHAPE relative to the entity didn't change), while its
   ABSOLUTE world position moved rigidly with the new Transform — proving
   the "still re-applies via the current inverse matrix" behavior, not a
   raw, un-reprojected freeze of `pose` itself.
3. `UnfreezingResetsAccumulatedSecondsSoNoCatchUpBurstOccurs` — freeze for a
   long simulated duration (e.g. 5 seconds' worth of `Update()` calls at a
   large `deltaSeconds` per call), then unfreeze and step ONE frame with a
   normal `deltaSeconds`; assert `ComputeFixedStepCount()`'s own
   `maxStepsPerFrame` clamp was never exercised in a surprising way (assert,
   indirectly, that the very next un-frozen step produces a SMALL, plausible
   pose change, not an implausibly large multi-second catch-up jump).
4. `DisablingADynamicChainRigStillRevertsToBindPoseExactlyAsBefore` — a
   byte-for-byte regression proving `enabled = false`'s pre-existing
   behavior (unaffected by `frozen`'s mere existence, which defaults to
   `false`) is completely unchanged — reuses
   `PhysicsSystemTests.cpp`'s own existing `DisabledDynamicChainRigIsSkippedEntirely`
   test verbatim as the ground truth to compare against.
5. **(v2, new, the single most important test this phase adds)**
   `AnOrdinaryNonFrozenChainNeverPopsBackToRawBindOrFkPoseOnARenderFrameWhereNoNewFixedStepAccumulates`
   — build a `DynamicChainRig`-only entity (no `SkeletalAnimator`, exactly
   like Phase 1's own end-to-end test), wire
   `AnimationSystem::EvaluatePoses()` → `PhysicsSystem::Update()` back-to-back
   exactly like `Game::Update()` does (mirrors Phase 1's
   `GameLoopPhysicsWithoutAnimationTests.cpp`). Drive it with a deliberately
   TINY, constant `deltaSeconds` well under `fixedTimestepSeconds` (e.g.
   `1.0/240.0`, simulating a 240 Hz display) for several hundred frames so
   `ComputeFixedStepCount()` legitimately returns `0` on the majority of
   them. After letting the chain settle for long enough to be visibly
   sagging away from bind pose at least once, assert that on EVERY
   subsequent frame — including every single `stepCount == 0` frame —
   `ResolvedAnimationPose::pose` for each chain-controlled bone stays within
   a tight epsilon of the value it held on the immediately preceding frame
   (i.e. it never snaps back toward the bind/FK value on a non-stepping
   frame, only ever changes smoothly on the rarer frames that DO cross the
   fixed-step threshold). This test MUST fail against the original v1 code
   (with the `if (stepCount <= 0) { continue; }` early-out still in place)
   and MUST pass against this phase's restructured code — write it FIRST,
   confirm it fails against the current (pre-Phase-4) tree, then implement
   the fix.
6. **(v2, new)** `AnAnimatedEntityWithADynamicChainRigAlsoNeverPopsOnAStepCountZeroFrame`
   — the same scenario as test 5, but for an entity with a genuinely-playing
   `SkeletalAnimator` (using a real motion clip, mirroring
   `AnimationSystemEvaluatePosesTests.cpp`'s own fixture) instead of a bare
   `DynamicChainRig` — proving this fix also resolves the pre-existing
   version of Culprit F that already affected every animated jiggle-physics
   model before this campaign even started, as a bonus alongside the
   T-pose-specific fix.

## Step 4: What We Will NOT Do (Focus, this phase)

- We will **not** add a third state beyond `enabled`/`frozen` (e.g. a
  separate "paused" vs. "frozen" distinction) — the user asked for exactly
  two opt-outs ("disable it or freeze it"), and these two fields fully cover
  that. Culprit F's fix is not a third state - it is a correction to how
  `stepCount == 0` is handled regardless of state.
- We will **not** add per-chain (as opposed to per-entity/per-`DynamicChainRig`)
  freeze granularity — `frozen` is a single flag for the whole rig, mirroring
  `enabled`'s own existing per-rig (not per-chain) scope exactly.
- We will **not** persist `frozen`'s state across a model reload/re-spawn —
  it lives on the ECS component exactly like `enabled` already does, with the
  same lifetime and the same lack of save/load support (an already-accepted,
  pre-existing limitation this phase does not change; this engine has no
  scene-serialization system at all yet - see `Assets/AssetTypes.h`'s own
  `Scene` roadmap note - so this is a non-issue today either way).
- We will **not** change `ComputeFixedStepCount()`/`FixedTimestepAccumulator.cpp`
  itself at all — Culprit F is entirely about how `PhysicsSystem.cpp`
  CONSUMES a legitimate `stepCount == 0` result, never about the accumulator
  math producing that result in the first place, which remains completely
  correct and untouched.
- We will **not** attempt to reduce `ApplyDynamicChainPhysicsToPose()`'s new
  "called exactly once per `Update()` call instead of once per substep"
  behavior back to its old per-substep shape for any reason — Step 3.3
  explains why the old per-substep calls were never observable anyway; this
  is a strict simplification, not a scope item requiring its own separate
  phase or sign-off.
