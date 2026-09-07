# PHASE0 — MASTER STRATEGY: Decoupling Dynamic-Chain Physics From Animation (T-Pose / Idle Physics Campaign) — v2

Orchestrator document for the `verlet-integration-7` campaign. Every child
phase document in this folder implements one slice of this plan, in strict
order — each phase's code depends on the previous phase's deliverables
already compiling. Every phase is a real, compilable, testable increment that
adds or edits real `.h`/`.cpp` files; none of them are "just planning."

This campaign builds directly on `verlet-integration-1` through
`verlet-integration-6` (already-landed): the fixed-timestep Verlet solver, the
tree-shaped `DynamicChainDefinition`, and the RigidBody/Joint-graph-driven
`DetectDynamicChains()` are all correct and are **not** touched here. What
this campaign fixes is **when, where (in what coordinate space), and for
which entities** that already-correct simulation actually gets to run and
actually gets rendered.

**v2 notice:** this is the second iteration of this document. A full,
line-by-line re-audit of every child phase against the CURRENT `src/` tree
(see "Revision Notes (v2)" at the bottom) confirmed Phases 1, 2 and 5 exactly
as originally written — they are left untouched. Phases 3 and 4 each had one
real, code-provable gap, corrected in place below and in their own files.
**Phase 4 in particular gained a second, co-equal goal** (fixing what this
revision calls **Culprit F**) that is at least as important as its original
freeze/disable goal — read its "Revision Notes" cross-reference carefully
before implementing it.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis, user-alignment decisions, ordering, and (v2) the second-iteration audit findings. |
| `PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md` | Fixes Culprit A: `AnimationSystem::EvaluatePoses()` gains a second pass so every entity with an enabled `DynamicChainRig` gets a valid bind-pose `ResolvedAnimationPose` EVERY frame, even with zero `SkeletalAnimator`/zero playing clip — unblocking `PhysicsSystem::Update()` for a pure T-pose model for the first time. Re-verified in v2, unchanged. |
| `PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md` | Fixes Culprit B: `AnimationSystem::SkinAndUpload()` widened so a physics-adjusted pose on a non-animated entity is actually re-skinned/re-uploaded to the GPU (CPU **and** GPU-compute skinning modes both), so the simulation is actually visible on screen. Re-verified in v2, unchanged. |
| `PHASE3_WORLD_SPACE_ROOT_MOTION_AWARE_CHAIN_SIMULATION.md` | Fixes Culprit C: `PhysicsSystem` starts simulating in true world space (composing the owning entity's own ECS `Transform` with the bone-local pose) instead of blind bone-local "model space" — this is what makes dragging/rotating a model produce genuine Verlet inertial lag in its hair/skirt. **(v2, Finding #1)** the world matrix composed for physics purposes now deliberately EXCLUDES the entity's own `Transform::scale` (rotation + translation only) — composing full scale would silently desync every chain's pre-computed `restLengths`/collider radius/`maxPlausibleRootDelta` (all authored in unscaled bind-pose units) from the scaled world distances the solver would otherwise see, on any spawned instance that isn't exactly scale = 1. |
| `PHASE4_FREEZE_AND_DISABLE_RUNTIME_CONTROLS.md` | Fixes Culprit D: adds an explicit `DynamicChainRig::frozen` opt-out (pause simulation, keep the last simulated shape, still ride along rigidly with the model) alongside the existing `enabled` (hard off, revert to bind pose) toggle, exposed in the Inspector. **(v2, Finding #2 — Culprit F, newly discovered this iteration, equally mandatory)** generalizes this phase's own restructuring so `PhysicsSystem::Update()` NEVER skips re-applying a chain's last-known simulated shape merely because the fixed-timestep accumulator produced zero NEW integration steps this render frame — not just while explicitly `frozen`. Without this generalization, every `DynamicChainRig` entity (animated or, after Phase 1, especially a T-pose one) visibly "pops" back to raw bind/FK pose on every render frame whose delta time doesn't cross the fixed physics timestep, which is the common case on any display faster than ~60 Hz. |
| `PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md` | Fixes Culprit E: re-tunes the default `DynamicJointSettings`/`GlobalPhysicsSettings` numeric constants specifically for the newly-reachable "perfectly still T-pose" scenario, backed by a new automated settling/stability regression test. Re-verified in v2, unchanged. |

## Step 1: The Goal (Where are we going?)

Direct quotes from the user, each now a binding constraint on every phase
below:

- *"I want T Pose to actually simulate physic ... Right now physics rely on
  animation. not good."* — a model that is NEVER animated (never has
  `Game::PlayAnimationOnEntity()` called on it) must still have its detected
  dynamic bone chains (hair, skirt, etc.) simulated under gravity/wind, every
  frame, forever, exactly like an animated model's jiggle physics already
  does — and (v2) it must do so **smoothly, with no per-frame visual
  flicker back to a dead T-pose**, which is exactly what Culprit F (found
  this iteration) would otherwise silently defeat.
- *"i want to hair or skirt get physically move by simulation if i drag model
  position left and right without to actually run animation on it"* — this is
  the single most important, concrete acceptance test for this whole
  campaign: translating/rotating the model's own placement in the world (its
  ECS `Transform`) must produce visible **inertial lag** in its simulated
  chains — not just gravity sag while sitting still.
