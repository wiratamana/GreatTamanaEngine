# PHASE0_MASTER_STRATEGY — Multi-Shape Collision for the Verlet Dynamic-Chain Solver

**v2** — re-audited directly against the real, current source tree on
2026-09-08 (second iteration of this campaign's own planning documents, per
this campaign's own two-pass "plan, then re-audit the plan" discipline). See
"Revision Notes (v2)" below Step 2 for exactly what changed and why. PHASE1
and PHASE3 were re-verified line-by-line against the real codebase and found
correct as originally written — they are UNCHANGED from v1. PHASE0 (this
file), PHASE2, PHASE4, PHASE5, and PHASE6 all received concrete, targeted
fixes (never a rewrite-for-its-own-sake) — see the Revision Notes.

campaign folder: `task_manager/verlet-integration-9/`
orchestrates: PHASE1, PHASE2, PHASE3, PHASE4, PHASE5, PHASE6 (all in this same folder)

This document is the **orchestrator**. It does not itself contain the
implementation — each child phase file is a fully self-contained,
implementable work chunk. An AI programmer should read this file first for
the big picture, then execute PHASE1 → PHASE6 **in strict numeric order**
(each phase's code depends on the previous phase's code existing and
compiling).

---

## Step 1 — The Goal (Where are we going?)

Today, `SolveSphereCollision()` (`src/Physics/SphereCollider.h`) is the
**only** collision-response primitive the Verlet dynamic-chain solver
(`src/Physics/DynamicChainSolver.h`) knows how to run. A chain may optionally
push its own joints off ONE hand-picked sphere (`hasHeadCollider` /
`headColliderBoneIndex` / `headColliderRadius` on `DynamicChainDefinition`).

The imported MMD/PMX model (e.g. "Furina") already ships **three** real
rigid-body collider shapes per `Assets/PhysicsData.h`'s own
`RigidBodyShape` enum: `Sphere`, `Box`, `Capsule` — visible today in the
Editor's Bone Viewer / Inspector (`RigidBodyShapeLabel()`,
`RigidBodyWireframe.h`, which already draws all three shapes correctly) but
**never actually used for collision** — only ever displayed.

**The goal of this campaign:** every Verlet-simulated joint particle in
every dynamic chain must be able to collide correctly against **any** of the
three PMX collider shapes (Sphere, Box, Capsule) that the model's own
`PhysicsData::rigidBodies` (Static bodies) describe — automatically, with no
manual per-bone authoring required — while every one of those three shapes
must itself be a fully correct, independent collision primitive (a particle
resting against a Box must be pushed out along the box's own nearest face;
against a Capsule, along the nearest point of its own line segment; against
a Sphere, exactly as today).

"All these shapes can collide against each shape" (the user's own words) is
satisfied because **every** particle of **every** opted-in chain is tested
against **every** detected collider, regardless of that collider's own
shape — Sphere, Box, and Capsule are all equally first-class from the
solver's point of view. (A Verlet particle itself remains a zero-radius
point, matching this engine's existing "joint = point mass" model — there is
no chain-vs-chain or collider-vs-collider physical body simulation in scope
here, only "does a physics point currently sit inside a collision volume".)

---

## Step 2 — The Situation (Where are we now?)

Investigated and confirmed by direct source inspection (2026-09-08):

