# PHASE2 Completion Report — Data Model: From "One Sphere" to "A Shared, Mixed-Shape Collider List"

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase file executed: `PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md`
Status: **COMPLETE** — compiles cleanly (both `gte_core` and
`GreatTamanaEngineTests`), all directly-relevant tests pass (46/46 new/updated
Physics tests, 92/92 across the wider Physics + Game/Physics + Animation
regression slice run as an extra sanity check), no regressions observed.

---

## What was done

Implemented `PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md` exactly as
specified (Steps 3.1–3.7):

1. **`src/Physics/DynamicChainDefinition.h` (3.1)** — removed the old
   `hasHeadCollider` / `headColliderBoneIndex` / `headColliderRadius` trio and
   its doc comment, replaced with a single `bool collisionEnabled = false;`
   field plus the full doc-comment block from the strategy document
   (verbatim), explaining the new "shared, model-wide collider list" design.
2. **`src/Physics/ModelColliderDefinition.h` (3.2, new file)** — the
   `ModelColliderDefinition` struct (`boneIndex`, `shape`, `shapeSize`,
   `localOffsetPosition`, `localOffsetRotation`) exactly as specified, living
   in Physics/'s "data-driven" tier (references `Assets/PhysicsData.h`).
3. **`src/Physics/DynamicChainSolver.h`/`.cpp` (3.3/3.4)** — `#include
   "Collider.h"` replaces `#include "SphereCollider.h"` (unused now);
   `StepDynamicChain()`'s signature changed from `const SphereCollider*
   collider = nullptr` to `const std::vector<Collider>& colliders = {}`; step
   5's body now reads `if (definition.collisionEnabled) { for each particle:
   for each collider: SolveCollision(...) }`. Doc comments (parameter
   description + step 5 + step list) updated to match the new contract.
4. **`src/Physics/DynamicChainDetection.cpp` (3.5)** — deleted the old
   fake single-sphere heuristic (`chain.hasHeadCollider = false;
   chain.headColliderBoneIndex = ...; chain.headColliderRadius = ...;`) from
   Step G; `collisionEnabled`'s own default member initializer already
   handles seeding, so nothing replaces those 4 lines. Step G's own top
   comment updated to stop mentioning "head-collider defaults" and instead
   point at `DetectModelColliders()` (PHASE3).
5. **`tests/Physics/DynamicChainSolverTests.cpp` (3.6)** — added `#include
   "Physics/Collider.h"` (plus `<algorithm>` for a new test's own
   `std::min`/`std::max`/`std::abs` use); renamed/rewrote test (e)
   (`HeadColliderKeepsJointsOffItsSurfaceWhenChainFallsIntoIt` →
   `EnabledCollisionKeepsJointsOffEveryColliderSurfaceWhenChainFallsIntoIt`)
   and test (f) (`ColliderIsIgnoredWhenHasHeadColliderIsFalse` →
   `CollidersAreIgnoredWhenCollisionEnabledIsFalse`) to use the new
   `collisionEnabled` + `std::vector<Collider>` API; added one brand-new test,
   `MultipleCollidersOfDifferentShapesAreAllRespectedSimultaneously`, proving
   the solver's inner `for (const Collider& collider : colliders)` loop
   genuinely visits every entry of a mixed Sphere+Box+Capsule list (not just
   `colliders[0]`) by placing each shape so it is the *only* thing blocking a
   different one of the chain's three joints. Every other pre-existing test in
   this file, and in the separate `DynamicChainSolverIdleSettlingTests.cpp`,
   needed **no change** (the new `colliders` parameter defaults to `{}`,
   exactly preserving every old call site).
6. **`src/Game/Physics/PhysicsSystem.h` (3.7)** — retired the stale
   `Update()` doc-comment mention of the now-removed
   `DynamicChainDefinition::hasHeadCollider`, replacing it with a description
   of the new shared, model-wide collider list per the strategy document's
   exact replacement text.
7. **`CMakeLists.txt`** — added `src/Physics/ModelColliderDefinition.h`
   immediately after `src/Physics/DynamicChainDefinition.cpp` in
   `add_library(gte_core STATIC ...)`'s source list.

### Additional work beyond PHASE2's own formal file list — keeping the whole engine building

`PHASE0_MASTER_STRATEGY.md` explicitly requires never leaving the build
broken between phases ("do not leave `PhysicsSystem.cpp` broken between
phases"), while also formally assigning the *real* fixes for three affected
call sites to **later** phases:

- `src/Game/Physics/PhysicsSystem.cpp` — real registration/resolution wiring
  is PHASE3 (`RegisterDynamicChains()`) / PHASE4 (world-space resolution).
  PHASE3's own Step 3.6 already documents the exact minimal placeholder fix
  needed to keep the build green in the meantime.
- `src/Editor/Panels/InspectorPanel.cpp` and `src/Editor/BoneViewerWindow.cpp`
  — the real Inspector/Bone-Viewer UI redesign (collider-count readout, etc.)
  is PHASE5's job (`PHASE0`'s own Revision Notes, finding #1, explicitly calls
  out `BoneViewerWindow.cpp` as a file PHASE5 must still fix).

Since none of PHASE2's own edit list touches these three files, `gte_core`
would **not** compile after PHASE2's changes alone (they still reference the
now-deleted `hasHeadCollider`/`headColliderBoneIndex`/`headColliderRadius`
fields and the old `StepDynamicChain(..., const SphereCollider*)` overload).
To honor the master strategy's own "never leave the build broken" rule without
pre-empting PHASE3/PHASE5's real feature work, this session applied ONLY the
minimal, clearly-labeled transitional compile fixes:

- **`PhysicsSystem.cpp`** — applied PHASE3's own pre-written Step 3.6
  placeholder verbatim: the old `SphereCollider`/`hasHeadCollider` block
  inside `StepDynamicChainRange()` is now `const std::vector<Collider>
  colliders;` (always empty), and the `StepDynamicChain(...)` call passes
  `colliders` instead of `hasCollider ? &collider : nullptr`. Behaviorally
  identical to "collision fully disabled" for every chain today (since
  `collisionEnabled` defaults to `false` and this list is always empty until
  PHASE4 lands). `#include "../../Physics/SphereCollider.h"` replaced with
  `#include "../../Physics/Collider.h"`.
