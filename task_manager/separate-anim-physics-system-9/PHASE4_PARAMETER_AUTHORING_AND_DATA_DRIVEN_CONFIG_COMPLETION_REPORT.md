# PHASE4 — Parameter Authoring and Data-Driven Configuration — Completion Report

Campaign: `task_manager/verlet-integration-1/` (Verlet-Integrated Dynamic Bone
Physics System). Parent: `PHASE0_MASTER_STRATEGY.md`. This report covers
**Phase 4 only** — `PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md` (v4)
— which depends on Phase 3 (`ResolvedAnimationPose`/`DynamicChainRig` ECS
components, the standalone `PhysicsSystem`, its stubbed
`DynamicChainRigCache`/`GlobalPhysicsSettings` members).

## Branch / Prerequisites

- Branch: `feature/physics-from-scratch` (unchanged, as required).
- Read `readme.md` and `agents.md` in full before starting.
- Re-read all six strategy files under `task_manager/verlet-integration-1/`
  (`PHASE0_MASTER_STRATEGY.md` through `PHASE5_...md`), focusing on
  `PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md`'s own v3/v4 Revision
  Notices (ownership on `PhysicsSystem`, never `AnimationSystem`; generic
  naming).
- Read the previous phase's own completion report
  (`task_manager/separate-anim-physics-system-9/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP_COMPLETION_REPORT.md`)
  for continuation clues — it confirmed exactly which two methods
  (`PhysicsSystem::RegisterDynamicChains()`/`AttachDynamicChainRigIfNeeded()`)
  were left as provable no-op stubs, that the `Game.cpp` call sites were
  already wired and would need no changes, and that
  `DynamicChainRigCache::ModelEntry` already had the exact shape this phase
  needed (`chains` + a `SkeletonData skeleton` copy).
- Verified against the CURRENT source tree that `SkinnedMeshData`
  (`src/Game/Animation/SkeletalRigCache.h`) did **not** yet carry a
  `PhysicsData` member, exactly as Phase 4's own document flagged as a
  possible gap to check ("if `SkinnedMeshData` does not yet carry a
  `PhysicsData` member, add one now") — confirmed directly and fixed as part
  of this phase (see below).

## What Was Done

Implemented Phase 4 per `PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md`'s
(v4) Step 3/Step 5 checklist, end to end.

### 3.1 — `GlobalPhysicsSettings` (already existed from Phase 3)

Phase 3 had already created `src/Physics/GlobalPhysicsSettings.h` with the
exact fields this phase's document draft called for (`gravity`, `wind`,
`fixedTimestepSeconds`, `maxStepsPerFrame`) — no change needed here.

### New (required by 3.4) — `SkinnedMeshData` gained a `physics` field

- **`src/Game/Animation/SkeletalRigCache.h`** — added
  `std::optional<PhysicsData> physics;` to `SkinnedMeshData`, exactly per this
  phase's own conditional instruction.
- **`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`** —
  `EnsureMeshAsset()`'s existing skinned-model branch now also copies
  `rig->physics` (already extracted by `PmxLoader.h`/`RigFile.h` — see
  `RigFileData::physics`) into `skinData.physics`, alongside the pre-existing
  `skinData.skinWeights`/`skinData.skeleton` assignments. A boneless/
  physicsless model still gets `std::nullopt`, never a failure.

### 3.2 — `src/Physics/DynamicChainDetection.h/.cpp` (new)

- **`DynamicChainDetectionDefaults`** — `defaultJointSettings`/
  `defaultGravityScale`/`defaultWindScale`/`defaultConstraintIterations`/
  `minimumChainLength` (defaults to 2), exactly per the document's struct.
- **`DetectDynamicChains(skeleton, physics, defaults)`** — a pure, Tier-1
  function with zero ECS/GPU/file-I/O dependency:
  - Builds a `childrenByParent` adjacency list once.
  - For every bone flagged `deformAfterPhysics`, determines whether it is a
    genuine **chain-start boundary**: either its parent is NOT itself
    `deformAfterPhysics` (the ordinary case), OR its parent IS
    `deformAfterPhysics` but has MORE THAN ONE `deformAfterPhysics` child (a
    **branch point** — each of the branch's children starts its OWN new
    chain, anchored at the branch bone itself). A bone with no parent at all
    (`parentBoneIndex < 0`) is never treated as a boundary — the whole run is
    discarded per this phase's own degenerate-root-bone rule (Culprit/Finding
    #5 from `PHASE0`'s v2 Revision Notes).
  - Walks forward from each boundary only while there is EXACTLY ONE
    `deformAfterPhysics` child, stopping at a leaf (0) or a branch (>1) —
    correctly leaving branch children to be discovered as independent chain
    starts by the same outer loop, rather than folding a branching rig into
    one incorrect linear definition.
  - Discards any resulting run shorter than `minimumChainLength`.
  - Computes `restLengths` directly from bind-pose `Bone::position` values
    (never recomputed per frame).
  - Seeds `mass`/`damping` per joint from a matching `RigidBody` (matched by
    `boneIndex`, only when `motionType != RigidBodyMotionType::Static`) when
    `physics` is non-null; `stiffness` always comes from
    `defaults.defaultJointSettings.stiffness` (no PMX equivalent), exactly as
    specified.