- *"Narrow: only models with a DynamicChainRig get the baseline pose
  treatment"* — this campaign's fixes must cost nothing extra for a plain,
  non-jiggle skinned prop; only models that actually carry a
  `DynamicChainRig` (i.e. `DetectDynamicChains()` found at least one chain)
  are affected.
- *"if GPU skinning: 1. skin on gpu 2. do physics simulation on gpu using same
  buffer from step 1. if not possible, then we can temporary disable gpu
  skinning for now"* — resolved (see Phase 2's own Step 2): the actual fix
  needed is far simpler than a full GPU-physics rewrite — widening WHICH
  entities `SkinAndUpload()` iterates already makes both the existing CPU
  path and the existing GPU-compute path work correctly for a non-animated,
  physics-only entity, with zero need to disable GPU skinning at all. A full
  GPU-resident Verlet solver sharing the GPU skinning buffer directly is
  flagged as a legitimate, separate, much larger future campaign — see this
  document's own "What We Will NOT Do", Step 4.
- *"auto animate. but i can opt-in to disable it or freeze it"* — physics
  must be always-on the instant a model with detected chains is spawned, no
  extra call needed, with an explicit, separate way to (a) disable it
  entirely (already exists: `DynamicChainRig::enabled`) or (b) freeze it in
  place (new: `DynamicChainRig::frozen` — Phase 4).
- *"Also reconsider the physics tuning/behavior itself"* — Phase 5 explicitly
  revisits the numeric defaults, not just the plumbing.

## Step 2: The Situation / The Problem (Where are we now?)

A full read of `src/Game/Game.cpp`, `src/Game/Animation/AnimationSystem.h/.cpp`,
`src/Game/Physics/PhysicsSystem.h/.cpp`, `src/ECS/Components/ResolvedAnimationPose.h`,
`src/ECS/Components/SkeletalAnimator.h`, `src/ECS/Components/DynamicChainRig.h`,
`src/ECS/TransformHierarchy.h`, `src/Animation/BoneWorldMatrixQuery.h`,
`src/Physics/DynamicChainSolver.cpp`, `src/Physics/DynamicChainDefinition.h`,
`src/Physics/FixedTimestepAccumulator.cpp`, `src/Game/RenderSystem.cpp`, and
every existing test in `tests/Game/Physics/`, `tests/Game/Animation/` turned
up the following, in order of how deeply they sit in the pipeline:

**`Game::Update()`'s own three-call order is ALREADY correct** and needs no
change: `m_animationSystem.EvaluatePoses(...)` → `m_physicsSystem.Update(...)`
→ `m_animationSystem.SkinAndUpload(...)` (`src/Game/Game.cpp`, lines 34-36).
This matches the user's own requested sequence
("1. Update Animation, 2. Update Physics, 3. Skinning, 4. Render" —
`Game::Render()` is called separately, afterward, by the application loop).
The bug is never about ordering — it is about which entities each of those
three stages ever actually touches, and in what coordinate space.

