# PHASE0 — MASTER STRATEGY: Verlet-Integrated Dynamic Bone Physics System (v4)

Orchestrator document for the `verlet-integration-1` campaign. Every child
phase document in this folder implements one slice of this plan. This
document is the single source of truth for **ordering, ownership, and the
identified root causes** — read this first, then execute
`PHASE1_...md` → `PHASE5_...md` in order. Each phase is a real, compilable,
testable increment; none of them are "just planning" — every phase produces
new/modified `.h`/`.cpp`/`CMakeLists.txt`/test files.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis + ordering. |
| `PHASE1_CORE_VERLET_PHYSICS_FOUNDATION.md` | New `src/Physics/` module: particles, Verlet integration, distance/goal constraints, procedural wind — pure, Tier-1-tested math with zero ECS/GPU dependency. |
| `PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md` | Converts a simulated particle chain back into `BoneLocalOffset` rotations that plug directly into the existing `Animation/` pipeline — the "physics ↔ skeleton" bridge. |
| `PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md` | (v4) Introduces the `ResolvedAnimationPose` ECS component as the ONLY hand-off between three genuinely independent stages `Game::Update()` now calls in order — `AnimationSystem::EvaluatePoses()` (Animation) → `PhysicsSystem::Update()` (Physics, a brand-new, standalone class with zero knowledge of motion sampling/IK/append) → `AnimationSystem::SkinAndUpload()` (Skinning) — plus the fixed-timestep accumulator and the new `DynamicChainRig` component carrying per-frame-persistent simulation state. |
| `PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md` | (v4) Global (world) vs. local (per-joint/per-model) parameter plumbing — now owned entirely by `PhysicsSystem` (`GlobalPhysicsSettings`, `DynamicChainRigCache`), never by `AnimationSystem` — auto-detection of physics-driven bone chains from already-imported PMX data (`Bone::deformAfterPhysics`, `PhysicsData::RigidBody`), and an Editor Inspector control surface. |
| `PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md` | Simple head/body collision, numerical-blow-up guards, Job System dispatch for many simultaneous chains. |

## Revision Notes (v2 — second-iteration self-audit)

This campaign's phase documents were re-read end-to-end against the CURRENT
source tree (not just cross-checked against each other) before any
implementation began, specifically hunting for gaps, incorrectness,
insufficiency, and missing pieces. Six concrete, verified findings came out
of that pass, each fixed directly in the phase document it belongs to (this
file needed no *content* change beyond this note — every Culprit A–E in
Step 2 below was independently re-verified against the live source tree,
line by line, and is still accurate):

1. **PHASE1 — `SolveDistanceConstraint` divide-by-zero guard.** v1 only
   special-cased "both particles pinned"; it never guarded the general case
   `a.inverseMass + b.inverseMass <= kEpsilon` (e.g. a future caller setting
   `inverseMass = 0` without also setting `pinned = true`), which divides by
   (near) zero. Fixed: an explicit guard independent of the `pinned` flags,
   matching this codebase's own "degrade gracefully instead of producing
   NaN" convention (`Vec3::Normalize()`).
2. **PHASE1 — `kTwoPi` duplication ambiguity resolved.** `Math/MathTypes.h`
   already defines `kTwoPi` (confirmed directly) — v1's hedge ("define
   locally, or reuse one if it already exists — check first") is now a
   direct instruction to reuse it; no ambiguity left for the implementer.
3. **PHASE2 — `stiffness`/`constraintIterations` parameter-coupling bug.**
   v1's `StepDynamicChain()` applied the GOAL constraint (`stiffness`) INSIDE
   the same per-iteration loop as the STRUCTURAL constraint
   (`constraintIterations`), which geometrically compounds the goal blend
   (`stiffness` applied N times pulls `1-(1-stiffness)^N` of the way to the
   target, not plain `stiffness`) — so tuning `constraintIterations` (meant
   to be a pure rod-rigidity/perf knob) would silently also change how
   "stiff" the simulated bone felt, coupling two parameters the brief (and
   Phase 1 itself) both promise are independent. Fixed: the goal constraint
   now runs exactly ONCE per fixed step, after every structural iteration,
   decoupling the two knobs completely.