- **`InspectorPanel.cpp`** (2 call sites) — the old "Head Collider" checkbox +
  bone-index/radius drag controls (which no longer have backing fields) were
  replaced with a single `ImGui::Checkbox("Collision Enabled",
  &chain.collisionEnabled);`, each clearly commented as a minimal transitional
  fix pending PHASE5's real collider-count-readout redesign.
- **`BoneViewerWindow.cpp`** (2 call sites) — the Verlet tree pane's
  `"Head Collider: r=%.3f"` text row now reads `chain.collisionEnabled` and
  shows a generic "Collision: enabled" message instead; the 3D-viewport
  head-collider sphere wireframe block (which depended on
  `headColliderBoneIndex`/`headColliderRadius`) was removed outright with a
  comment explaining why and pointing at PHASE5 for the real shared-list
  overlay.

None of these three files' real, permanently-intended behavior (PHASE3's
`DetectModelColliders()`/registration, PHASE4's per-frame world-space
resolution, PHASE5's Inspector/Bone-Viewer redesign) was implemented here —
only the minimal edits needed so the field/signature renames didn't leave
dangling references. This is consistent with, not a deviation from, the
master strategy's own explicit instructions for this exact situation.

---

## Verification

Per this task's workflow rules (no full build/regression yet), a compile
check was run, plus a full run of the affected test suites as an extra
integrity check on the additional file touches described above:

1. `cmake -S . -B build` — reconfigured successfully.
2. `cmake --build build --target gte_core` — **zero warnings/errors**,
   including `DynamicChainDefinition.cpp`, `DynamicChainSolver.cpp`,
   `DynamicChainDetection.cpp`, `Game/Physics/PhysicsSystem.cpp`,
   `Editor/Panels/InspectorPanel.cpp`, `Editor/BoneViewerWindow.cpp`.
3. `cmake --build build --target GreatTamanaEngineTests` — **zero
   warnings/errors**, including the updated
   `tests/Physics/DynamicChainSolverTests.cpp`.
4. `GreatTamanaEngineTests.exe
   --gtest_filter=DynamicChainSolverTests.*:DynamicChainDefinitionTests.*:DynamicChainDetectionTests.*:BoxColliderTests.*:CapsuleColliderTests.*:ColliderTests.*`
   — **46/46 passed**, including the 2 rewritten tests and the 1 new
   multi-shape-collider test.
5. As a broader sanity check (not the full suite — that is PHASE6's job),
   `--gtest_filter=PhysicsSystem*:*DynamicChain*:*Collider*` — **92/92
   passed**, covering every Physics/, Game/Physics/, and
   Animation-touching-physics test in the suite, confirming the
   `PhysicsSystem.cpp` placeholder fix introduced no regression anywhere
   physics-adjacent (freeze/culprit-F, parallel-vs-serial byte-identity,
   world-space root motion, anchor rigidity, animation+physics interplay).

No other test suites were run (per the "no full build/regression yet"
instruction) — the full `ctest` run is deliberately deferred to PHASE6 as
planned by the master strategy.

---

## Notes for the next phase (PHASE3)

- `PhysicsSystem.cpp`'s `RegisterDynamicChains()` does **not** yet call
  `DetectModelColliders()` — nothing populates a real collider list for any
  model yet. `StepDynamicChainRange()`'s `colliders` local is a hardcoded
  empty placeholder (see "Additional work" above) — PHASE3's own Step 3.6 is
  now DONE (folded into this session for build-health reasons), so PHASE3
  should proceed directly to Steps 3.1–3.5, 3.7, 3.8 (the actual
  `ModelColliderDetection.h`/`.cpp`, `DynamicChainRigCache::ModelEntry`'s new
  `colliders` field, `RegisterDynamicChains()`'s real
  `DetectModelColliders()` call + `entry.colliders = std::move(colliders);`,
  the new test file, and its `CMakeLists.txt`/`tests/CMakeLists.txt`
  registration) — please re-check `PhysicsSystem.cpp`'s current state before
  editing, since its Step 3.6 placeholder is already in place, not still
  pending.
- `InspectorPanel.cpp`/`BoneViewerWindow.cpp` currently show only a minimal
  `collisionEnabled` checkbox / generic "Collision: enabled" text — PHASE5
  still owns the real collider-count readout and any richer
  visualization/wireframe of the shared model-wide list once PHASE3/PHASE4
  make that list real. Please re-read this report's "Additional work"
  section before starting PHASE5's own edits, since the two call sites this
  document originally described (with the old bone-index/radius controls)
  no longer exist in that exact form.
- No deviations from PHASE2's own strategy document were made for anything
  within its formal scope; the only extra work was the three
  build-health-motivated placeholder fixes described above, each clearly
  commented in-code as transitional and attributed to the phase that owns
  the real fix.