1. **Culprit A — `ResolvedAnimationPose` (the ONLY hand-off data physics can
   read/write) is produced EXCLUSIVELY by `AnimationSystem::EvaluatePoses()`
   for an entity carrying an actively-PLAYING `SkeletalAnimator`.**
   `EvaluatePoses()`'s entire body (`AnimationSystem.cpp`, lines 196-261)
   iterates `ComponentStorage<SkeletalAnimator>` only, and its very first
   guard is `if (!animator.playing || animator.animationGtaPath.empty()) {
   continue; }` (line 209) — a T-pose model that was spawned via
   `Game::CreateMeshEntityFromGtaFile()` but never had
   `Game::PlayAnimationOnEntity()` called on it never gets a
   `SkeletalAnimator` component at all, so it never even reaches this loop's
   body. `PhysicsSystem::Update()`'s own guard
   (`resolvedPose == nullptr -> continue`, `PhysicsSystem.cpp` line 207-209)
   then unconditionally skips that entity, forever, every single frame. This
   exact behavior is not an accident this campaign is "discovering" — it is
   **currently asserted as correct** by
   `tests/Game/Physics/PhysicsSystemTests.cpp`'s own
   `UpdateSafelyNoOpsOnAnEntityWithDynamicChainRigButNoResolvedAnimationPoseYet`
   test. Meanwhile `DynamicChainRig` itself IS already attached unconditionally
   for any skinned model with detected chains, regardless of animation
   (`Game::CreateMeshEntityFromGtaFile()` calls
   `m_physicsSystem.RegisterDynamicChains()` +
   `AttachDynamicChainRigIfNeeded()` unconditionally, lines 62-63) — so the
   ONLY missing piece is `ResolvedAnimationPose` itself. **Fixed by Phase 1.**
