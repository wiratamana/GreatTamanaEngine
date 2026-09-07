# PHASE1 — Baseline `ResolvedAnimationPose` For Non-Animated Physics Rigs

Part of the `verlet-integration-7` campaign — read `PHASE0_MASTER_STRATEGY.md`
first. This phase fixes **Culprit A**: `PhysicsSystem::Update()` never runs
for an entity with no `SkeletalAnimator`/no playing clip, because
`ResolvedAnimationPose` is only ever produced for a playing animator.

## Step 1: The Goal (Where are we going?)

Any entity carrying an **enabled** `DynamicChainRig` component must have a
valid, correctly-sized `ResolvedAnimationPose` component every single frame —
whether or not it also has a `SkeletalAnimator`, and whether or not that
animator (if present) is currently playing. When there is no fresher,
genuinely-ANIMATED pose available this frame, that pose must be exactly the
model's own bind pose (an all-default `BoneLocalOffset` per bone — see
`Animation/SkeletonPose.h`'s own documented convention that an all-identity
`BoneLocalOffset` array is precisely bind/T-pose). This is the ONLY change
needed to unblock `PhysicsSystem::Update()` for a T-pose model — that method
itself, and its existing behavior for animated entities, are both completely
untouched by this phase.

Scope, per the user's own explicit answer (`PHASE0`, Step 1): **narrow** —
only entities with an enabled `DynamicChainRig` are affected. A plain skinned
prop with zero detected chains (no `DynamicChainRig` at all) pays exactly
zero extra cost from this phase.

## Step 2: The Situation / The Problem (Where are we now?)

`AnimationSystem::EvaluatePoses(Registry&, double)` (`src/Game/Animation/AnimationSystem.cpp`,
lines 196-261) does the following, and only this:

```cpp
ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();
for (std::size_t i = 0; i < animators.Size(); ++i) {
    SkeletalAnimator& animator = animators.ComponentAt(i);
    const Entity animatorEntity = animators.EntityAt(i);
    if (!animator.playing || animator.animationGtaPath.empty()) {
        continue; // <-- ResolvedAnimationPose is NEVER written for this entity.
    }
    ... sample -> IK -> append -> resolvedPose->pose = std::move(pose);
}
```

An entity that never had `Game::PlayAnimationOnEntity()` called on it never
gets a `SkeletalAnimator` component at all, so it never even reaches this
loop's body — it simply never appears in `ComponentStorage<SkeletalAnimator>`.
`PhysicsSystem::Update()` (`src/Game/Physics/PhysicsSystem.cpp`, lines
206-209) then does:

```cpp
ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
if (resolvedPose == nullptr) {
    continue; // Nothing to overwrite - AnimationSystem::EvaluatePoses() hasn't produced a pose for this entity this frame.
}
```

— permanently skipping that entity's `DynamicChainRig`, forever, every frame.
Meanwhile `DynamicChainRig` itself is ALREADY attached unconditionally for
any skinned model with at least one detected chain, regardless of animation:
`Game::CreateMeshEntityFromGtaFile()` (`src/Game/Game.cpp`, lines 56-63) calls
`m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin)` and
`m_physicsSystem.AttachDynamicChainRigIfNeeded(m_registry, root, absoluteGtaPath)`
unconditionally, right at spawn time, with no animation involved at all. So
the ONLY missing piece, confirmed by reading every relevant file end to end,
is `ResolvedAnimationPose` itself never being produced.

`DynamicChainRig::meshGtaPath` (`src/ECS/Components/DynamicChainRig.h`, line
32) already carries the exact same string key
`AnimationSystem::m_rigCache` (a `SkeletalRigCache`,
`src/Game/Animation/SkeletalRigCache.h`) is keyed by — `RegisterSkinnedMesh()`
and `RegisterDynamicChains()` are called from the SAME `Game.cpp` hand-off
site with the SAME `absoluteGtaPath` argument (`Game.cpp` lines 54, 62). This
means `AnimationSystem` can already resolve a `DynamicChainRig`'s own
skeleton via its EXISTING `m_rigCache.TryGet(rig.meshGtaPath)` — no new
registration call, no new cache, and no new cross-system dependency is
needed; `AnimationSystem.cpp` only needs to `#include` the `DynamicChainRig`
component header and add one more loop.