### 3.3 — `DynamicChainRigCache` gained a mutable accessor

Phase 3's stub `DynamicChainRigCache` (`src/Game/Physics/DynamicChainRigCache.h`)
already had the exact `ModelEntry{ chains, skeleton }` shape and a
`Register()`/`TryGet()` pair this phase needed — this phase added exactly one
new method, **`TryGetMutable()`** (a non-`const` counterpart to `TryGet()`),
needed so the Editor Inspector's live per-joint damping/stiffness/mass
sliders (3.5 below) can write directly into the cached
`DynamicChainDefinition` without a second, parallel cache existing.

### 3.4 — Wired real detection + real cache into `PhysicsSystem`

- **`PhysicsSystem::RegisterDynamicChains()`** (`src/Game/Physics/PhysicsSystem.cpp`)
  — no longer a `(void)`-cast no-op: calls `DetectDynamicChains()` with a
  default-constructed `DynamicChainDetectionDefaults`, then
  `m_rigCache.Register(absoluteGtaPath, ModelEntry{ chains, skeleton copy })`.
- **`PhysicsSystem::AttachDynamicChainRigIfNeeded()`** — no longer a no-op:
  looks up the just-registered model, and — if it has at least one detected
  chain — adds a `DynamicChainRig` component sized to match, exactly per the
  phase document's own pseudocode (this body was already correct in Phase 3's
  stub file comment/shape; Phase 4 just made `RegisterDynamicChains()`
  actually populate something for it to find).
- **`PhysicsSystem::GetDynamicChainRigCache()`** (new, both `const`/non-`const`
  overloads) — the Editor-facing accessor 3.5 needs to reach the cache
  directly (`PhysicsSystem::Update()` itself continues to use `m_rigCache`
  directly, never through this new accessor).
- **`Game.cpp`'s call site** — unchanged, exactly as the Phase 3 report
  predicted: `m_physicsSystem.RegisterDynamicChains(...)`/
  `AttachDynamicChainRigIfNeeded(...)` were already wired inside
  `Game::CreateMeshEntityFromGtaFile()`, alongside (never through)
  `m_animationSystem.RegisterSkinnedMesh()`.

### 3.5 — Editor Inspector: "Dynamic Chain Physics" section

- **`src/Game/Game.h`** — added a public `PhysicsSystem& GetPhysicsSystem()`
  accessor, mirroring `GetRegistry()`'s existing "Editor observes/edits
  through a public accessor" convention.
- **`src/Editor/Panels/InspectorPanel.h/.cpp`** — `BuildInspectorPanel()`/
  `BuildEntityInspector()` both gained a `PhysicsSystem& physicsSystem`
  parameter (present in EVERY signature, not gated behind
  `GTE_ENABLE_PROJECT_PANEL`, since this feature has nothing to do with that
  switch). A new section, shown for any entity carrying a `DynamicChainRig`
  component (right after the existing "Skeletal Animator" section, following
  its exact `CollapsingHeader` pattern):
  - An `enabled` checkbox bound directly to `DynamicChainRig::enabled`.
  - A read-only "N chain(s), N joint(s) total" summary.
  - Per-chain (`ImGui::TreeNode`), per-joint `damping`/`stiffness`/`mass`
    (`DragFloat`) sliders, writing directly into the
    `DynamicChainDefinition`/`DynamicJointSettings` held by
    `PhysicsSystem`'s own `DynamicChainRigCache` (via the new
    `TryGetMutable()`) — a live, shared-across-every-instance-of-that-model
    edit, exactly as specified (no per-instance override this phase).
  - A read-only `GlobalPhysicsSettings` readout (gravity, wind direction/
    base/gust strength) wrapped in `ImGui::BeginDisabled()` — makes the
    global/local split visible/legible without building a live-editing
    surface for it this phase (explicitly deferred, per Step 4 below).
- **`src/Editor/ImGuiEditorLayer.cpp`** — the one call site updated to pass
  `game.GetPhysicsSystem()` through to both `BuildInspectorPanel()` overloads.

### 3.6 — Tests for this phase

