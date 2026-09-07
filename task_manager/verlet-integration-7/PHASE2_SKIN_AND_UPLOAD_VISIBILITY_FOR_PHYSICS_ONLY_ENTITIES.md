# PHASE2 — `SkinAndUpload()` Visibility For Physics-Only Entities

Part of the `verlet-integration-7` campaign — read `PHASE0_MASTER_STRATEGY.md`
and `PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md` first.
This phase depends directly on Phase 1's edits to `AnimationSystem.h/.cpp`
already being landed. This phase fixes **Culprit B**: even after Phase 1,
`PhysicsSystem::Update()`'s simulated pose for a non-animated entity is never
re-skinned/re-uploaded to the GPU, so it is computed but never actually seen.

## Step 1: The Goal (Where are we going?)

`AnimationSystem::SkinAndUpload()` must re-skin (CPU mode) or re-upload bone
matrices + mark for compute dispatch (GPU mode) for **every** entity that has
a fresh `ResolvedAnimationPose` this frame because of Phase 1 — i.e. any
enabled `DynamicChainRig`-only entity — not only entities with an actively
playing `SkeletalAnimator`. This must work correctly for BOTH
`AnimationSystem::SkinningMode::CpuJobSystem` (today's default) and
`SkinningMode::GpuCompute`, with zero need to special-case or disable either
mode, and without breaking the method's own documented strict-sequential,
shared-GPU-mesh-buffer safety rule.

## Step 2: The Situation / The Problem (Where are we now?)

`SkinAndUpload(Registry& registry)` (`src/Game/Animation/AnimationSystem.cpp`,
lines 263-463) iterates `ComponentStorage<SkeletalAnimator>& animators` only,
with the identical gate `EvaluatePoses()` used to have before Phase 1:

```cpp
if (!animator.playing || animator.animationGtaPath.empty()) {
    continue;
}
```

Every subsequent lookup in the method's body —
`m_rigCache.TryGet(animator.meshGtaPath)` (`SkeletalRigCache`),
`m_gpuRigCache.TryGet(animator.meshGtaPath)` (`GpuSkinningRigCache`),
`m_meshInstantiationSystem.TryGetMeshAssetParts(animator.meshGtaPath)` — is
keyed **purely by the mesh path string**, never by anything specific to
`SkeletalAnimator` itself. `DynamicChainRig::meshGtaPath`
(`ECS/Components/DynamicChainRig.h`, line 32) carries the exact same string,
by the exact same convention `PhysicsSystem.cpp` already documents
explicitly ("lets `PhysicsSystem` process an entity knowing only 'this
entity has a `DynamicChainRig` and (hopefully) a `ResolvedAnimationPose`,'
with no assumption that a `SkeletalAnimator` component ... even exists on
it"). Nothing in `SkinAndUpload()`'s own body structurally requires a
`SkeletalAnimator` — it is purely an artifact of which entity SET this one
loop currently walks.

`GpuSkinningRigCache::Register()` (`Game/Animation/GpuSkinningRigCache.h`)
is ALSO already called unconditionally at spawn time, alongside
`RegisterSkinnedMesh()`, regardless of animation (`Game.cpp`, lines 65-75) —
so a `DynamicChainRig`-only entity's model already has valid GPU-skinning
resources registered and ready, exactly like an animated one's.

## Step 3: The Plan (How will we get there?)

### 3.1 — Extract the existing per-entity body into a private helper

In `src/Game/Animation/AnimationSystem.h`, add a new private method:

```cpp
// task_manager/verlet-integration-7, Phase 2 - the ENTIRE existing
// per-entity body of SkinAndUpload(), extracted verbatim (zero logic
// change) so it can be called for an entity discovered via EITHER a
// playing SkeletalAnimator OR a DynamicChainRig (see SkinAndUpload()'s own
// updated doc comment) without maintaining two copies of the real
// skin/pack/upload logic. `entity` is whichever entity owns the
// ResolvedAnimationPose to read from; `meshGtaPath` is that entity's own
// resolved model path (SkeletalAnimator::meshGtaPath OR
// DynamicChainRig::meshGtaPath - both are the same string convention);
// `mode` is the SAME snapshotted SkinningMode SkinAndUpload() itself
// captured at the top of its own call.
void SkinAndUploadOneEntity(Registry& registry, Entity entity, const std::string& meshGtaPath, SkinningMode mode);
```

In `src/Game/Animation/AnimationSystem.cpp`, move the ENTIRE existing loop
body of `SkinAndUpload()` (currently everything from
`const SkinnedMeshData* skinData = m_rigCache.TryGet(animator.meshGtaPath);`
down to the closing brace of the `for (const MeshAssetPartGroup& group :
groups)` loop, i.e. lines ~310-461) into this new method, replacing every
`animator.meshGtaPath` reference with the new `meshGtaPath` parameter and
every `animatorEntity` reference with the new `entity` parameter. This is a
**pure mechanical extraction** — no behavior changes inside the extracted
body at all.

### 3.2 — Rebuild `SkinAndUpload()`'s own outer loop around a combined entity list

Replace `SkinAndUpload()`'s body with:

```cpp
void AnimationSystem::SkinAndUpload(Registry& registry)
{
    GTE_PROFILE_SCOPE("AnimationSystem::SkinAndUpload");

    const SkinningMode mode = m_mode;
    m_gpuModelsNeedingDispatchThisFrame.clear();

    // *** OUTER PROCESSING MUST REMAIN STRICTLY SEQUENTIAL, ONE MODEL AT A
    // TIME - see this method's own header comment in AnimationSystem.h for
    // the full shared-GPU-mesh-buffer rationale, UNCHANGED by this phase. ***

    std::unordered_set<Entity> processedThisFrame;

    // Source 1 - every actively-playing SkeletalAnimator, EXACT existing
    // order/logic, now delegated to SkinAndUploadOneEntity().
    ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();
    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
        const Entity animatorEntity = animators.EntityAt(i);
        if (!animator.playing || animator.animationGtaPath.empty()) {
            continue;
        }
        SkinAndUploadOneEntity(registry, animatorEntity, animator.meshGtaPath, mode);
        processedThisFrame.insert(animatorEntity);
    }

    // Source 2 (task_manager/verlet-integration-7, Phase 2) - every enabled
    // DynamicChainRig entity Phase 1's EvaluatePoses() guaranteed a fresh
    // ResolvedAnimationPose for THIS SAME frame, that Source 1 above did NOT
    // already process (an entity may legitimately carry BOTH components -
    // it must be skinned/uploaded exactly ONCE per frame, never twice, which
    // would double-upload the same shared GPU vertex buffer).
    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        DynamicChainRig& rig = rigs.ComponentAt(i);
        if (!rig.enabled) {
            continue;
        }
        const Entity entity = rigs.EntityAt(i);
        if (processedThisFrame.count(entity) > 0) {
            continue;
        }
        if (!registry.HasComponent<ResolvedAnimationPose>(entity)) {
            continue; // Phase 1 didn't (or couldn't - e.g. unregistered model) produce one this frame.
        }
        SkinAndUploadOneEntity(registry, entity, rig.meshGtaPath, mode);
    }
}
```

`#include <unordered_set>` in `AnimationSystem.cpp` if not already present
(check first — Phase 1 may have already added it for its own
`animatedThisFrame` set in `EvaluatePoses()`; if so, reuse the same include).
`#include "../../ECS/Components/DynamicChainRig.h"` — Phase 1 already added
this to `AnimationSystem.cpp`, so no new include is needed here.

### 3.3 — Update `SkinAndUpload()`'s doc comment (`AnimationSystem.h`)

Update the existing doc comment (currently lines 132-161) to describe the new
combined entity set (Source 1 + Source 2), explicitly reference Phase 1's
`ResolvedAnimationPose` guarantee for `DynamicChainRig`-only entities, and
call out that `SkinAndUploadOneEntity()` is now the single, shared body both
sources call into — mirroring this file's own existing standard of precision.

### 3.4 — Tests

New tests in `tests/Game/Animation/AnimationSystemEvaluatePosesTests.cpp`
(reusing its existing fixture class) or a new sibling file
`tests/Game/Animation/AnimationSystemSkinAndUploadPhysicsOnlyTests.cpp` if the
existing file is judged large enough already to warrant a split — either is
acceptable, but every case below must exist:

1. `SkinAndUploadProcessesADynamicChainRigOnlyEntityWithNoSkeletalAnimatorAtAll`
   — build a `SkinnedMeshData` with real vertex/skin-weight data (mirroring
   the existing `SkinAndUploadDoesNotCrashWhenNoGpuMeshPartsAreRegistered`
   fixture), register it, create an entity with only a `DynamicChainRig`,
   manually populate its `ResolvedAnimationPose` (simulating what Phase 1's
   `EvaluatePoses()` would have already written), call `SkinAndUpload()`, and
   assert `EXPECT_NO_FATAL_FAILURE`. Since this fixture has no real
   `MeshAssetPart`s registered (mirrors the existing test's own scope), the
   assertion here is the same "degrades gracefully, doesn't crash" bar the
   sibling existing test already uses — the crucial thing being proven is
   that the method reaches (and returns cleanly from) `SkinAndUploadOneEntity()`
   for this entity AT ALL, which it structurally could not before this phase.
2. `SkinAndUploadNeverProcessesAnEntityTwiceWhenItHasBothAPlayingSkeletalAnimatorAndADynamicChainRig`
   — build an entity with both components + `SkinningMode::GpuCompute`
   (`animationSystem.SetSkinningMode(AnimationSystem::SkinningMode::GpuCompute)`),
   a real GPU-registered model (`RegisterGpuSkinnedMesh()`), call
   `EvaluatePoses()` then `SkinAndUpload()`, and assert
   `animationSystem.CollectModelsNeedingGpuSkinningThisFrame()` contains
   **exactly one** entry for this model path (proving `SkinAndUploadOneEntity()`
   ran exactly once for this entity this frame, not twice — a double-run
   would still be de-duplicated by `m_gpuModelsNeedingDispatchThisFrame`'s
   own existing `std::find` guard, so additionally assert indirectly that
   the method did not throw/crash from a double `descriptorSet`/buffer touch
   either, via `EXPECT_NO_FATAL_FAILURE`).
3. `SkinAndUploadSkipsADisabledDynamicChainRigEntirely`
   — `rig.enabled = false`, no `SkeletalAnimator`, a manually-populated
   `ResolvedAnimationPose` present anyway; assert
   `EXPECT_NO_FATAL_FAILURE(animationSystem.SkinAndUpload(registry))` and (if
   feasible to observe) that no GPU/CPU work was scheduled for it — mirrors
   `PhysicsSystemTests.cpp`'s own `DisabledDynamicChainRigIsSkippedEntirely`
   test in spirit.
4. `SkinAndUploadSkipsADynamicChainRigEntityWithNoResolvedAnimationPoseYet`
   — a `DynamicChainRig`-only entity where `EvaluatePoses()` was deliberately
   never called (or targeted an unregistered model path) — assert
   `EXPECT_NO_FATAL_FAILURE` and no crash, mirroring the existing
   `SkinAndUploadSkipsAnEntityWithNoResolvedAnimationPoseYet` test's own
   "not (yet) available - continue" convention, now proven for Source 2 too.

## Step 4: What We Will NOT Do (Focus, this phase)

- We will **not** restructure `SkinningBatchContext`/`RunSkinningBatch`/
  `PackUntexturedBatchContext`/`PackTexturedBatchContext` or any
  `Jobs::Dispatch()` internals — this phase only widens WHICH entities reach
  the existing, unmodified `SkinAndUploadOneEntity()` body.
- We will **not** attempt to disable or special-case GPU-compute skinning
  mode for physics-only entities — Phase 0's investigation already confirmed
  this widened iteration is sufficient for both modes.
- We will **not** touch `Application/RenderPasses.cpp`'s
  `AddGpuSkinningPasses()` call site or
  `CollectModelsNeedingGpuSkinningThisFrame()`'s own signature/logic — both
  already work purely off `m_gpuModelsNeedingDispatchThisFrame`, which this
  phase populates through the exact same existing code path, just from a
  wider set of callers.
