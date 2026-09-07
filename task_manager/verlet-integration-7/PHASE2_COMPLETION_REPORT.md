# PHASE2 — Completion Report: `SkinAndUpload()` Visibility For Physics-Only Entities

Part of the `verlet-integration-7` campaign. Implements
`PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md` exactly as
specified, following on from `PHASE1_COMPLETION_REPORT.md` (Phase 1's
baseline `ResolvedAnimationPose` for non-animated `DynamicChainRig` entities,
already landed and re-verified as unchanged by this phase). This phase fixes
**Culprit B**: even after Phase 1, a physics-only (never-animated) entity's
freshly computed pose was never re-skinned/re-uploaded to the GPU, so it was
computed but never actually visible on screen.

## What was done

### `src/Game/Animation/AnimationSystem.h`

- Added a new **private** method declaration:
  `void SkinAndUploadOneEntity(Registry& registry, Entity entity, const std::string& meshGtaPath, SkinningMode mode);`
  — the single shared per-entity skin/pack/upload body both entity sources
  (SkeletalAnimator and DynamicChainRig) now call into.
- Rewrote `SkinAndUpload()`'s own doc comment to describe the two entity
  sources (Source 1 = playing `SkeletalAnimator`, Source 2 = enabled
  `DynamicChainRig` not already covered by Source 1), the de-duplication
  guarantee (an entity carrying both components is processed exactly once),
  and the still-mandatory "strictly sequential, one model at a time"
  constraint — now phrased generically ("regardless of which source
  discovered it") rather than solely in terms of `SkeletalAnimator`.

### `src/Game/Animation/AnimationSystem.cpp`

- **Extracted** the entire existing per-entity body of the old
  `SkinAndUpload()` (mode branch, GPU bone-matrix upload +
  `m_gpuModelsNeedingDispatchThisFrame` bookkeeping, CPU
  `SkinVertexRange()`/`Jobs::Dispatch()` skinning, `MeshAssetPartGroup`
  packing/upload) verbatim into the new `SkinAndUploadOneEntity()` — a pure
  mechanical extraction, zero logic change. Every `animator.meshGtaPath`
  reference became the new `meshGtaPath` parameter and every
  `animatorEntity` reference became the new `entity` parameter; every early
  `continue` in the old loop body became a `return` in the new function.
- **Rebuilt** `SkinAndUpload()`'s own body around a combined entity list,
  exactly per the phase plan:
  - Snapshots `mode` and clears `m_gpuModelsNeedingDispatchThisFrame` once,
    at the top, unchanged from before.
  - **Source 1** — the original `ComponentStorage<SkeletalAnimator>` loop,
    now delegating its body to `SkinAndUploadOneEntity()`, and recording
    every processed entity into a new local
    `std::unordered_set<Entity> processedThisFrame`.
  - **Source 2** — a new pass over `ComponentStorage<DynamicChainRig>`:
    skips a disabled rig, skips an entity already processed by Source 1
    (`processedThisFrame`), skips an entity with no `ResolvedAnimationPose`
    yet (Phase 1 didn't produce one this frame, e.g. an unregistered
    model), and otherwise calls `SkinAndUploadOneEntity()` for it too.
  - The "outer processing must remain strictly sequential" warning comment
    was preserved and generalized to cover both sources — no restructuring
    that would fire off multiple entities' `Dispatch()` calls concurrently
    was introduced.
- `#include <unordered_set>` and
  `#include "../../ECS/Components/DynamicChainRig.h"` were already present
  (added by Phase 1) — no new includes needed.

## Tests added

### `tests/Game/Animation/AnimationSystemSkinAndUploadPhysicsOnlyTests.cpp` (new file)

Created as a sibling file to the existing (already sizeable)
`AnimationSystemEvaluatePosesTests.cpp`, per the phase plan's own "either is
acceptable" allowance. Four `TEST` cases, all Tier 1 (no live
Renderer/GPU device — `RenderSystem`/`MeshInstantiationSystem` are both
default-constructible with no GPU dependency):