2. **Culprit B — even once Culprit A is fixed and `PhysicsSystem::Update()`
   starts writing a physics-adjusted pose for a non-animated entity, nothing
   ever renders it.** `AnimationSystem::SkinAndUpload()`
   (`AnimationSystem.cpp`, lines 263-463) has the EXACT SAME gate:
   `ComponentStorage<SkeletalAnimator>` only, `if (!animator.playing ||
   animator.animationGtaPath.empty()) { continue; }` (line 306). A
   `DynamicChainRig`-only entity never reaches CPU vertex re-skinning
   (`SkinVertexRange`), never reaches GPU bone-matrix re-upload
   (`gpuEntry->boneMatricesBuffer.Upload(...)`), and never gets its
   `Mesh::UpdateVertexData()` called — its GPU mesh buffer is permanently
   frozen at whatever it was uploaded as on first spawn. Every one of the
   three lookups this method makes (`SkeletalRigCache::TryGet()`,
   `GpuSkinningRigCache::TryGet()`,
   `MeshInstantiationSystem::TryGetMeshAssetParts()`) is keyed purely by
   `absoluteGtaPath` string — none of them structurally requires a
   `SkeletalAnimator` to exist; it is purely an artifact of which entity SET
   this loop currently iterates. **Fixed by Phase 2**, for both
   `SkinningMode::CpuJobSystem` (today's default) and
   `SkinningMode::GpuCompute` — no need to disable GPU skinning at all.
3. **Culprit C (the deepest one, and the one the user's own drag-test example
   directly exposes) — `PhysicsSystem` simulates entirely in bone-local
   "model space," with ZERO knowledge that the owning ECS entity even has a
   `Transform`.** `StepDynamicChainRange()` (`PhysicsSystem.cpp`, lines
   69-114) computes every root/joint/collider position via
   `ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex)`
   (`Animation/BoneWorldMatrixQuery.h`) — and that function, by its own
   explicit doc comment and implementation
   (`Animation/BoneChainResolver.h`'s `ResolveSingleBoneChain()`), ONLY EVER
   walks `SkeletonData::Bone::parentBoneIndex` — it has no `Registry&`
   parameter, no knowledge `ECS/TransformHierarchy.h` exists at all. Meanwhile
   `RenderSystem::CollectRenderables()` (`RenderSystem.cpp`, lines 8-33)
   applies the entity's REAL, resolved world Transform
   (`ComputeWorldMatrix(registry, entity)`, walking the full ECS parent
   chain) as one single rigid `model` matrix multiply, in the vertex shader,
   entirely DOWNSTREAM of and invisible to CPU skinning/physics. The direct,
   observable consequence: dragging a spawned model's `Transform.position` in
   the Editor moves every vertex — including every hair/skirt tip — by
   EXACTLY the same rigid delta, in the very same frame, with **zero lag**,
   because `PhysicsSystem` never even learns the entity moved; from its own
   point of view, "model space" and "world space" are silently assumed to be
   the same thing, which is only ever true when the entity's own resolved
   Transform happens to be the identity. This is true whether or not the
   model is animated — it is a strictly separate bug from Culprit A/B.
   **Fixed by Phase 3.**

   **(v2 addendum — Finding #1, discovered this iteration):** naively
   composing the entity's FULL resolved world transform (translation *
   rotation * scale, i.e. `ECS/TransformHierarchy.h::ComputeWorldMatrix()`'s
   own `Mat4::TRS(...)`) — which is what a first-pass reading of "simulate in
   world space" suggests — silently introduces a NEW bug for any spawned
   instance whose `Transform::scale != Vec3::One()` (a supported, ordinary
   case — `Transform.h`'s own `scale` field defaults to `Vec3::One()` but is
   freely editable, e.g. via the Editor's transform gizmo/Inspector). Every
   `DynamicChainDefinition::restLengths` entry, `headColliderRadius`, and
   `maxPlausibleRootDelta` is precomputed once, in the model's own UNSCALED
   bind-pose units (`DynamicChainDetection.h`, at registration time, shared
   by every entity spawned from that model path). If the physics-space world
   matrix bakes in a non-unit scale, the Verlet solver's structural distance
   constraints (`ChainConstraints.h::SolveDistanceConstraint()`) would be
   fighting to hold WORLD distances at the UNSCALED rest lengths — visibly
   stretching or crushing a scaled model's simulated chain relative to its
   own scaled body, and shifting how far a "plausible" drag can be before
   `maxPlausibleRootDelta` misfires. Phase 3 (v2) fixes this by excluding
   scale entirely from the matrix used for physics purposes — see that
   phase's own updated Step 3.1.
4. **Culprit D — there is no way to pause a chain's simulation while keeping
   its current jiggled shape.** `DynamicChainRig::enabled` (added by
   `verlet-integration-1`, `ECS/Components/DynamicChainRig.h`) is the only
   existing control, and it is a hard stop: the moment it is (or, post-Phase
   1, the entity's own baseline pass) is skipped, the chain's bones simply
   revert to whatever the bind/FK pose already says for them — there is no
   "keep the last simulated shape, stop moving it further" middle ground.
   **Fixed by Phase 4.**
5. **Culprit E — every existing numeric default (`DynamicJointSettings::damping
   = 0.08f`/`stiffness = 0.35f`, `GlobalPhysicsSettings::gravity`/zero-default
   `WindSettings`) was only ever exercised, and only ever visually judged,
   with physics riding on TOP of an actively-playing MMD dance animation** —
   large, constantly-changing bone motion that masks a mediocre default
   (e.g. a `stiffness` that snaps back almost immediately, or a `damping`
   that never quite settles). Once Phases 1-4 land, physics runs
   continuously against a PERFECTLY STILL T-pose baseline for the first time
   ever in this engine's history — any tuning problem (looks "dead," never
   sags at all; or jitters/oscillates forever without settling) becomes
   immediately, permanently visible with nothing to hide behind. **Fixed by
   Phase 5.**
6. **Culprit F (v2 — newly discovered this iteration; every bit as
   consequential as A-E, and arguably the single biggest risk to this
   campaign's own core acceptance test) — `PhysicsSystem::Update()`'s
   per-rig loop `continue`s out entirely whenever the fixed-timestep
   accumulator produces zero NEW steps this render frame, which SILENTLY
   ALSO skips re-applying the chain's already-known simulated shape into the
   pose for this frame:**

   ```cpp
   const int stepCount = ComputeFixedStepCount(rig.accumulatedSeconds, ...);
   if (stepCount <= 0) {
       continue; // <-- ALSO skips re-applying state.particles into `pose` this frame!
   }
   ```

   (`PhysicsSystem.cpp`, current lines 216-220). `ComputeFixedStepCount()`
   (`Physics/FixedTimestepAccumulator.cpp`) implements the standard
   accumulator pattern against a FIXED `fixedTimestepSeconds` (default
   `1.0f/60.0f`) regardless of the ACTUAL, variable render frame rate — on
   any display faster than ~60 Hz (144 Hz/240 Hz gaming monitors are
   mainstream, and this engine's own `Application::Run()` is explicitly
   variable-timestep, per `FixedTimestepAccumulator.h`'s own file comment),
   the overwhelming MAJORITY of rendered frames legitimately produce
   `stepCount == 0` — that is the whole point of an accumulator, and is not
   itself a bug. The bug is what happens to `resolvedPose->pose` on exactly
   those frames: `AnimationSystem::EvaluatePoses()` (today, for any playing
   `SkeletalAnimator`; after Phase 1, for EVERY enabled `DynamicChainRig`,
   animated or not) unconditionally overwrites the WHOLE
   `ResolvedAnimationPose::pose` array, every single frame it runs, with a
   value that has ZERO knowledge of physics (`EvaluateAnimatedPoseBeforePhysics()`'s
   pure sample→IK→append result, or Phase 1's own all-default bind pose) —
   this happens on every frame, whether or not `PhysicsSystem::Update()` is
   about to step. When `PhysicsSystem::Update()` then `continue`s past a rig
   because `stepCount <= 0`, it leaves that fresh, physics-blind pose
   completely UNCORRECTED for this one frame — every chain-controlled bone
   visibly "pops" back to raw bind/FK for that single rendered frame, then
   snaps back to its correct simulated position the next frame that DOES
   cross the fixed-step threshold. The net visible effect at any frame rate
   above ~60 Hz is a persistent, rapid flicker between "settled/sagging" and
   "rigid T-pose," on EVERY `DynamicChainRig` entity, not a smooth
   simulation at all. This defect already exists, latent, in today's shipped
   engine for an ANIMATED model (pre-dating this whole campaign) — but Phase
   1 makes it dramatically worse and far more visible for the campaign's own
   headline scenario, because Phase 1's baseline pass overwrites the ENTIRE
   pose to a flat, all-default bind pose every single frame, unconditionally,
   for a T-pose model — the exact scenario Phase 5 is about to spend an
   entire phase carefully tuning to look good would otherwise flicker
   uselessly regardless of how good the tuning is. **Fixed by Phase 4**,
   generalizing the exact restructuring that phase already needed for its
   own `frozen` feature (re-apply `state.particles` into the pose
   unconditionally, taking zero NEW integration steps when `stepCount == 0`,
   for ANY reason — `frozen` or an ordinary sub-threshold accumulator frame)
   into the phase's own PRIMARY, mandatory deliverable rather than a
   `frozen`-only side effect.

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 5, strictly in order — each one's own tests assume the
previous phase's code already compiles and passes:

1. **Phase 1** makes `AnimationSystem::EvaluatePoses()` guarantee a valid,
   correctly-sized, bind-pose `ResolvedAnimationPose` exists every frame for
   every entity with an ENABLED `DynamicChainRig`, whether or not it also has
   a playing `SkeletalAnimator` — the single missing link that unblocks
   `PhysicsSystem::Update()` (itself completely untouched by this phase) for
   a pure T-pose model for the first time. Must land first: every later
   phase's own tests assume a non-animated entity can already reach
   `PhysicsSystem::Update()` with a valid pose.
2. **Phase 2** widens `AnimationSystem::SkinAndUpload()`'s own entity
   iteration to also cover a `DynamicChainRig`-only entity (Phase 1's new
   producer), so Phase 1 + `PhysicsSystem::Update()`'s simulated pose is
   actually re-skinned/re-uploaded and visible on screen, for CPU and
   GPU-compute skinning modes alike, with the strict single-model-at-a-time
   GPU-buffer-sharing sequencing rule fully preserved.
3. **Phase 3** rewrites `PhysicsSystem`'s own root/joint/collider position
   math to compose the owning entity's REAL, resolved ECS world position and
   rotation (`ECS/TransformHierarchy.h::ComputeWorldTransform()`,
   deliberately EXCLUDING scale — v2, Finding #1) with the existing
   bone-local pose math, so the Verlet solver genuinely integrates in true
   (unscaled) world space — this is what makes dragging/rotating a model
   produce real inertial lag in its hair/skirt, the user's own explicit
   acceptance test, without desyncing every chain's pre-authored rest
   lengths/collider radius/teleport-guard threshold for a non-unit-scale
   instance. Depends on Phase 1 only for END-TO-END testability (a
   non-animated drag-test needs Phase 1's baseline pose to exist) — the code
   change itself lives entirely in `PhysicsSystem.cpp`.
4. **Phase 4** does two co-equal things now (v2): (a) adds the explicit
   `DynamicChainRig::frozen` opt-out (pause stepping, keep the last
   simulated shape, still ride along rigidly with further Transform motion)
   alongside the existing `enabled` toggle, exposed in the Inspector; and
   (b) fixes Culprit F — generalizes the exact same "reapply the last known
   simulated shape even when zero new integration steps run this call"
   restructuring so it applies UNCONDITIONALLY, for every rig, on every
   frame the fixed-timestep accumulator produces `stepCount == 0`, not only
   while explicitly `frozen`. (b) has no dependency on (a) at all and could
   theoretically land alone, but they touch the exact same functions
   (`PhysicsSystem::Update()`'s per-rig loop, `StepDynamicChainRange()`), so
   landing them together in one phase avoids touching this code twice.
   Depends on Phase 3's world-space plumbing (a frozen chain's "keep riding
   along rigidly" behavior, and Culprit F's "reapply every frame" behavior,
   are both only meaningful once the entity's own Transform is actually
   consulted every frame).
5. **Phase 5** re-tunes the actual numeric physics constants and adds a new
   automated settling/stability regression test for the now-continuously-
   running idle/T-pose scenario. Deliberately last: tuning against a fully
   working, fully visible, non-flickering pipeline (Phases 1-4) rather than
   tuning blind, or tuning against a still-flickering pipeline that would
   have made Phase 5's own settling measurements meaningless.

## Step 4: What We Will NOT Do (Focus)

- We will **not** implement a full GPU-resident Verlet solver sharing the GPU
  skinning compute buffer directly (the user's own conditional "if not
  possible" fallback). Investigation during this Phase 0 pass already found
  this is unnecessary to satisfy the actual ask — Phase 2's much smaller fix
  (widen `SkinAndUpload()`'s entity set) already makes both CPU and
  GPU-compute skinning modes correctly display physics-only entities with no
  GPU pipeline changes at all. A genuine GPU-Verlet-physics rewrite remains a
  legitimate, separate, much larger future campaign.
- We will **not** change `Game::Update()`'s fixed three-call order — already
  correct.
- We will **not** touch `Physics/DynamicChainDetection.h`,
  `Physics/RigidBodyJointGraph.h`, or any part of `verlet-integration-6`'s
  already-correct chain-detection algorithm. This campaign is entirely about
  WHEN/WHERE/in-what-SPACE an already-detected chain gets simulated and
  rendered, never about HOW chains are detected.
- We will **not** touch `Physics/VerletIntegration.cpp`,
  `Physics/ChainConstraints.cpp`, `Physics/WindField.cpp`,
  `Physics/SphereCollider.cpp`, or `Physics/FixedTimestepAccumulator.cpp`'s
  own internal formulas — Phase 5 only touches numeric DEFAULT VALUES
  (`DynamicChainDefinition.h`'s `DynamicJointSettings`,
  `GlobalPhysicsSettings.h`), never the pure math functions themselves. (v2:
  Phase 4's Culprit F fix touches only HOW `PhysicsSystem.cpp` calls into
  these functions — i.e. whether it calls them at all this frame and what it
  does with a `state.particles` array once already computed — never their
  own internal math either.)
- We will **not** remove, rename, or weaken
  `DynamicChainDefinition::maxPlausibleRootDelta`'s existing teleport-guard
  safety net — Phase 3 explicitly preserves and adds new regression tests for
  it (a real drag must stay under it; a real teleport must still trip it).
- We will **not** add per-instance overrides beyond the two new flags Phase 4
  adds (`frozen`) — this inherits the same already-accepted "per-model-path,
  shared by every spawned entity" limitation `DynamicJointSettings` editing
  already has today (see `verlet-integration-6`'s own identical scope note).
- We will **not** change `RenderSystem::CollectRenderables()`/
  `RenderSystem::Draw()` at all — the render stage already correctly applies
  whatever `pose`/mesh data the first three stages left it; this campaign's
  entire job is making sure those three stages actually populate that data
  for a non-animated, physics-only model, every frame, without flicker.
- We will **not** attempt to solve non-uniform-scale-aware COLLISION (a
  scaled `headColliderRadius` staying visually correct relative to a scaled
  mesh) — Phase 3 (v2) only guarantees the SIMULATION itself (rest lengths,
  teleport threshold) stays correct under scale by excluding scale from the
  physics-space matrix entirely; `headColliderRadius` remains an authored,
  literal world-space radius exactly as `DynamicChainDefinition.h`'s own doc
  comment already states, unaffected by (and un-informed by) an instance's
  own scale, same as before this campaign. A scale-aware collider radius
  remains an explicit non-goal, same spirit as this document's other
  deliberately-out-of-scope items above.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact function signatures/fields to add, the
exact existing call sites to touch, and the exact test files to add or
extend, following this codebase's own "one new/changed piece of pure logic,
one same-change test file update" convention already used by every prior
`verlet-integration-*` campaign. Do not reorder the phases — Phase 2's new
iteration path only has something meaningful to skin/upload once Phase 1's
baseline pose exists; Phase 3's drag-test is only end-to-end-testable once
Phase 1 lands; Phase 4's "ride along rigidly while frozen" behavior AND its
Culprit F fix are only meaningful once Phase 3's world-space plumbing exists;
Phase 5's settling test is tuning against the complete, already-working,
non-flickering pipeline every earlier phase built.

Whenever this document's own line-number citations and a later phase's own
re-verification disagree, trust the child phase's own re-check against the
real, current source tree at the moment it is implemented — this document is
the ROADMAP, each child phase is the up-to-date work order.

## Revision Notes (v2 — Second Iteration Audit)

Performed as a dedicated re-audit pass: every one of this campaign's five
child phase documents was re-read line-by-line against the actual, current
`src/` tree (not merely against its own prior prose) — every file/function/
line-number citation in Phases 1, 2, 3, 4 and 5 was independently
re-verified against `AnimationSystem.h/.cpp`, `PhysicsSystem.h/.cpp`,
`Game.cpp`, `DynamicChainRig.h`, `DynamicChainDefinition.h`,
`GlobalPhysicsSettings.h`, `TransformHierarchy.h`, `Transform.h`,
`ChainConstraints.h`, `DynamicChainSolver.cpp`, `FixedTimestepAccumulator.h/.cpp`,
`VerletParticle.h`, `DynamicChainRuntimeState.h`, `BoneLocalOffset.h`,
`SkeletonPose.h`, `Mat4.h`, and `InspectorPanel.cpp`.

- **Phases 1, 2 and 5: confirmed accurate, no changes made.** Every quoted
  code excerpt, line range, struct field, and function signature in these
  three documents matches the current source tree exactly (e.g. Phase 1's
  citation of `EvaluatePoses()` at `AnimationSystem.cpp` lines 196-261 is
  exact down to the line number; `DynamicJointSettings::damping = 0.08f`/
  `stiffness = 0.35f` cited by Phase 5 match `DynamicChainDefinition.h`
  verbatim). Their plans remain sound and are left byte-for-byte as
  originally written — deliberately NOT rewritten "for the sake of it," per
  this task's own explicit "skip v2 if it's already good enough" allowance.
- **Finding #1 (Phase 3 — scale exclusion):** the original Phase 3 composed
  the entity's FULL resolved world matrix (`ComputeWorldMatrix()`, i.e.
  translation * rotation * SCALE) for physics purposes, then took its
  generic 4x4 inverse (falling back to `Mat4::Identity()` for a "degenerate
  scale axis"). This is provably wrong for any spawned instance whose
  `Transform::scale != Vec3::One()`: `DynamicChainDefinition::restLengths`,
  `headColliderRadius`, and `maxPlausibleRootDelta` are all precomputed once
  in UNSCALED bind-pose units and shared by every entity spawned from that
  model path (`DynamicChainDetection.h`) — feeding a SCALED world position
  into `SolveDistanceConstraint()` against an UNSCALED `restLength` fights
  the solver against its own authored data, every single step. The
  `Mat4::Identity()` "degenerate scale" fallback is also a real regression
  risk in its own right — it silently teleports the whole simulation to the
  world origin for one frame instead of preserving the entity's actual
  position/rotation. **Fix:** Phase 3 (v2) now builds the physics-space
  matrix from `ComputeWorldTransform(registry, entity)`'s `position`/
  `rotation` fields ONLY (scale forced to `Vec3::One()`), which (a) makes
  the simulation exactly scale-invariant (an entity's chain simulates
  identically regardless of its own `Transform::scale` — proven by a new
  test), (b) makes the resulting matrix a rigid transform that is
  ALGEBRAICALLY NEVER singular, removing the need for (and the silent
  failure mode of) the old generic-inverse-with-Identity-fallback, and (c)
  is still fully correct visually — the FINAL render-time model matrix
  (`RenderSystem::CollectRenderables()`, untouched by this campaign) still
  applies the entity's real scale on top of the correctly-simulated,
  unscaled pose, exactly the same way it already applies scale to a
  non-physics mesh today.
- **Finding #2 (Phase 4 — Culprit F, the flicker bug):** while re-verifying
  Phase 4's own restructuring of `StepDynamicChainRange()` for the `frozen`
  feature, this audit discovered the SAME restructuring the original Phase 4
  already needed for its OWN feature (re-apply `state.particles` into the
  pose unconditionally, independent of whether any NEW integration step ran
  this call) is unconditionally REQUIRED for every ordinary, non-frozen rig
  too — see Step 2's new "Culprit F" writeup above for the full mechanism.
  The original Phase 4 document's own prose phrased this restructuring as
  something done "for a frozen rig," which — read literally — would let an
  implementer build a version that only skips the early-`continue` for
  `rig.frozen == true`, leaving the exact same flicker bug fully intact for
  every ordinary rig (the overwhelming common case). Phase 4 (v2) makes this
  fix explicit, universal, and mandatory, and adds a dedicated regression
  test that fails if a future change re-introduces the early `continue`
  for the ordinary (non-frozen) case. This is flagged as the single most
  important correction in this whole v2 pass: without it, this entire
  campaign's headline feature (a T-pose that visibly, continuously,
  believably simulates physics — Phase 1 through 5's entire point) would
  ship looking like a flicker/strobe effect on any display faster than
  60 Hz, which is most of them.
