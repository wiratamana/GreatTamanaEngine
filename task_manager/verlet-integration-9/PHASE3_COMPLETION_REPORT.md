# PHASE3 Completion Report — Auto-Detection of Model Colliders From Real PMX Rigid Bodies

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase file executed: `PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md`
Status: **COMPLETE** — compiles cleanly (both `gte_core` and
`GreatTamanaEngineTests`), all directly-relevant tests pass (99/99 across the
Physics/, Game/Physics/, and Animation-touching-physics regression slice run
as an extra sanity check), no regressions observed.

---

## What was done

Implemented `PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md`
exactly as specified (Steps 3.1–3.8). PHASE2's own completion report had
already noted that its own Step 3.6 (the `PhysicsSystem.cpp`
`StepDynamicChainRange()` minimal placeholder keeping the build green) was
folded into the PHASE2 session for build-health reasons — this session
re-verified that placeholder is exactly the one described by PHASE3's own
Step 3.6 and proceeded directly to the real detection/registration work
(Steps 3.1–3.5, 3.7, 3.8):

1. **`src/Physics/ModelColliderDetection.h` (3.1, new file)** — declares
   `DetectModelColliders(const SkeletonData&, const PhysicsData*)`, returning
   `std::vector<ModelColliderDefinition>`, exactly per the strategy document's
   doc comment (verbatim).
2. **`src/Physics/ModelColliderDetection.cpp` (3.2, new file)** — implements
   `DetectModelColliders()` exactly as specified: iterates every
   `PhysicsData::rigidBodies` entry, keeps only `RigidBodyMotionType::Static`
   bodies attached to a valid in-range bone with a non-degenerate shape
   (`IsDegenerateColliderShape()` — Sphere/Capsule: radius `<= kEpsilon`; Box:
   ALL three half-extents `<= kEpsilon`; a Capsule's non-positive `height` is
   explicitly still valid, matching `RigidBodyWireframe.h`'s own documented
   rule), computes each survivor's bind-pose-relative local offset
   (`bindBoneWorldInverse * rigidBodyBindWorld`, using the SAME
   `Quat::FromEulerDegrees(RadToDeg(x), RadToDeg(y), RadToDeg(z))` PMX Euler
   convention as `RigidBodyWireframe.cpp`'s own `RotationFromPmxEuler()`,
   redeclared locally per this campaign's own established
   redeclare-not-share convention), and returns the result
   `std::stable_sort`-ed by ascending `boneIndex` for full determinism. Both
   `RotationFromPmxEuler()` and `IsDegenerateColliderShape()` live in an
   anonymous namespace, matching the strategy document's exact code.
3. **`CMakeLists.txt`** — `src/Physics/ModelColliderDetection.h`/`.cpp` added
   to `add_library(gte_core STATIC ...)`'s source list, immediately after the
   existing `src/Physics/ModelColliderDefinition.h` line.
4. **`src/Game/Physics/DynamicChainRigCache.h` (3.4)** — added
   `#include "../../Physics/ModelColliderDefinition.h"`, and `ModelEntry`
   gained a new `std::vector<ModelColliderDefinition> colliders;` field
   (with the exact doc comment from the strategy document) immediately after
   the existing `diagnostics` field.
5. **`src/Game/Physics/PhysicsSystem.cpp` (3.5)** — added
   `#include "../../Physics/ModelColliderDetection.h"`. Inside
   `RegisterDynamicChains()`, immediately after the existing
   `DetectDynamicChains(...)` call, added the new, independent
   `DetectModelColliders(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr)`
   call (with the exact doc comment from the strategy document), and added
   `entry.colliders = std::move(colliders);` alongside the existing
   `entry.chains`/`entry.skeleton`/`entry.diagnostics` assignments onto the
   freshly-built `DynamicChainRigCache::ModelEntry`.
6. **`tests/Physics/ModelColliderDetectionTests.cpp` (3.7, new file)** —
   7 tests, mirroring `tests/Physics/DynamicChainDetectionTests.cpp`'s own
   hand-built-fixture style, covering every required case from the strategy
   document:
   - `StaticSphereBodyAtBoneOriginProducesAColliderWithZeroLocalOffset`
   - `StaticBoxBodyOffsetFromItsBoneProducesTheCorrectLocalOffset` (a genuine
     round-trip proof: composing the bone's bind-pose world matrix with the
     produced local offset reproduces exactly the rigid body's own authored
     bind-pose transform, both position and rotation)
   - `DynamicAndDynamicAndBoneMergeBodiesAreNeverIncluded`
   - `UnattachedOrOutOfRangeBoneIndexIsExcluded`
   - `DegenerateShapesOfEachKindAreExcluded` (one sub-case per shape, plus
     confirming a Capsule with a non-positive height but positive radius IS
     still included)
   - `NullPhysicsDataDetectsNothing`
   - `ResultIsSortedByAscendingBoneIndex`
