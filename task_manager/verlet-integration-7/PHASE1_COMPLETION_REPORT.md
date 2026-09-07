# PHASE1 — Completion Report: Baseline `ResolvedAnimationPose` For Non-Animated Physics Rigs

Part of the `verlet-integration-7` campaign. Implements
`PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md` exactly as
specified (re-verified against the current `src/` tree before implementation
— every citation in that document matched the real source). No previous
phase report exists in this campaign folder to read from; Phase 1 is this
campaign's first implementation slice, building only on the already-landed
`verlet-integration-1` through `verlet-integration-6` work described in
`PHASE0_MASTER_STRATEGY.md`.

## What was done

### `src/Game/Animation/AnimationSystem.cpp`

- Added `#include "../../ECS/Components/DynamicChainRig.h"` and
  `#include <unordered_set>`.
- `EvaluatePoses(Registry&, double)` now runs a **second pass** immediately
  after its existing `SkeletalAnimator` loop:
  - The existing loop is unchanged except it now records every entity it
    wrote a fresh, genuinely-ANIMATED pose for into a new local
    `std::unordered_set<Entity> animatedThisFrame`.
  - The new pass iterates `ComponentStorage<DynamicChainRig>`. For each
    **enabled** rig whose entity was **not** already animated this same
    call, and whose model **is** registered in `m_rigCache`, it
    adds/overwrites that entity's `ResolvedAnimationPose::pose` with a full
    bind-pose array (`std::vector<BoneLocalOffset>` sized to the skeleton's
    bone count, every entry default-constructed — `BoneLocalOffset{}` is
    exactly identity translation/rotation, i.e. bind/T-pose).
  - A disabled rig, or one whose model isn't (yet) registered, is skipped
    entirely with no `ResolvedAnimationPose` added — mirrors
    `PhysicsSystem::Update()`'s own "disabled → skip" convention exactly, so
    a model that never opts into physics behaves byte-for-byte as before
    this whole campaign.

### `src/Game/Animation/AnimationSystem.h`

- Extended `EvaluatePoses()`'s doc comment to describe the new second pass,
  its narrow `DynamicChainRig`-only scope, and the "never clobber a fresher
  animated pose written this same call" rule.

### `PhysicsSystem.cpp`/`.h`

- **Not touched at all**, exactly as the phase plan required. Its existing
  `resolvedPose == nullptr → continue` guard now simply always finds a
  non-null pose for any enabled `DynamicChainRig` entity, because
  `EvaluatePoses()` guarantees one exists first (fixed `Game::Update()` call
  order: `EvaluatePoses()` → `PhysicsSystem::Update()` →
  `SkinAndUpload()`).

## Tests added

### `tests/Game/Animation/AnimationSystemEvaluatePosesTests.cpp`

Added `#include "ECS/Components/DynamicChainRig.h"` plus 5 new `TEST_F`
cases on the existing fixture:

1. `EvaluatePosesWritesBindPoseForADynamicChainRigOnlyEntityWithNoSkeletalAnimatorAtAll`
   — an entity with only a `DynamicChainRig` (no `SkeletalAnimator` at all)
   gets a full bind-pose `ResolvedAnimationPose`, every entry identity.
2. `EvaluatePosesDoesNotClobberAFreshlyAnimatedPoseOnAnEntityThatHasBothAPlayingSkeletalAnimatorAndADynamicChainRig`
   — an entity with both components keeps its genuinely-animated pose,
   proven against an independently-computed ground truth
   (`EvaluateAnimatedPoseBeforePhysics()`).
3. `EvaluatePosesFallsBackToBindPoseForADynamicChainRigEntityWhoseSkeletalAnimatorExistsButIsNotPlaying`
   — calls `EvaluatePoses()` twice (once playing, producing a proven
   non-identity pose; once with `playing = false`) and asserts the second
   call's result is exactly bind pose — the regression that matters most:
   an entity must not get stuck holding stale animated data once its
   animator stops.