- **`tests/Physics/DynamicChainDetectionTests.cpp`** (new, 6 tests) — a
  simple 3-joint linear run detects correctly (root/order/rest-lengths); a
  branching run (one bone with two `deformAfterPhysics` children) detects two
  separate chains, each anchored at the branch bone and starting at its own
  child; a run shorter than `minimumChainLength` detects nothing; a matching
  `Dynamic` `RigidBody` overrides only its own joint's mass/damping while a
  sibling joint keeps the defaults; a degenerate skeleton whose own literal
  root bone is itself flagged `deformAfterPhysics` produces ZERO chains from
  that run (never `rootBoneIndex == -1`) while an unrelated, properly-
  anchored chain elsewhere in the same skeleton is still detected normally;
  and an empty skeleton detects nothing.
- **`tests/Game/Physics/DynamicChainRigCacheTests.cpp`** (new, 4 tests) — a
  trivial `Register()`/`TryGet()` round-trip (both `skeleton` and `chains`
  survive intact); an unknown path returns `nullptr` from both `TryGet()` and
  `TryGetMutable()`; and `TryGetMutable()` lets a caller live-edit a cached
  chain's `DynamicJointSettings`, visible to a subsequent `TryGet()` call.
- **`tests/Game/Physics/PhysicsSystemTests.cpp`** extended with a REAL,
  non-stub, end-to-end test,
  `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity` — registers
  a synthetic 4-bone rig (a 2-joint `deformAfterPhysics` run extending along
  +X, deliberately perpendicular to gravity — see "Bugs Found And Fixed"
  below for why) through `RegisterDynamicChains()`/
  `AttachDynamicChainRigIfNeeded()`, drives a pure-FK (all-identity)
  `ResolvedAnimationPose` directly (no `AnimationSystem` involved at all —
  further proof the two systems stay genuinely decoupled), runs 30
  `PhysicsSystem::Update()` calls at a 60fps `deltaSeconds` under non-zero
  gravity, and asserts the resulting pose genuinely DIFFERS from the pure-FK
  values that were written in — the end-to-end proof this whole campaign
  exists to deliver. Every pre-existing Phase 3 test in this file (the 5
  "provable no-op" tests, still valid since none of them register a real
  chain for their own paths) passes unchanged.
- **`tests/CMakeLists.txt`** — both new test files added to the
  unconditional (Tier 1) `GTE_TEST_SOURCES` list, plus matching descriptive
  paragraphs added to the file's own "Test taxonomy" header comment block for
  `DynamicChainDetectionTests.cpp`/`DynamicChainRigCacheTests.cpp`, and the
  existing `PhysicsSystemTests.cpp` paragraph extended to describe this
  phase's new end-to-end test alongside Phase 3's original no-op coverage.
- **`CMakeLists.txt`** — `src/Physics/DynamicChainDetection.h/.cpp` added to
  `gte_core`'s source list, right after the existing `Physics/*` block.

## Bugs Found And Fixed (during this phase's own implementation)

- **First draft of the end-to-end test (3.6) initially failed** — the
  synthetic rig's bones were laid out colinear with gravity (all along +Y,
  gravity pointing along -Y). A chain colinear with the gravity vector only
  ever compresses/stretches along its own bind axis under gravity, which the
  structural distance constraint fully undoes every step (restoring the
  exact original direction, just re-normalizing the distance) — producing
  ZERO net rotation, exactly matching `BoneChainPhysicsResolverTests.cpp`'s
  own documented "already aligned, skip" case. This is expected, correct
  physics behavior, not a bug in `DetectDynamicChains()`/`PhysicsSystem`
  itself — the test fixture was wrong, not the production code. Fixed by
  reorienting the synthetic rig to extend along +X (perpendicular to
  gravity), which genuinely bends under gravity as expected. Re-verified:
  the corrected test passes; nothing under `src/Physics/`/`src/Game/Physics/`
  needed any change for this.

## What Was Deliberately NOT Done (per Phase 4's own "Step 4: What We Will
NOT Do", and this task's overall workflow rules)

- **No per-INSTANCE parameter override** — every entity spawned from the same
  model `*.gta` shares one `DynamicChainDefinition` set (tunable via the
  Inspector), exactly mirroring `SkeletalRigCache`'s own per-path sharing.
- **No Editor control surface for editing `GlobalPhysicsSettings` itself** —
  3.5's readout is read-only (wrapped in `ImGui::BeginDisabled()`); live
  editing is an explicitly deferred follow-up.
- **No bone-NAME-based chain auto-detection** — `deformAfterPhysics`/
  `RigidBody::motionType` are the only signals used, per spec.
- **No new JSON/asset-file authoring format** for models with no PMX physics
  data — out of scope, per spec.
