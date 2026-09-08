# PHASE2 Completion Report — Runtime Application of Saved Joint Physics Overrides on Instantiation

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
phase: `PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`
status: **DONE** — compiles cleanly (`gte_core` + `GreatTamanaEngineTests`), ready for PHASE3.

## What was implemented

Followed the phase strategy document (`PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md`)
step by step:

1. **`src/Physics/JointPhysicsOverrideApplication.h`** (new file) — declares
   `ApplyJointPhysicsOverrides(std::vector<DynamicChainDefinition>& chains,
   const std::vector<JointPhysicsOverride>& overrides)`, a pure/free function
   (no ECS/Editor/GPU/file I/O dependency) with the exact doc comment the
   strategy document specifies, including the "last one wins on a duplicate
   boneIndex" and "silently ignore a non-matching boneIndex" contracts.
2. **`src/Physics/JointPhysicsOverrideApplication.cpp`** (new file) —
   implementation exactly as specified: builds a `boneIndex -> override`
   lookup map (skipped entirely when `overrides` is empty, the overwhelmingly
   common case), then walks every chain's `jointBoneIndices` and rewrites the
   matching joint's `damping`/`stiffness`/`mass` in place, leaving
   `group`/`collisionMask`/`collisionRadius` and any chain-level field
   (`collisionEnabled`, `gravityScale`, ...) untouched.
3. **`src/Game/Physics/PhysicsSystem.cpp`** — added
   `#include "../../Physics/JointPhysicsOverrideApplication.h"` alongside the
   existing `Physics/` includes, and inserted exactly one call,
   `ApplyJointPhysicsOverrides(detection.chains, data.jointPhysicsOverrides);`,
   in `RegisterDynamicChains()` strictly AFTER `DetectDynamicChains()` returns
   and strictly BEFORE `DetectModelColliders()`/`m_rigCache.Register()` run —
   every other line in that method is unchanged. The existing
   `#ifndef NDEBUG` disjoint-`jointBoneIndices` assert still sits after this
   call and remains valid unmodified, since `ApplyJointPhysicsOverrides()`
   never adds/removes/reorders a joint, only rewrites three floats on
   existing entries in place.

## Tests added/extended

- **New file — `tests/Physics/JointPhysicsOverrideApplicationTests.cpp`**
  (Tier 1, pure, mirrors `DynamicChainDefinitionTests.cpp`'s hand-built-fixture
  style), covering exactly the five cases the strategy document specifies:
  - `MatchingOverrideRewritesDampingStiffnessMass`
  - `OverrideWithNoMatchingBoneIndexIsSilentlyIgnored`
  - `EmptyOverrideListLeavesEveryChainUnchanged`
  - `DuplicateBoneIndexOverridesLastEntryWins`
  - `AppliesAcrossMultipleChainsIndependently`
- **`tests/Game/Physics/PhysicsSystemTests.cpp`** — added
  `RegisterDynamicChainsAppliesSavedJointOverridesOnTopOfDetectionDefaults`,
  an integration-shaped test proving the WIRING (not just the pure function)
  works: reuses the exact same synthetic 4-bone/2-joint rig fixture as the
  existing `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity`
  test (root -> chainRoot(Static anchor) -> joint1(Dynamic) -> joint2(Dynamic)),
  attaches a `JointPhysicsOverride` naming joint2's bone index (3) with custom
  damping/stiffness/mass, calls `PhysicsSystem::RegisterDynamicChains()`, and
  asserts the resulting `DynamicChainRigCache::ModelEntry`'s matching joint's
  `DynamicJointSettings` reflect the saved values, not the detection defaults.
  This reused the file's own existing `PhysicsSystem::GetDynamicChainRigCache()`
  accessor (already public, no new API needed).

## Build registration

The strategy document's own PHASE0 master plan named PHASE5 as "the only
phase that touches build files." However, this phase introduces two genuinely
new source files (`JointPhysicsOverrideApplication.h`/`.cpp`) that
`PhysicsSystem.cpp` now depends on for a real, non-inline symbol
(`ApplyJointPhysicsOverrides`) — leaving them unregistered in `CMakeLists.txt`
would make `gte_core`/`GreatTamanaEngineTests` fail to link, directly
contradicting this task's own "Fast Compile Check" workflow requirement for
*this* phase. Registered both new files now (not deferred to PHASE5):

- `CMakeLists.txt` — added `src/Physics/JointPhysicsOverrideApplication.h`/
  `.cpp` to `gte_core`'s source list, right after `src/Physics/Collider.cpp`.
- `tests/CMakeLists.txt` — added
  `Physics/JointPhysicsOverrideApplicationTests.cpp` to
  `GTE_TEST_SOURCES`, right after `Physics/DynamicChainDetectionJointRadiusTests.cpp`.

This is a deliberate, minimal deviation from the strategy document's original
build-registration sequencing (a documentation-vs-practical-compile-check
mismatch the v2 strategy review didn't flag), not a scope change — PHASE5
remains responsible for registering PHASE3/PHASE4's own new files (the
`MeshAssetGpuCatalog`/`DynamicChainPhysicsPersistence`/Editor-facing pieces),
and for the full end-to-end round-trip regression test.

## Compile verification

Ran a fast, targeted build (not a full project build/regression run, per this
task's instructions):

```
cmake --build build --target gte_core                 -> SUCCESS (0 errors)
cmake --build build --target GreatTamanaEngineTests    -> SUCCESS (0 errors)
```

Both the engine core static library and the full test binary link cleanly.
Test *execution* (`ctest`) was intentionally not run this phase, matching the
task's "no full build/regression yet" instruction (full regression is
explicitly deferred to a later phase per this campaign's own workflow rules).

## What this phase deliberately does NOT do (unchanged from the strategy doc)

- Does not add any way to actually PRODUCE a non-empty
  `jointPhysicsOverrides` list from the Editor yet (PHASE4) — this phase's own
  tests build one by hand.
- Does not change `DetectDynamicChains()` itself in any way — overrides are
  applied strictly AFTER it runs, as a separate, later, independently-testable
  step.
- Does not touch `Panels/InspectorPanel.cpp` or anything under `src/Editor/`.
- Does not address the "already-cached, second spawn in the same session sees
  stale data" gap (`PHASE0_MASTER_STRATEGY.md`, Step 2.5) — that is entirely a
  `MeshAssetGpuCatalog` cache-freshness problem, upstream of this phase's own
  function, and is PHASE3's job.

## Next step

**PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md** — give
`MeshAssetGpuCatalog` a new `RefreshCachedJointPhysicsOverridesFromDisk(absoluteGtaPath)`
method that re-reads ONLY the trailing `jointPhysicsOverrides` section of an
already-cached path's metadata and updates the cached `SkinnedMeshData` in
place, threaded through `MeshInstantiationSystem`/`Game` the same way
`TryGetSkinnedMeshData()`/`GetPhysicsSystem()` already are.