## Step 3: The Plan (How will we get there?)

### 3.1 — `src/Game/Animation/AnimationSystem.cpp`

Add `#include "../../ECS/Components/DynamicChainRig.h"` to the includes list
(alongside the existing `#include "../../ECS/Components/ResolvedAnimationPose.h"`
and `#include "../../ECS/Components/SkeletalAnimator.h"`).

In `EvaluatePoses(Registry& registry, double deltaSeconds)`:

1. Keep the existing `SkeletalAnimator` loop **completely unchanged**, except:
   declare a local `std::unordered_set<Entity> animatedThisFrame;` right
   before the loop starts, and insert `animatedThisFrame.insert(animatorEntity);`
   at the exact point the existing code already does
   `resolvedPose->pose = std::move(pose);` (the last line of that loop body,
   currently line 259). `#include <unordered_set>` if not already present in
   this file (it is not — check the current includes list first).
2. Immediately after that loop (still inside `EvaluatePoses()`, before the
   function returns), add a **second loop**, over
   `ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();`:

```cpp
for (std::size_t i = 0; i < rigs.Size(); ++i) {
    DynamicChainRig& rig = rigs.ComponentAt(i);
    if (!rig.enabled) {
        continue; // Mirrors PhysicsSystem::Update()'s own "disabled -> skip entirely" convention - no wasted work.
    }
    const Entity entity = rigs.EntityAt(i);
    if (animatedThisFrame.count(entity) > 0) {
        continue; // Already given a fresh, genuinely-ANIMATED pose above this same call - never clobber it.
    }

    const SkinnedMeshData* skinData = m_rigCache.TryGet(rig.meshGtaPath);
    if (skinData == nullptr) {
        continue; // Not (yet) registered - degrade gracefully, same convention as the loop above.
    }

    // Baseline/idle pose - task_manager/verlet-integration-7, Phase 1. Bind
    // pose (an all-default BoneLocalOffset per bone) is exactly what
    // Animation/SkeletonPose.h's own file comment defines as "unrotated,
    // T/A-pose" - this is PhysicsSystem::Update()'s pre-physics INPUT for a
    // model with no active animation, mirrored exactly the same way an
    // animated model's own freshly-sampled pose is its pre-physics input.
    // Always a full, fresh overwrite (never merged with a previous frame's
    // leftover value) - EvaluatePoses() runs BEFORE PhysicsSystem::Update()
    // every frame (see Game::Update()'s fixed call order), so this is always
    // exactly the "before physics touches it" snapshot, whether the entity
    // is animated or not.
    ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    if (resolvedPose == nullptr) {
        resolvedPose = &registry.AddComponent<ResolvedAnimationPose>(entity);
    }
    resolvedPose->pose.assign(skinData->skeleton.bones.size(), BoneLocalOffset{});
}
```

3. Update `EvaluatePoses()`'s own doc comment in `src/Game/Animation/AnimationSystem.h`
   (currently lines 110-130) to describe this new second pass, its narrow
   `DynamicChainRig`-only scope, and the "never clobber a fresher animated
   pose written this same call" rule — the exact same standard of precision
   the existing comment already holds itself to.

### 3.2 — Why this is safe / does not regress anything

- `PhysicsSystem.cpp` is **not touched** by this phase at all. Its own
  existing guard (`resolvedPose == nullptr -> continue`) keeps working
  exactly as documented; it simply now always finds a non-null pose for any
  enabled `DynamicChainRig` entity, because `EvaluatePoses()` guarantees one
  exists before `PhysicsSystem::Update()` ever runs (fixed call order,
  `Game::Update()`).