7. **`tests/CMakeLists.txt` (3.8)** —
   `Physics/ModelColliderDetectionTests.cpp` added to `GTE_TEST_SOURCES`,
   immediately after the existing `Physics/ColliderTests.cpp` line.

Nothing outside this file set was touched. `PhysicsSystem.cpp`'s
`StepDynamicChainRange()` placeholder (still resolving an always-empty
`const std::vector<Collider> colliders;` per PHASE2's own already-applied
Step 3.6 fix) is intentionally left exactly as-is — PHASE4 is the phase that
replaces it with the real per-frame world-space resolution of
`model->colliders` into that call site. This phase's own scope ends at
"every model's collider list is correctly detected and cached" per the
strategy document's own closing note.

---

## Verification

Per this task's workflow rules (no full build/regression yet — that is
PHASE6's job), a fast, targeted compile check plus a wider sanity-check test
run were performed:

1. `cmake -S . -B build` — reconfigured successfully (no re-download, no
   `CMakeLists.txt` syntax errors).
2. `cmake --build build --target gte_core` — **zero warnings/errors**,
   including the two new files (`ModelColliderDetection.h`/`.cpp`) and every
   edited file (`DynamicChainRigCache.h`, `PhysicsSystem.cpp`).
3. `cmake --build build --target GreatTamanaEngineTests` — **zero
   warnings/errors**, including the new
   `tests/Physics/ModelColliderDetectionTests.cpp`.
4. `GreatTamanaEngineTests.exe
   --gtest_filter=ModelColliderDetectionTests.*:DynamicChainRigCacheTests.*:PhysicsSystem*:*DynamicChain*:*Collider*`
   — **99/99 passed**, including all 7 new `ModelColliderDetectionTests`, and
   confirming zero regressions in `DynamicChainRigCacheTests` (the new
   `colliders` field defaults to empty and does not disturb the existing
   `chains`/`skeleton`/`diagnostics` round-trip assertions), every
   `PhysicsSystem*`/`*DynamicChain*` test, and every pre-existing
   `*Collider*` test (Sphere/Box/Capsule/unified `Collider`).

No other test suites were run (per the "no full build/regression yet"
instruction) — the full `ctest` run is deliberately deferred to PHASE6 as
planned by the master strategy.

---

## Notes for the next phase (PHASE4)

- Every model with real PMX Static rigid-body data now has a correctly
  detected, cached `ModelEntry::colliders` list (bind-pose-relative, LOCAL
  form) the moment `RegisterDynamicChains()` runs — but nothing resolves it
  to world space per frame yet. `PhysicsSystem.cpp`'s
  `StepDynamicChainRange()` still calls `StepDynamicChain(...)` with a
  hardcoded, always-empty `const std::vector<Collider> colliders;` local (the
  PHASE2/PHASE3-Step-3.6 placeholder) — so no chain in the running
  game/Editor collides against anything new yet, exactly as the strategy
  document's own closing note says.
- PHASE4 should proceed exactly as documented in
  `PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md`: resolve
  every `model->colliders` entry's current WORLD position AND orientation
  once per entity per frame (reusing `entityWorldMatrix`/the
  `Quat::FromMat4()` bone-rotation-extraction precedent already established
  in `BoneChainPhysicsResolver.cpp`), replace the placeholder
  `const std::vector<Collider> colliders;` local with the real resolved list,
  and thread it through `DynamicChainBatchContext`'s new field (PHASE0's own
  Revision Notes v2, finding #5, already pins the exact field
  position/initializer text to avoid a positional aggregate-initializer
  mismatch).
- No deviations from PHASE3's own strategy document were made; no
  ambiguities were encountered that required a judgment call beyond what was
  already fully specified. `PhysicsSystem.cpp`'s Step 3.6 placeholder was
  already in place from the PHASE2 session exactly as that session's own
  completion report described — this session did not need to (and did not)
  touch it.