4. **PHASE3 — multi-substep goal-target contamination (the most serious
   finding).** v1's `AnimationSystem.cpp` wiring recomputed
   `animatedJointWorldPositions` from `pose` INSIDE the per-substep `for
   (step...)` loop — but `pose` is exactly what `ApplyDynamicChainPhysicsToPose()`
   overwrites at the end of the PREVIOUS substep. Any frame running more than
   one fixed substep (routine well above 0fps — any real frame rate below
   ~60fps, not merely an extreme stall) would silently feed the goal
   constraint its own prior physics output instead of the pure animated FK
   target, defeating "Stiffness: keep the originally-animated shape" on
   exactly the frames most likely to need it. Fixed: the pure FK target
   positions (and root position) are now captured ONCE, before the substep
   loop begins, and reused unchanged for every substep that frame.
5. **PHASE4 — `DetectDynamicChains()` degenerate-root-bone guard.** A
   malformed/unusual rig whose physics-driven bone run starts at the
   skeleton's literal root (no non-`deformAfterPhysics` ancestor at all)
   would previously produce a chain with `rootBoneIndex == -1`; per
   `Animation/BoneChainResolver.h`'s own documented contract,
   `ComputeBoneWorldMatrix(..., -1)` silently returns `Mat4::Identity()`,
   which would anchor that chain to the WORLD origin instead of to the
   character. Fixed: such a chain is now explicitly discarded at detection
   time, with a dedicated regression test.
6. **All phases — missing `tests/CMakeLists.txt` taxonomy-comment updates.**
   Every existing Tier-1 test file in this codebase has a matching
   descriptive paragraph in `tests/CMakeLists.txt`'s own header comment
   (confirmed directly — every current entry has one); none of the five
   phase documents' "add this test file" instructions mentioned adding the
   matching paragraph. Fixed: each phase's own test-registration step now
   says so explicitly.

## Revision Notes (v3 — ECS-independence surgical correction)