- `PhysicsSystemTests.cpp`'s existing
  `UpdateSafelyNoOpsOnAnEntityWithDynamicChainRigButNoResolvedAnimationPoseYet`
  test still passes unmodified — it constructs its `Registry` by hand and
  calls `PhysicsSystem::Update()` directly, never going through
  `AnimationSystem::EvaluatePoses()` at all, so it is testing
  `PhysicsSystem`'s own degrade-gracefully contract in isolation, which this
  phase never changes.
- A disabled rig (`rig.enabled == false`) is skipped by this new pass, so it
  never gets a `ResolvedAnimationPose` added purely by this phase (matches
  `PhysicsSystem::Update()`'s own existing "disabled -> skip" behavior,
  meaning a model that never enables physics behaves byte-for-byte as before
  this whole campaign).
- An entity with BOTH a playing `SkeletalAnimator` and a `DynamicChainRig`
  (the common "animated character with jiggle hair" case) is completely
  unaffected — `animatedThisFrame` guarantees the new pass never overwrites
  its freshly-sampled animated pose with a bind-pose reset.

## Step 3.3 — Tests (`tests/Game/Animation/AnimationSystemEvaluatePosesTests.cpp`)

Add the following `TEST_F(AnimationSystemEvaluatePosesTest, ...)` cases,
reusing the fixture's existing `BuildTwoBoneSkeleton()`/temp-directory
conventions already in this file:

1. `EvaluatePosesWritesBindPoseForADynamicChainRigOnlyEntityWithNoSkeletalAnimatorAtAll`
   — register a skeleton via `RegisterSkinnedMesh()`, create an entity with
   **only** `registry.AddComponent<DynamicChainRig>(entity, DynamicChainRig{ meshPath, {}, 0.0f, true })`
   (no `SkeletalAnimator`, no `MeshAssetSource` needed), call
   `EvaluatePoses(registry, 0.016)`, assert
   `registry.HasComponent<ResolvedAnimationPose>(entity)`, assert
   `resolvedPose->pose.size() == skeleton.bones.size()`, and assert every
   entry equals a default-constructed `BoneLocalOffset{}` (bind pose) via
   `ApproximatelyEqual`/`RepresentSameRotation` exactly like the file's
   existing assertions.
2. `EvaluatePosesDoesNotClobberAFreshlyAnimatedPoseOnAnEntityThatHasBothAPlayingSkeletalAnimatorAndADynamicChainRig`
   — build an entity with a real motion clip via `Play()` (mirroring
   `EvaluatePosesWritesResolvedAnimationPoseMatchingManualEvaluation`'s own
   existing setup) **plus** an added `DynamicChainRig{ meshPath }`, call
   `EvaluatePoses(registry, 0.0)`, and assert the resulting pose still
   exactly matches `EvaluateAnimatedPoseBeforePhysics(...)`'s own
   independently-computed ground truth — i.e. it is NOT the bind pose,
   proving the second pass never clobbers it.
3. `EvaluatePosesFallsBackToBindPoseForADynamicChainRigEntityWhoseSkeletalAnimatorExistsButIsNotPlaying`
   — call `EvaluatePoses()` TWICE: first with `animator.playing = true` and a
   real clip (producing a non-identity pose, proving the fixture actually
   animates), then set `animator.playing = false` and call `EvaluatePoses()`
   again; assert the SECOND call's resulting pose is exactly bind pose — this
   is the regression that matters most: an entity must not get permanently
   "stuck" holding stale animated data once its animator stops, it must fall
   back to a clean baseline every frame it isn't actively animating.
4. `EvaluatePosesSkipsADisabledDynamicChainRigEntirely`
   — `rig.enabled = false`, no `SkeletalAnimator`; assert
   `EXPECT_FALSE(registry.HasComponent<ResolvedAnimationPose>(entity))` after
   `EvaluatePoses()`.