4. `EvaluatePosesSkipsADisabledDynamicChainRigEntirely` — `rig.enabled =
   false` never gets a `ResolvedAnimationPose` added.
5. `EvaluatePosesDegradesGracefullyForADynamicChainRigWhoseModelWasNeverRegistered`
   — an unregistered `meshGtaPath` degrades gracefully (no crash, no
   component added).

### `tests/Game/GameLoopPhysicsWithoutAnimationTests.cpp` (new file)

`GameLoopPhysicsWithoutAnimationTests.ADynamicChainRigOnlyEntityVisiblySagsUnderGravityAcrossManyFramesWithNoAnimationEverPlayed`
— wires a real `AnimationSystem` + `PhysicsSystem` pair together, by hand, in
`Game::Update()`'s own exact two-call order
(`EvaluatePoses()` → `PhysicsSystem::Update()`), against the same synthetic
2-joint chain fixture (`Static` anchor + two `Dynamic` rigid bodies, jointed,
extending perpendicular to gravity) `PhysicsSystemTests.cpp`'s own
`RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity` test uses.
Deliberately **never** calls `AnimationSystem::Play()` — no
`SkeletalAnimator` component ever exists on the entity. After 60 simulated
frames under gravity, the resulting `ResolvedAnimationPose` is asserted to
visibly diverge from a pure bind pose — the literal, automated,
end-to-end proof that "T-pose actually simulates physics" now holds, wired
exactly the way the real engine calls it.

Registered in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list, alongside
the existing Animation/Physics test entries.

## Verification

- **Fast compile check only** (per this task's own workflow rules — no full
  build/regression yet):
  - `cmake --build build --target gte_core` — succeeded, 0 errors
    (rebuilt `AnimationSystem.cpp.obj` plus everything that depends on it).
  - `cmake -S . -B build` (reconfigure, needed since `tests/CMakeLists.txt`'s
    source list changed) — succeeded.
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded, 0
    errors.
- As an extra correctness check beyond a bare compile (not a full
  regression run), the newly-added tests were executed directly via a
  `--gtest_filter`:
  - `AnimationSystemEvaluatePosesTest.*` (8 tests, including the 3
    pre-existing ones) — all passed.
  - `GameLoopPhysicsWithoutAnimationTests.*` (1 test) — passed.
  - `PhysicsSystemTests.*` (6 pre-existing tests, to confirm this phase's
    change to `AnimationSystem.cpp` didn't regress `PhysicsSystem`'s own
    already-passing suite) — all passed, unchanged.
- The full test suite (`ctest`) was **not** run, per this task's explicit
  "no full build/regression yet" instruction — that is reserved for a later
  phase in this campaign (or an explicit full-build instruction).

## What this unblocks

Any entity with an enabled `DynamicChainRig` now gets a valid, correctly
sized `ResolvedAnimationPose` every frame, whether or not it also has a
`SkeletalAnimator` — including a model that has **never** had
`Game::PlayAnimationOnEntity()` called on it at all. `PhysicsSystem::Update()`
itself required zero changes to start working for this case; its existing
`resolvedPose == nullptr → continue` guard was already correct, it just
never had anything to find before this phase. This directly unblocks
Phase 2 (visibility — re-skin/re-upload a physics-only entity's pose so it's
actually visible on screen), which is the next task in this campaign's
strict phase order.

## Scope discipline (confirmed)

- Only entities with an **enabled** `DynamicChainRig` are affected — a
  plain skinned prop with zero detected chains pays exactly zero extra cost
  (per the user's own explicit "narrow" answer, `PHASE0_MASTER_STRATEGY.md`,
  Step 1).
- `PhysicsSystem.h`/`.cpp` were not touched at all.
- `SkinAndUpload()` was not touched — a `DynamicChainRig`-only entity's
  freshly-produced pose exists after this phase but is not yet visibly
  rendered; that remains Phase 2's job entirely, exactly as planned.
- No fallback "last known animated pose" was implemented — the fallback is
  always the bind/T-pose, locked in by test 3 above.

## Next step

Proceed to `PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md`.