- **`DynamicChainRigCache`/`GlobalPhysicsSettings` remain owned exclusively by
  `PhysicsSystem`** — never became members of `AnimationSystem` at any point
  in this phase's implementation.
- No full build or full regression test was run (per this task's explicit
  workflow rules) — only a fast, targeted incremental compile of `gte_core` +
  `GreatTamanaEngineTests` + the `GreatTamanaEngine` executable itself (to
  confirm the whole engine, not just the test binary, still links cleanly
  with the Editor changes), plus a filtered run of the new/touched test
  suites.

## Verification

1. **Reconfigure**: `cmake -S . -B build` — succeeded, picked up the two new
   source files with no errors (only the same pre-existing, unrelated
   KTX-Software git-describe warning every prior phase report also noted).
2. **Fast compile check** (not a full build):
   - `cmake --build build --target GreatTamanaEngineTests` — rebuilt exactly
     the new/changed object files (`DynamicChainDetection.cpp.obj`,
     `PhysicsSystem.cpp.obj`, `MeshAssetGpuCatalog.cpp.obj`, `Game.cpp.obj`,
     `AnimationSystem.cpp.obj`, `InspectorPanel.cpp.obj`,
     `ImGuiEditorLayer.cpp.obj`, plus a handful of other Editor/Application
     files transitively depending on the touched headers, and the new test
     `.cpp.obj` files), re-linked `libgte_core.a`, and re-linked
     `GreatTamanaEngineTests.exe` — **23/23 build steps succeeded, zero
     warnings/errors**. (One iteration was needed: the end-to-end test's
     first synthetic rig failed at RUNTIME, not compile time — see "Bugs
     Found And Fixed" above; the corrected version recompiled and passed
     cleanly.)
   - `cmake --build build --target GreatTamanaEngine` — the real, full engine
     executable (Editor included) also builds and links cleanly against every
     changed header (`Game.h`, `InspectorPanel.h`, `PhysicsSystem.h`,
     `SkeletalRigCache.h`) with zero errors.
3. **Targeted test run**: ran
   `GreatTamanaEngineTests.exe --gtest_filter=DynamicChainDetectionTests.*:DynamicChainRigCacheTests.*:PhysicsSystemTests.*:AnimationSystemEvaluatePosesTest.*`
   directly — **19/19 tests passed** (0 failures: 6 new
   `DynamicChainDetectionTests`, 4 new `DynamicChainRigCacheTests`, 6
   `PhysicsSystemTests` — 5 pre-existing Phase 3 no-op tests unchanged plus 1
   new Phase 4 end-to-end test — and 3 pre-existing
   `AnimationSystemEvaluatePosesTest`). A broader filtered run
   (`*Physics*:*Skinned*:*SkeletalRigCache*:*Animation*`, 33 tests across 11
   suites, covering every test file touched even indirectly by the
   `SkinnedMeshData`/`RigFileData` changes) also passed 33/33 with zero
   regressions. No full `ctest` regression run was performed, per this task's
   workflow rules (reserved for the campaign's final phase).

## Next Steps (for whoever picks up Phase 5)

Proceed to `PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md`. It
depends directly on this phase's real `DetectDynamicChains()`/
`DynamicChainRigCache`/Inspector wiring — a real MMD model with a
`deformAfterPhysics` bone run (most rigs with hair/skirt jiggle bones)
now genuinely swings under gravity/wind end to end, with live-tunable
damping/stiffness/mass sliders visible in the Inspector. Two things Phase 5
should know about, directly resulting from this phase's own implementation
choices:

1. `DetectDynamicChains()`'s branch-handling rule (a branch bone becomes the
   ROOT anchor for each of its child chains, itself never included in any
   child chain's own `jointBoneIndices`) means a branch bone's own pose entry
   can be rewritten by MULTIPLE different chains' own
   `ApplyDynamicChainPhysicsToPose()` calls in the same frame (once per child
   chain, in `model->chains` iteration order) — today this is a silent
   "last write wins" per-frame overwrite, not a crash, but Phase 5's own
   collision/stability hardening pass should be aware this exists before
   adding anything that assumes a bone's pose entry is only ever written once
   per frame.
2. The Inspector's new "Dynamic Chain Physics" section (3.5) edits
   `PhysicsSystem`'s own `DynamicChainRigCache` entries directly and LIVE
   (no separate "Apply" step) — Phase 5's own performance-hardening work
   (e.g. Job System dispatch for many simultaneous chains) must keep this in
   mind: a chain definition being read by `PhysicsSystem::Update()` on one
   thread must never be concurrently mutated by the Inspector on the main
   thread if any of that work is ever moved off the main thread (today
   `PhysicsSystem::Update()` and the Inspector both only ever run on the main
   thread, so this is currently safe, but it is a real constraint to
   document once a job-dispatch change is made).