A further review — done by reading the ACTUAL, current
`src/Game/Animation/AnimationSystem.h/.cpp` and `src/Game/Game.cpp` source
(not just cross-checking this campaign's own documents against each other)
— found that v2's Phase 3/4 design, while numerically/behaviorally correct,
did not deliver genuine ECS SYSTEM independence between animation and
physics: it wired Phase 2's bridge in as a block of code appended directly
inside `AnimationSystem::Update()`'s own per-animator loop, with
`AnimationSystem` itself owning the chain cache
(`m_chainRigCache`)/global settings (`m_globalPhysicsSettings`) and
`#include`-ing `src/Physics/` headers directly. That makes "animation" and
"physics" a single inseparable call, not two independently schedulable,
independently testable systems — the exact opposite of how every other pair
of systems in this engine relates (`RenderSystem`/`MeshInstantiationSystem`/
`AnimationSystem` each own their own state and are called as separate,
named steps from `Game::Update()`/`Game::Render()`, never nested inside one
another's private loop).

**Fixed by Phase 3 (v3 rewrite)**: a new ECS component,
`ResolvedAnimationPose`, becomes the ENTIRE hand-off contract between three
independent stages `Game::Update()` now calls in a fixed order:

```
AnimationSystem::EvaluatePoses(registry, dt)   // ANIMATION - samples motion, solves IK, applies
                                                // append, writes the result into ResolvedAnimationPose.
                                                // Zero knowledge physics exists.

PhysicsSystem::Update(registry, dt)            // PHYSICS - reads ResolvedAnimationPose's bone
                                                // translations, runs Verlet simulation, OVERWRITES
                                                // the physics-controlled bone entries in that SAME
                                                // component. Zero knowledge of motion sampling/IK/
                                                // append - it only ever reads finished bone translations.

AnimationSystem::SkinAndUpload(registry)       // Reads whatever ResolvedAnimationPose currently holds
                                                // (physics-adjusted or not - doesn't care which) and
                                                // does the actual vertex skin + GPU upload.
```

`PhysicsSystem` is a brand-new class (`src/Game/Physics/PhysicsSystem.h/.cpp`),
never `#include`d by `AnimationSystem`, and never itself `#include`ing
`AnimationPoseEvaluator.h`/`MotionSampler.h`/`IkSolver.h`/`AppendBoneSolver.h`/
`VertexSkinning.h`/`SkeletalAnimator.h`/`Renderer`/`Mesh`. `GlobalPhysicsSettings`
and `DynamicChainRigCache` (originally drafted in v2 as `AnimationSystem`
members) move to being owned by `PhysicsSystem` instead. This is a
documentation-only, pre-implementation correction — as of this revision,
`src/Physics/` and the ECS wiring described by Phases 1–5 have not yet been
implemented in the source tree, so no code migration is needed, only these
phase documents. Every Culprit A–E finding from v2 remains independently
verified and unaffected in substance — only WHO calls Phase 1/2's pure
functions changes (`PhysicsSystem::Update()`, never
`AnimationSystem::Update()`, which no longer exists as a single method at
all — see Phase 3's own v3 Revision Notice for the full rationale and
Phase 4/5's matching updates for ownership changes downstream).

## Revision Notes (v4 — generalized from hair-specific to generic dynamic-bone terminology)

This campaign was renamed end-to-end, before any further implementation,
to fix a naming/scope mismatch that had crept into every phase document:
**everything Phases 1–5 actually design and build is a generic,
completely hair-agnostic Verlet particle-chain physics system.** Nothing
under `src/Physics/`, `src/Game/Physics/`, or any new ECS component reads,
stores, names, or reasons about hair specifically anywhere — it only ever
knows about "particles," "chains," and "joints." The word "hair"/"ponytail"
had only ever entered this campaign's own class/file/document naming
because hair/ponytail secondary motion was the illustrative motivating
example in the original brief, never because the underlying simulation
needed that concept to exist. A system built to swing a chain of bones
under gravity, momentum, damping, stiffness, and wind is equally usable for
a hair strand, a tail, a cape corner, a rope, an antenna, a chain-link
prop, or anything else that reduces to "a linear run of bones with no
inherent knowledge of what it visually represents" — so this document, and
every child phase document, now says exactly that and nothing more
hair-specific. Concretely, this pass:

- Renamed every hair-specific identifier introduced BY this campaign to a
  generic equivalent (old → new): `HairPhysicsSystem` → `PhysicsSystem`;
  `HairPhysicsRig` (ECS component) → `DynamicChainRig`; `HairPhysicsRigCache`
  → `DynamicChainRigCache`; `HairChainDefinition` → `DynamicChainDefinition`;
  `HairJointSettings` → `DynamicJointSettings`; `HairChainRuntimeState` →
  `DynamicChainRuntimeState`; `HairChainSolver` → `DynamicChainSolver`;
  `HairPhysicsConstraints` → `ChainConstraints`; `HairChainDetection.h` /
  `DetectHairChains()` → `DynamicChainDetection.h` / `DetectDynamicChains()`;
  `HairChainDetectionDefaults` → `DynamicChainDetectionDefaults`;
  `StepHairChain()` → `StepDynamicChain()`; `ApplyHairChainPhysicsToPose()`
  → `ApplyDynamicChainPhysicsToPose()`; `RegisterHairPhysicsChains()` →
  `RegisterDynamicChains()`; `AttachHairPhysicsRigIfNeeded()` →
  `AttachDynamicChainRigIfNeeded()`.
- Reworded every phase document's prose to describe "a dynamic bone chain"
  (a generic, linear run of physics-simulated bones, root-pinned to the
  animated FK pose) instead of "a ponytail/hair chain" — no example use
  case is named anywhere in this campaign's own documentation any more; the
  system's job is to move bones believably under gravity/momentum/damping/
  stiffness/wind, full stop.
- `GlobalPhysicsSettings`, `WindField`/`WindSettings`, `VerletParticle`,
  `VerletIntegration`/`IntegrateParticle`, `FixedTimestepAccumulator`,
  `SphereCollider`, `ResolvedAnimationPose`, `Animation/BoneWorldMatrixQuery.h`'s
  `ComputeBoneWorldMatrix()`, and `BoneChainPhysicsResolver` were already
  generic, hair-free names in every prior revision — this pass leaves all of
  them unchanged.
- Pre-existing, REAL engine source that lives outside this campaign's own
  scope — `src/Assets/PhysicsData.h`'s `RigidBody`/`RigidBodyMotionType` and
  `src/Assets/SkeletonData.h`'s `Bone::deformAfterPhysics` — is untouched by
  this rename (this campaign only ever reads these fields, it does not own
  or define them) and every phase document continues to refer to them by
  their real, existing names, including verbatim quotes of their own source
  comments exactly where they were already quoted (e.g.
  `Bone::deformAfterPhysics`'s own doc comment literally reads
  *"DeformAfterPhysics (physics-driven 'jiggle' bones)"*, and
  `RigidBodyMotionType::Dynamic`'s own doc comment literally reads *"the bone
  should instead follow the SIMULATED rigid body (e.g. jiggle hair/skirt
  bones)"* — this campaign does not get to rename code it does not own, and
  a direct quote of an existing comment is reproduced verbatim, not
  paraphrased).
- No behavior, ordering, formula, parameter, or test coverage described by
  any phase changed in this pass — this is a pure naming/documentation
  generalization, applied uniformly and simultaneously across all five
  phase documents before any further implementation proceeds.

## Step 1: The Goal (Where are we going?)

Give GreatTamanaEngine a **from-scratch, bare-bone, C++, Verlet-integration**
based secondary-motion physics system that makes a chain of bones swing
naturally under gravity, momentum, and wind, while still tracking the
underlying keyframed animation — exactly the brief:

- **Damping** — how fast a simulated bone chain stops swinging (per-joint,
  local).
- **Stiffness** — how much a simulated bone chain tries to keep its
  originally-animated shape (per-joint, local — implemented as a "goal"
  pull-back-to-FK-target constraint, see Phase 1).
- **Weight** — how heavy a simulated bone chain feels (per-joint mass →
  inverse-mass in the Verlet solver, local).
- **Wind** — dynamic movement even when the character stands still
  (global — one wind field shared by the whole world/scene).

This must be implemented **the same way every other core subsystem in this
engine was implemented**: hand-rolled in C++ (no physics middleware — this
engine has explicitly never vendored Bullet, exactly the same "own the core
math" philosophy that already produced `src/Math/` instead of GLM and
`src/ECS/` instead of EnTT — see `AGENTS.md`, Coding Guidelines, and
`src/Assets/PhysicsData.h`'s own file comment: *"no actual physics simulation
happens anywhere in this engine yet ... no Bullet or any other physics
backend is vendored"*). Verlet integration is the correct bare-bone
technique for exactly this job: it is unconditionally stable for a
constrained particle chain, needs no explicit velocity state (velocity is
implicit in `position - previousPosition`), and constraint solving is a
handful of vector-algebra lines — no linear-algebra solver, no external
dependency, entirely in the spirit of this codebase's existing
`src/Math/Vec3.h`/`Mat4.h`/`Quat.h`.

## Step 2: The Situation / The Problem (Where are we now?)

A deep read of the current source tree (`src/Animation/`, `src/Assets/`,
`src/ECS/Components/`, `src/Game/Animation/`) turned up the exact root
causes — the "culprits" — that must each be addressed by a specific phase
below. This is not a greenfield problem: the engine already imports (from
`.pmx`) every piece of *data* a bone-chain physics system needs, but has
**zero** code anywhere that ever turns that data into motion.

1. **Culprit A — there is no physics simulation code anywhere in the
   engine, only physics *data*.** `src/Assets/PhysicsData.h` (`RigidBody`/
   `Joint`) is explicitly documented as *"DATA only — no actual physics
   simulation happens anywhere in this engine yet"*. `SkeletonData.h`'s
   `Bone::deformAfterPhysics` flag ("physics-driven 'jiggle' bones") is
   parsed by `PmxLoader.cpp` and then **never read by anything** — grep
   confirms no consumer exists. `RigidBodyMotionType::Dynamic`'s own doc
   comment literally says *"the bone should instead follow the SIMULATED
   rigid body (e.g. jiggle hair/skirt bones)"* — a simulation that has never
   been written. **Fixed by Phase 1** (the simulation itself) **and Phase 4**
   (wiring this exact existing data to select which bones get simulated).
2. **Culprit B — the animation pipeline has a fixed, closed 4-stage order
   with no room for a dynamics stage.** `Animation/AnimationPoseEvaluator.h`
   documents the pipeline as exactly `SampleAnimationPose() → SolveIkChains()
   → ApplyAppendInheritance() → ComputeSkinningMatrices()` and is explicit
   that this order is "correctness-critical" and must not be
   reordered/extended casually. A physics-driven bone is typically **never
   keyframed at all** in a `.vmd` (that's the entire point of
   `deformAfterPhysics` — the artist relies on physics, not hand-keying), so
   today it simply sits at bind pose, motionless, forever. **Fixed by
   Phase 3 (v3/v4)**, which reaches this "5th stage" via a new,
   independently-owned system (`PhysicsSystem`) rather than a 5th call
   inlined into `AnimationPoseEvaluator`/`AnimationSystem` itself — the
   existing four-call sequence stays byte-for-byte untouched and the new
   stage communicates with it ONLY through the `ResolvedAnimationPose` ECS
   component (`sample → IK → append → FK` writes it; `PhysicsSystem`
   separately, independently, may overwrite bones in it; skinning,
   separately, independently, reads it).
3. **Culprit C — nothing in the engine's animation/ECS layer has ever
   needed FRAME-TO-FRAME PERSISTENT MUTABLE state before.** Every existing
   `src/Animation/*` module is pure/stateless (a function of its current
   inputs only — see `AGENTS.md`'s Job System Phase 4 audit table, which
   explicitly classifies all of them `JOB-SAFE` for exactly this reason).
   `SkeletalAnimator` (`src/ECS/Components/SkeletalAnimator.h`) comes closest
   with its `frame` float, but that's a single scalar advanced by a formula,
   not physical state. **Verlet integration structurally requires**
   `position` *and* `previousPosition` to persist, unbroken, from one frame
   to the next — a genuinely new category of engine state. **Fixed by
   Phase 3**, which introduces the first stateful, per-entity, per-chain
   runtime buffer in the animation stack, modeled deliberately on
   `SkeletalAnimator`'s own "component stores plain data, logic lives in a
   free function/system" convention (`AGENTS.md`, ECS section).
4. **Culprit D — the per-frame update loop uses a variable, uncapped
   `deltaSeconds`.** `Game::Update(double deltaSeconds, ...)` feeds straight,
   real, frame-to-frame delta time into every system it calls, including
   (v3/v4) the future, independent `PhysicsSystem::Update(Registry&, double
   deltaSeconds)`. Position-Verlet integration is only stable/deterministic
   under a **fixed** timestep — a variable `dt` (a stutter, an Editor
   breakpoint, a slow asset load) fed directly into `IntegrateParticle()`
   would make the simulated chain jitter, stretch, or explode on any
   frame-time spike. **Fixed by Phase 3**, which adds a small fixed-timestep
   accumulator (the exact same "don't feed real frame delta straight into
   your integrator" lesson every physics engine's own manual teaches), owned
   entirely by `PhysicsSystem` — every other existing system/stage is
   untouched.
5. **Culprit E — the one existing helper this new feature critically needs
   is private and non-reusable.** `IkSolver.cpp`'s anonymous-namespace
   `ComputeBoneWorldMatrix()` (*"a single bone's CURRENT world matrix,
   re-derived fresh from `pose` every call"*) is **exactly** the primitive
   Phase 2's bone↔particle bridge also needs (to know where a chain's root
   bone currently sits in world space before simulating its children) — but
   it is `static`-scoped to `IkSolver.cpp` today. Re-deriving a second,
   independent copy of this cycle-guarded ancestor walk would directly
   violate this codebase's own explicit rule (`AGENTS.md`, "Skeletal
   Animation Pose Resolution": *"Never hand-roll a new cycle-guarded
   bone-ancestor-chain walk — use `Animation/BoneChainResolver.h`'s
   `ResolveBoneChain()`/`ResolveSingleBoneChain()` instead"*) and would be
   the exact "three independent, subtly different hand-rolled versions of
   this exact pattern" anti-pattern that file already had to be refactored
   out of once before. **Fixed by Phase 2**, which promotes this helper into
   a shared, public location.

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 5, strictly in order — each phase's code depends on the
previous phase's deliverables compiling and passing its own tests:

1. **Phase 1** builds the pure math core in a brand-new, always-compiled,
   ECS/GPU-free `src/Physics/` module (mirroring how `src/Jobs/` and
   `src/Profiling/` were bootstrapped as new top-level modules): particles,
   single-step Verlet integration, distance constraints, goal/stiffness
   constraints, and a deterministic procedural wind field. Fully
   Tier-1-tested, no engine wiring yet.
2. **Phase 2** builds the bridge from "a chain of already-simulated particle
   positions" to "a set of `BoneLocalOffset` rotations the existing
   `Animation/` pipeline already knows how to consume" — reusing (after
   promoting it, per Culprit E) the exact bone-world-matrix query IK solving
   already relies on.
3. **Phase 3** (v4) introduces the `ResolvedAnimationPose` ECS component, the
   fixed-timestep accumulator, and a brand-new, standalone `PhysicsSystem`
   — splitting `AnimationSystem::Update()` into `EvaluatePoses()`/
   `SkinAndUpload()` with `PhysicsSystem::Update()` sandwiched between
   them in `Game::Update()` — the first frame a simulated bone chain visibly
   swings.
4. **Phase 4** makes chain selection and every tunable parameter data-driven
   from data the engine already parses out of `.pmx` (no new file format),
   splits global-vs-local parameter ownership cleanly, and exposes live
   tuning through the existing Editor Inspector.
5. **Phase 5** hardens the feature: simple collision so a simulated chain
   doesn't clip through the head/shoulders, numerical safety clamps, and Job
   System dispatch so many simultaneously-animated chains don't become a
   single-threaded bottleneck.

## Step 4: What We Will NOT Do (Focus)

- We will **not** vendor Bullet or any other third-party physics engine —
  see `cmake/FetchSaba.cmake`'s own header comment and `PhysicsData.h`'s own
  comment: this has been a deliberate, standing engine decision, and a
  hand-rolled Verlet chain is a complete, sufficient solution for
  constrained-bone-chain secondary motion in general (it is not being asked
  to solve general rigid-body ragdoll physics).
- We will **not** attempt full PMX `Joint`/`RigidBody` 6-DOF constraint
  fidelity (springs, cone-twist limits, box/capsule-vs-box collision, etc.).
  Phase 4 reads `RigidBody`'s *mass/damping* fields opportunistically as a
  parameter *source* when convenient, but never builds a general constraint
  solver for every `JointType`.
- We will **not** touch `Animation/SkeletonPose.cpp`, `MotionSampler.cpp`,
  `IkSolver.cpp`'s solving logic, or `AppendBoneSolver.cpp`'s solving logic —
  these four stages stay byte-for-byte as they are; the new stage is
  strictly additive, appended after them.
- We will **not** build a general cloth/skirt (surface/mesh) simulation in
  this campaign — the data model (Phase 1/2) is a single, linear bone chain,
  reusable for any number of independent chains on the same model, but this
  campaign's own scope, tests, and tuning are all single-chain,
  linear-bone-run specific, never a 2D constraint mesh/cloth solver.
- We will **not** add a brand-new asset file format for authoring chains —
  Phase 4 deliberately reuses `SkeletonData`/`PhysicsData` fields the engine
  already extracts from `.pmx` today.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact structs/functions/signatures to write,
the exact existing call sites to touch, and the exact test files to add or
extend. Do not skip a phase's own test file — every new Tier-1-testable
module in this plan must land with its `tests/` counterpart in the same
change, per `AGENTS.md`'s own "Testability & Regression Safety" rule, which
this campaign inherits unmodified. Do not reorder the phases — Phase 3
cannot compile without Phase 2's bridge function signatures, and Phase 2
cannot compile without Phase 1's `VerletParticle`/constraint functions.