1. `src/Physics/SphereCollider.h/.cpp` — the only shape with real collision
   math (`SolveSphereCollision()`: push a penetrating particle straight out
   to the sphere's surface along `center → particle`). No Box/Capsule
   equivalent exists anywhere in the engine.
2. `src/Physics/DynamicChainDefinition.h` — a chain carries **one**
   optional collider: `bool hasHeadCollider`, `std::int32_t
   headColliderBoneIndex`, `float headColliderRadius` (sphere-only, by
   construction — there is no field for a shape or an orientation at all).
3. `src/Physics/DynamicChainSolver.h/.cpp` (`StepDynamicChain()`) — step 5
   ("Collision") solves exactly one `const SphereCollider*` against every
   joint particle, once.
4. `src/Game/Physics/PhysicsSystem.cpp` (`StepDynamicChainRange()`) —
   resolves that single sphere's WORLD-space `center` every frame from
   `chain.headColliderBoneIndex`'s own current animated bone position;
   `radius` is a flat, hand-authored number.
5. `src/Physics/DynamicChainDetection.cpp` (Step G) — the "head collider" is
   **never actually derived from any real PMX rigid body**: it is a crude
   heuristic (`headColliderBoneIndex = chain.rootBoneIndex`, `radius =`
   average segment length / 2) and is left **disabled by default**
   (`hasHeadCollider = false`) — a human must manually opt in via the
   Editor Inspector and hand-tune a bone index + radius.
6. `Assets/PhysicsData.h::RigidBody` **already** carries everything needed
   to describe a real Box/Capsule/Sphere collider precisely as authored in
   the PMX file: `shape` (`RigidBodyShape`), `shapeSize` (per-shape field
   reuse — Sphere: `x` = radius; Box: `xyz` = half-extents; Capsule: `x` =
   radius, `y` = full height), `translate`/`rotateRadians` (an **absolute
   model-space bind-pose transform**, not bone-relative — confirmed against
   `src/Editor/RigidBodyWireframe.cpp`'s own working PMX-accurate
   visualization code), `boneIndex` (which bone this body follows), and
   `motionType` (`Static` = "strictly follows the bone" — i.e. a pure
   kinematic obstacle, never itself simulated — vs. `Dynamic`/
   `DynamicAndBoneMerge`, which are the chain's own simulated joints,
   already fully handled by the existing chain-detection algorithm).
7. `src/Editor/RigidBodyWireframe.cpp` already contains fully-correct,
   tested geometry generation for all three shapes (used only for
   Bone-Viewer visualization) — its PMX Euler-rotation convention
   (`Quat::FromEulerDegrees(RadToDeg(x), RadToDeg(y), RadToDeg(z))`, verified
   against `saba::MMDPhysics.cpp`'s own `ry * rx * rz`) is the SAME
   convention this campaign must reuse for real collision math, so a
   Box/Capsule's on-screen wireframe and its actual collision volume are
   always visually consistent.
8. Build system: this project has **no globbing** — every `.h`/`.cpp` file
   is listed explicitly in the root `CMakeLists.txt` (`add_library(gte_core
   STATIC ...)`) and every test file is listed explicitly in
   `tests/CMakeLists.txt` (`GTE_TEST_SOURCES`). Every phase below that adds a
   new file **must** also add it to both lists, or the new code will never
   be compiled/linked/run.

**Net conclusion:** this is not a small bugfix, it is a genuine, multi-part
feature: (a) two brand-new shape-collision primitives, (b) a data-model
change from "one sphere" to "a list of mixed-shape colliders", (c) wiring
those colliders automatically from the model's own real PMX rigid-body data
(replacing the current fake heuristic), (d) correct per-frame world-space
resolution including ORIENTATION (needed for Box/Capsule, unlike the
rotation-invariant Sphere), and (e) an Editor UI update. Six phases, in
strict dependency order.

### Revision Notes (v2) — gaps found and fixed after re-auditing against the real codebase

This campaign's v1 documents (PHASE0-PHASE6) were re-read in full and
cross-checked, line by line, against the actual current state of every file
they reference (`src/Physics/*`, `src/Game/Physics/*`, `src/Editor/*`,
`src/Math/*`, `src/Assets/PhysicsData.h`, both `CMakeLists.txt` files, and
every test file quoted). The overwhelming majority of v1 was confirmed
**byte-for-byte accurate** — every quoted code excerpt, every claimed
function signature, every claimed line-insertion anchor in both
`CMakeLists.txt` files matched the real source tree exactly. Five concrete
gaps were found and are now fixed in the affected child phase documents
(never silently — every fix below names exactly which phase file changed):

1. **[Critical — would have broken the build with no phase assigned to fix
   it] `src/Editor/BoneViewerWindow.cpp` was never in scope.** This file has
   TWO separate blocks that directly read
   `chain.hasHeadCollider`/`chain.headColliderBoneIndex`/
   `chain.headColliderRadius` (confirmed at its own current lines ~735-737,
   a text row in the Bone Viewer's Verlet tree pane, and ~1493-1513, the
   actual head-collider wireframe drawn in the 3D viewport overlay). PHASE2
   deletes all three of those fields. Neither v1's PHASE0 file manifest, nor
   PHASE5 (which only ever mentioned `InspectorPanel.cpp`), assigned anyone
   to fix this file — it would have failed to compile the moment PHASE2
   landed, with no phase in the whole campaign responsible for the fix
   (PHASE6's own final grep audit would have *found* the dangling
   references, but finding them mid-PHASE6 with no prescribed fix content
   is exactly the kind of "wasted phase" this campaign's own instructions
   say to avoid). **Fixed in `PHASE5_EDITOR_INSPECTOR_UI_UPDATE.md` v2**,
   which is now scoped to cover BOTH `InspectorPanel.cpp` (unchanged from
   v1) AND `BoneViewerWindow.cpp`/`BoneViewerWindow.h` (new), with a full,
   concrete replacement plan for both blocks.
2. **[Moderate — misleading test-authoring instruction] PHASE6's guidance to
   "reuse `MakeRigidBody()`/`MakeJoint()` ... do not reinvent it"
   undersold what actually needs reinventing.** The real
   `MakeRigidBody()` helper (private to
   `tests/Physics/DynamicChainDetectionTests.cpp`'s own anonymous namespace)
   only sets `boneIndex`/`motionType`/`mass`/`linearDamping` — it has no
   parameters for `shape`/`shapeSize`/`translate`/`rotateRadians` at all, so
   it cannot be used, as literally written, to build the Sphere/Box/Capsule
   Static bodies PHASE6's own new end-to-end test requires. **Fixed in
   `PHASE6_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md` v2**, which now
   spells out the exact, richer, LOCAL (this codebase's own established
   per-file-helper convention — see e.g. `RotationFromPmxEuler()` being
   independently redeclared in three different `.cpp` files rather than
   shared) helper signature the new test file must write for itself.
3. **[Minor — stale doc comment, not compile-breaking] `src/Game/Physics/
   PhysicsSystem.h`'s own `Update()` doc comment** still name-drops
   `DynamicChainDefinition::hasHeadCollider` after this campaign removes it.
   **Fixed as a new trivial step in `PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md`
   v2** (Step 3.7) — PHASE2 is the phase that actually deletes the field, so
   it is the natural, single place to also retire every doc-comment mention
   of it, rather than leaving a stray reference for PHASE6's audit to merely
   notice.
4. **[Worth-adding, for future maintainers/AI agents — not required for this
   campaign's own compile-and-pass success] `AGENTS.md`'s own cross-thread
   safety reference table** (the "Job System" section) has a row that
   explicitly names `SphereCollider` as one of the `src/Physics/*` files
   proven **JOB-SAFE** (pure functions, no shared mutable state, safe to run
   concurrently inside `StepDynamicChain()`'s own per-chain parallel
   `Dispatch()` path). This campaign's new `BoxCollider`/`CapsuleCollider`/
   `Collider` execute through that exact same call path
   (`SolveCollision()` is invoked from inside `StepDynamicChain()`, which
   PHASE4 confirms still runs inside `RunDynamicChainBatchJob()` job
   bodies) and are equally pure/stateless, but v1 never updated this table
   row — leaving a future contributor to either wrongly assume they're
   unsafe by omission, or wrongly assume safety without it ever having been
   audited/documented. **Fixed as a new step in
   `PHASE6_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md` v2** (this
   campaign's own final documentation-sweep phase is the correct place for
   it).
5. **[Robustness/QoL — ambiguous instruction that could cause an LLM
   programmer to silently mis-wire an aggregate initializer] PHASE4's
   instruction for adding a new field to `DynamicChainBatchContext`** said
   only "pass `&resolvedColliders` ... in whatever position the struct's
   field order places it — keep field order and initializer order in sync,"
   without stating an exact slot. `DynamicChainBatchContext context{ ... };`
   is a plain aggregate initializer — every field is positional; inserting
   the new field/argument at two DIFFERENT relative positions between the
   struct definition and the initializer call silently compiles into
   nonsense (wrong field gets the wrong value) rather than failing to build,
   which is exactly the dangerous kind of ambiguity a fully-detailed
   strategy document must not leave to chance. **Fixed in
   `PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md` v2**,
   which now pins the new field as the exact last member (immediately after
   `bool frozen;`) and spells out the complete, final struct body AND the
   complete, final aggregate-initializer call text verbatim, leaving zero
   room for a positional mismatch.

PHASE1 (`PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md`) and PHASE3
(`PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md`) were
independently re-verified in full (every quoted `Math/`, `Assets/`,
`Animation/` API call; every quoted existing-file excerpt) and found
completely correct as originally written — per this campaign's own "skip
the rewrite if it's already good enough" principle, **they are unchanged
from v1** and are not reproduced again here; read them as-is.

---

## Step 3 — The Plan (Phase Index)

Execute **in this exact order** — each phase's code will not compile without
the previous phase's code already in place.

| # | File | One-line summary | Depends on |
|---|------|-------------------|------------|
| 1 | `PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md` | New, pure, engine-data-free `BoxCollider`/`CapsuleCollider`/unified `Collider` primitives + their `Solve*Collision()` math + Tier-1 tests. Zero dependency on ECS/ Assets/ ECS/Editor. | none |
| 2 | `PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md` | Replace `DynamicChainDefinition`'s single `hasHeadCollider`/`headColliderBoneIndex`/`headColliderRadius` trio with one `bool collisionEnabled` flag; change `StepDynamicChain()`'s signature from a single `const SphereCollider*` to a shared `const std::vector<Collider>&`; new `ModelColliderDefinition` struct. (v2: also retires the one stale doc-comment reference in `Game/Physics/PhysicsSystem.h`.) | 1 |
| 3 | `PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md` | New `DetectModelColliders()` — replaces the old fake single-sphere heuristic with a real reader of every `RigidBodyMotionType::Static` PMX body (any of the 3 shapes), precomputing each one's bind-pose-relative offset from its tracked bone. Wired into `PhysicsSystem::RegisterDynamicChains()`. | 2 |
| 4 | `PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md` | `Game/Physics/PhysicsSystem.cpp` resolves every model collider's current WORLD position **and orientation** once per entity per frame (reusing the already-established `Quat::FromMat4()` bone-rotation-extraction precedent from `BoneChainPhysicsResolver.cpp`), and feeds the shared list into every opted-in chain's `StepDynamicChain()` call. (v2: pins the exact, unambiguous `DynamicChainBatchContext` field/initializer position.) | 3 |
| 5 | `PHASE5_EDITOR_INSPECTOR_UI_UPDATE.md` | Update the two Inspector call sites (`ModelPartInspector`'s Verlet case + the per-chain "Dynamic Chain Physics" section) that still reference the removed `hasHeadCollider` trio, replacing them with the new single `collisionEnabled` checkbox + a live collider-count readout. **(v2: ALSO fixes `src/Editor/BoneViewerWindow.cpp`/`.h` — two more call sites referencing the same removed fields that v1 never assigned to any phase — see that file's own Step 2/3.)** | 4 |
| 6 | `PHASE6_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md` | New end-to-end regression test proving a real chain collides correctly against a real Sphere+Box+Capsule trio through the FULL `PhysicsSystem::Update()` pipeline (not just isolated math); final sweep confirming every new file is registered in both CMakeLists.txt files and every pre-existing test that referenced the removed fields was updated, not merely deleted. **(v2: corrects the test-fixture helper guidance, adds `BoneViewerWindow.cpp`/`.h` to the audited file list, adds an `AGENTS.md` job-safety-table update.)** | 1–5 |

### Cross-cutting conventions every phase MUST follow (do not deviate)

- **No file globbing exists.** Every new `.h`/`.cpp` source file added in
  ANY phase below MUST be added to `CMakeLists.txt`'s
  `add_library(gte_core STATIC ...)` list (alphabetically-grouped-by-folder,
  matching the existing `src/Physics/...` block). Every new **test** file
  MUST be added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list (and,
  if it lives under `src/Editor/`, gated behind the same
  `if(GTE_ENABLE_EDITOR)`/`if(GTE_ENABLE_PROJECT_PANEL)` blocks the
  neighboring Editor tests already use).
- **Never silently regress an existing behavior.** `SphereColliderTests.cpp`
  and the low-level `SolveSphereCollision()` itself are NOT touched by this
  campaign — they keep working exactly as before; new Box/Capsule primitives
  are purely additive siblings.
- **Degrade gracefully, never crash.** Every new function must follow the
  codebase's own established convention (seen throughout
  `DynamicChainSolver.cpp`, `ChainConstraints.cpp`, `SphereCollider.cpp`):
  a malformed/degenerate/out-of-range input is a documented no-op, never an
  exception, never an out-of-bounds read.
- **`previousPosition` is never touched by a collision response** — matches
  `SolveSphereCollision()`'s own explicit contract (a corrected position
  still contributes to next-step implied velocity, producing a visible
  "slide off the surface" instead of an instant freeze).
- **A pinned particle (`particle.pinned == true`) is always left untouched**
  by every collision function — same existing convention as every other
  constraint in `ChainConstraints.h`/`SphereCollider.h`.
- **Doc-comment discipline.** This codebase's header comments are long,
  precise, and cross-reference the exact originating phase/file/line of
  reasoning (see any existing header in `src/Physics/`). Match that style —
  future maintainers (and future AI agents) rely on it exclusively; there is
  no separate design-doc wiki.
- **PMX rotation convention.** Any code that turns a `RigidBody`'s
  `rotateRadians` (radians, PMX Euler order) into a `Quat` MUST use exactly
  `Quat::FromEulerDegrees(RadToDeg(x), RadToDeg(y), RadToDeg(z))` — this is
  the one and only verified-correct PMX rotation order in this codebase
  (`src/Editor/RigidBodyWireframe.cpp`'s own `RotationFromPmxEuler()`,
  cross-referenced against `saba::MMDPhysics.cpp`). Do not "simplify" it to
  a different Euler order.
- **Architectural tiering** (this matters for where new files live):
  - `src/Physics/{VerletParticle,VerletIntegration,ChainConstraints,
    SphereCollider,WindField}.h` are today's **pure, engine-data-free**
    primitives — no bone index, no `Assets/` include, nothing but plain
    `Vec3`/`float`/`bool`. PHASE1's new `BoxCollider.h`/`CapsuleCollider.h`/
    `Collider.h` belong in this same tier.
  - `src/Physics/{DynamicChainDefinition,DynamicChainDetection,
    RigidBodyJointGraph}.h` are the **data-driven** tier — they freely
    reference `Assets/PhysicsData.h`/`Assets/SkeletonData.h` and raw bone
    indices. PHASE2's `ModelColliderDefinition.h` and PHASE3's
    `ModelColliderDetection.h` belong in this same tier (this exactly
    mirrors — and is justified by — `DynamicChainDetection.h`'s own existing
    `#include "../Assets/PhysicsData.h"`).
  - Keep these two tiers separate: `Collider.h` (Phase 1) must never
    `#include` `Assets/PhysicsData.h` or know what a "bone" is; the
    translation from a PMX `RigidBodyShape`/bone-relative data into a
    plain world-space `Collider` happens only in Phase 3/4 code.

### Why the design is "one shared model-wide collider list", not "one collider per chain"

The old, removed feature let a human hand-pick exactly one sphere per
chain. The new feature auto-derives a **whole list** of colliders per
**model** (every `RigidBodyMotionType::Static` PMX body, of any shape) once,
and every chain that opts in (`collisionEnabled = true`) is tested against
the model's **entire** list, not just one hand-picked bone. This is more
correct (a skirt's hem should be able to brush against a leg's Static
capsule collider even though the skirt's own chain is anchored at the hip,
not the leg) and removes the old "human must hand-pick a bone index and
guess a radius" authoring burden entirely — collision now "just works" the
moment `collisionEnabled` is switched on for a chain, exactly matching the
user's own request that collision "just work" against whatever shapes the
model already ships.

### Full list of every file this campaign touches or creates

```
NEW   src/Physics/BoxCollider.h                          (Phase 1)
NEW   src/Physics/BoxCollider.cpp                         (Phase 1)
NEW   src/Physics/CapsuleCollider.h                       (Phase 1)
NEW   src/Physics/CapsuleCollider.cpp                     (Phase 1)
NEW   src/Physics/Collider.h                              (Phase 1)
NEW   src/Physics/Collider.cpp                            (Phase 1)
NEW   tests/Physics/BoxColliderTests.cpp                  (Phase 1)
NEW   tests/Physics/CapsuleColliderTests.cpp               (Phase 1)
NEW   tests/Physics/ColliderTests.cpp                      (Phase 1)
EDIT  src/Physics/DynamicChainDefinition.h                (Phase 2)
NEW   src/Physics/ModelColliderDefinition.h               (Phase 2)
EDIT  src/Physics/DynamicChainSolver.h                    (Phase 2)
EDIT  src/Physics/DynamicChainSolver.cpp                  (Phase 2)
EDIT  src/Physics/DynamicChainDetection.cpp               (Phase 2 - remove old heuristic)
EDIT  src/Game/Physics/PhysicsSystem.h                    (Phase 2 v2 - stale doc-comment fix only)
EDIT  tests/Physics/DynamicChainSolverTests.cpp            (Phase 2 - update tests e/f)
NEW   src/Physics/ModelColliderDetection.h                (Phase 3)
NEW   src/Physics/ModelColliderDetection.cpp              (Phase 3)
NEW   tests/Physics/ModelColliderDetectionTests.cpp       (Phase 3)
EDIT  src/Game/Physics/DynamicChainRigCache.h             (Phase 3)
EDIT  src/Game/Physics/PhysicsSystem.cpp                  (Phase 3 registration call + Phase 4 runtime resolution)
EDIT  src/Editor/Panels/InspectorPanel.cpp                (Phase 5, two call sites)
EDIT  src/Editor/BoneViewerWindow.h                       (Phase 5 v2 - RigidBodyEntry gains `motionType`)
EDIT  src/Editor/BoneViewerWindow.cpp                     (Phase 5 v2 - two more call sites v1 missed entirely)
NEW   tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp (Phase 6)
EDIT  AGENTS.md                                           (Phase 6 v2 - Job System table row, doc-only)
EDIT  CMakeLists.txt                                      (every phase that adds a file)
EDIT  tests/CMakeLists.txt                                (every phase that adds a test file)
```

Proceed to `PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md`.