5. `EvaluatePosesDegradesGracefullyForADynamicChainRigWhoseModelWasNeverRegistered`
   — `rig.meshGtaPath = "NeverRegistered.gta"`, never call
   `RegisterSkinnedMesh()`; assert `EXPECT_NO_FATAL_FAILURE(...)` and no
   `ResolvedAnimationPose` added.

## Step 3.4 — New end-to-end proof: `tests/Game/GameLoopPhysicsWithoutAnimationTests.cpp`

A brand-new Tier-1 test file (plain `Registry`, no GPU/SDL/ImGui — mirrors the
existing `PhysicsSystemTests.cpp`'s own fixture style exactly, including its
synthetic 4-bone `SkinnedMeshData`/`PhysicsData` rig construction) that wires
`AnimationSystem::EvaluatePoses()` → `PhysicsSystem::Update()` back-to-back,
by hand, in the SAME fixed order `Game::Update()` itself uses, for an entity
carrying **only** a `DynamicChainRig` (no `SkeletalAnimator`, ever) — proving
this is not just individually-testable plumbing but a genuine, working,
wired-together pipeline:

`TEST(GameLoopPhysicsWithoutAnimationTests, ADynamicChainRigOnlyEntityVisiblySagsUnderGravityAcrossManyFramesWithNoAnimationEverPlayed)`

- Build a real `AnimationSystem` + `PhysicsSystem` pair (mirrors
  `AnimationSystemEvaluatePosesTests.cpp`'s own `RenderSystem`/
  `MeshInstantiationSystem` construction pattern), register the same style of
  synthetic 2-joint chain fixture `PhysicsSystemTests.cpp`'s own
  `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity` test
  already uses (a `Static` anchor rigid body + two `Dynamic` rigid bodies,
  jointed, extending perpendicular to gravity) via
  `animationSystem.RegisterSkinnedMesh(path, data)` **and**
  `physicsSystem.RegisterDynamicChains(path, data)`, spawn one entity, call
  `physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path)`, and
  deliberately **never** call `animationSystem.Play(...)` for it (no
  `SkeletalAnimator` component ever exists on this entity, by construction).
- Set `physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0, -9.8f, 0)`.
- Loop 60 times: `animationSystem.EvaluatePoses(registry, 1.0/60.0);
  physicsSystem.Update(registry, 1.0/60.0);` — exactly `Game::Update()`'s own
  two-call sequence (its third call, `SkinAndUpload()`, is Phase 2's concern
  and is irrelevant to this pose-level assertion).
- Assert `registry.HasComponent<ResolvedAnimationPose>(entity)` and that the
  resulting pose visibly diverges from a fresh, pure bind pose (same
  divergence-detection pattern `PhysicsSystemTests.cpp`'s own test already
  uses) — the literal, automated proof that "T-pose actually simulates
  physics" now holds, wired exactly the way the real engine calls it, so any
  future change that re-couples the two systems (e.g. someone "helpfully"
  re-adding an `animator.playing` check inside `PhysicsSystem::Update()`
  itself) is caught immediately by this test.

## Step 4: What We Will NOT Do (Focus, this phase)

- We will **not** touch `PhysicsSystem.h`/`.cpp` at all in this phase — every
  fix here lives in `AnimationSystem.h`/`.cpp` only.
- We will **not** make the baseline-pose pass apply to every skinned model
  unconditionally — only entities with an enabled `DynamicChainRig`, per the
  user's own explicit "narrow" answer.
- We will **not** attempt to preserve a "last known animated pose" as the
  fallback when an animator stops playing — the fallback is always the
  bind/T-pose, a simple, obvious, well-defined baseline (Test 3 above locks
  this in explicitly).
- We will **not** touch `SkinAndUpload()` in this phase — a
  `DynamicChainRig`-only entity's freshly-produced pose exists after this
  phase, but is not yet visibly rendered; that is Phase 2's job entirely.