1. `SkinAndUploadProcessesADynamicChainRigOnlyEntityWithNoSkeletalAnimatorAtAll`
   — a real, minimal 2-vertex skinned mesh (mirroring the existing
   `SkinAndUploadDoesNotCrashWhenNoGpuMeshPartsAreRegistered` fixture)
   registered under a `DynamicChainRig`-only entity; after a real
   `EvaluatePoses()` call (Phase 1's baseline pass), `SkinAndUpload()` is
   asserted to run to completion with `EXPECT_NO_FATAL_FAILURE` — proving
   the method now actually reaches `SkinAndUploadOneEntity()` for this
   entity at all, which was structurally impossible before this phase.
2. `SkinAndUploadDoesNotCrashOrClobberTheAnimatedPoseForAnEntityWithBothAPlayingSkeletalAnimatorAndAnEnabledDynamicChainRig`
   — an entity carrying BOTH components at once (the case
   `processedThisFrame`'s de-duplication exists to guard). Proves the
   combined Source-1-then-Source-2 sequence never crashes and never
   disturbs `ResolvedAnimationPose` (`SkinAndUpload()` only ever reads it).
   **Adaptation note:** the phase plan's own Step 3.4 test #2 called for
   using `SkinningMode::GpuCompute` + a real `RegisterGpuSkinnedMesh()`
   call to observe `CollectModelsNeedingGpuSkinningThisFrame()` reporting
   exactly one entry — this requires a genuinely live `Renderer`/`VkDevice`
   (`GpuSkinningRigCache::Register()` creates real GPU buffers), which does
   not exist anywhere in this repository's Tier-1 test suite today (see
   `TESTING.md`'s accepted "Tier 2, no automated coverage yet" bucket for
   `Buffer`/`RenderTexture`/`Pipeline`/etc.). This test instead proves the
   same de-duplication safety property (no crash, no double-processing
   side effect observable without a live GPU device) using the default
   `CpuJobSystem` mode, which needs no GPU device at all — the actual
   `processedThisFrame` dedup logic exercised is identical code either way
   (it runs before the CPU/GPU mode branch inside `SkinAndUploadOneEntity()`
   is even reached a second time).
3. `SkinAndUploadSkipsADisabledDynamicChainRigEntirely` — `rig.enabled =
   false`, with a `ResolvedAnimationPose` manually pre-populated (as if left
   over from some other path) to prove `SkinAndUpload()` independently
   honors `enabled == false` rather than merely relying on the pose
   component's absence.
4. `SkinAndUploadSkipsADynamicChainRigEntityWithNoResolvedAnimationPoseYet`
   — `EvaluatePoses()` is deliberately never called at all; asserts
   `EXPECT_NO_FATAL_FAILURE`, mirroring the existing
   `SkinAndUploadSkipsAnEntityWithNoResolvedAnimationPoseYet` test's own
   convention for Source 1, now proven for Source 2 too.

Registered in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list (with a
matching descriptive comment block, following this file's own existing
per-entry documentation convention), immediately after
`Game/Animation/AnimationSystemEvaluatePosesTests.cpp`.

## Verification

- **Fast compile check only** (per this task's own workflow rules — no full
  build/regression yet):
  - `cmake --build build --target gte_core` — succeeded, 0 errors (rebuilt
    `AnimationSystem.cpp.obj` plus everything depending on it).
  - `cmake -S . -B build` (reconfigure, needed since `tests/CMakeLists.txt`'s
    source list changed) — succeeded (the one `ktx` git-describe warning
    printed is pre-existing/unrelated, not a new failure).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded, 0
    errors.
- As an extra correctness check beyond a bare compile (not a full regression
  run), the newly-added tests plus every directly-related pre-existing suite
  were executed via `--gtest_filter`:
  - `AnimationSystemSkinAndUploadPhysicsOnlyTest.*` (4 new tests) — all
    passed.
  - `AnimationSystemEvaluatePosesTest.*` (8 tests, Phase 1's own suite) —
    all passed, unchanged.
  - `GameLoopPhysicsWithoutAnimationTests.*` (1 test, Phase 1's own
    end-to-end T-pose-sags-under-gravity proof) — passed, unchanged.
  - `PhysicsSystemTests.*` (6 tests) — all passed, unchanged, confirming
    this phase's `AnimationSystem.cpp` changes didn't regress
    `PhysicsSystem`'s own already-passing suite.
  - Total: 19/19 passed.
- The full test suite (`ctest`) was **not** run, per this task's explicit
  "no full build/regression yet" instruction — reserved for a later phase in
  this campaign (or an explicit full-build instruction).

## What this unblocks

A `DynamicChainRig`-only entity's physics-adjusted pose (Phase 1's baseline,
optionally further adjusted by `PhysicsSystem::Update()` once Phase 3 lands)
is now actually re-skinned (CPU mode) or has its bone matrices re-uploaded
for GPU compute dispatch (GPU mode), and its GPU mesh vertex buffer is kept
current every frame — closing the last gap between "physics computes a pose"
and "that pose is visible on screen" for a model that has never had
`Game::PlayAnimationOnEntity()` called on it. This directly unblocks Phase 3
(world-space root-motion-aware chain simulation), the next task in this
campaign's strict phase order.

## Scope discipline (confirmed)

- `SkinAndUploadOneEntity()`'s extracted body is byte-for-byte the same
  logic the old inline loop ran — no behavior change inside it at all.
- No restructuring of `SkinningBatchContext`/`RunSkinningBatch`/
  `PackUntexturedBatchContext`/`PackTexturedBatchContext`/`Jobs::Dispatch()`
  internals.
- No special-casing or disabling of GPU-compute skinning mode for
  physics-only entities — both modes reach the same shared
  `SkinAndUploadOneEntity()` body through the exact same widened iteration.
- `src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()` call site
  and `CollectModelsNeedingGpuSkinningThisFrame()`'s own signature/logic
  were not touched — both already work purely off
  `m_gpuModelsNeedingDispatchThisFrame`, now populated through the exact
  same existing code path, just from a wider set of callers.
- The strict "one model at a time, never interleaved" sequencing rule for
  `SkinAndUpload()`'s outer processing was preserved and explicitly
  generalized in both the code comment and this phase's own tests — no
  entity's `Dispatch()`/`WaitForJobs()` bracket is ever started before the
  previous one (from either source) has fully completed.

## Next step

Proceed to `PHASE3_WORLD_SPACE_ROOT_MOTION_AWARE_CHAIN_SIMULATION.md`.
