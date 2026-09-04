# PHASE5 — Collision, Stability, and Performance Hardening (v4 — retargeted at `PhysicsSystem`, generic naming)

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 4 (real,
data-driven dynamic bone chains actually animating on a live model).
Modifies: `src/Physics/DynamicChainSolver.cpp`, `src/Game/Physics/PhysicsSystem.cpp`
(v3/v4 — never `AnimationSystem.cpp`, see Phase 3's own v3 Revision Notice).
Adds: `src/Physics/SphereCollider.h`, safety clamps inside the solver, and
an optional Job System dispatch path for many simultaneously-animated
chains. This is the last phase — it takes the now-functionally-complete
feature (as of Phase 4) from "works on the happy path" to "doesn't visibly
break under realistic/adversarial conditions."

## v4 Revision Notice (naming only, read this first)

The "v3 Revision Notice" immediately below is kept intact for history. v4
adds exactly one further kind of change: every hair-specific identifier
this phase references is renamed to a generic equivalent, per
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v4)" — `HairPhysicsSystem`
→ `PhysicsSystem`, `HairChainSolver` → `DynamicChainSolver`,
`HairChainDefinition` → `DynamicChainDefinition`, `HairChainRuntimeState` →
`DynamicChainRuntimeState`, `HairPhysicsConstraints` → `ChainConstraints`,
`ApplyHairChainPhysicsToPose` → `ApplyDynamicChainPhysicsToPose`,
`DetectHairChains` → `DetectDynamicChains`,
`kMinHairJointsToParallelize` → `kMinDynamicJointsToParallelize`. No
collision formula, safety clamp, or parallelism decision described below
changed — only names.

## v3 Revision Notice (retargeting — kept for history)

Every reference in v2 of this document to `AnimationSystem::Update()`'s own
"physics block" now means `PhysicsSystem::Update()`'s own body instead —
Phase 3's v3 rewrite moved dynamic-chain stepping out of `AnimationSystem`
entirely, into its own standalone class (`src/Game/Physics/PhysicsSystem.h/.cpp`).
The Job System sequencing rule this document's Step 2/3.4 relies on ("the
outer per-animator loop must stay strictly sequential because of shared GPU
mesh buffers") is a constraint on `AnimationSystem::SkinAndUpload()`'s own
loop specifically (the GPU-touching half) — `PhysicsSystem::Update()`
touches no GPU/Renderer/Mesh state at all, so this document's own
parallelism discussion (3.4) is scoped entirely within
`PhysicsSystem::Update()`'s per-entity loop, across one entity's own
independent chains, same as v2 intended, just inside a different,
now-independent function.

## Step 1: The Goal

Close the three remaining gaps a from-scratch Verlet dynamic-bone-chain
system always has once its bare-bone core (Phases 1–4) is working: (1) a
simulated chain clipping straight through the character's own head/
shoulders with no collision at all, (2) no numerical safety net against a
bad frame (huge `deltaSeconds`, a teleporting root bone, degenerate input
data) visibly exploding the chain into NaN/garbage positions, and (3) every
chain today being simulated strictly serially inside
`PhysicsSystem::Update()`'s per-entity loop — fine for one model, a
potential bottleneck for a scene with many simultaneously-animated
physics-bearing characters.

## Step 2: The Situation / The Problem

- Phase 1/2's constraint solver only ever pulls particles toward each other
  (distance) or toward an animated goal (stiffness) — nothing stops a
  simulated joint from passing straight through the character's own skull,
  which looks visibly wrong for a chain swinging quickly.
- Verlet integration, while unconditionally stable for well-formed input, is
  NOT immune to genuinely bad input: `DynamicChainSolver.cpp`'s lazy-init
  step (Phase 2, 3.4) seeds particles from the animated pose exactly once —
  if a chain's animated ROOT bone teleports a large distance in one frame
  (e.g. a model being repositioned by a script, or an Editor gizmo drag),
  the now-stale, far-away particle positions would whip toward the new root
  position at effectively infinite velocity on the very next step, since
  nothing currently guards against an implausibly large per-step
  displacement.
- `AnimationSystem::SkinAndUpload()`'s own outer per-animator loop must stay
  strictly sequential (Phase 3's own rule, inherited unmodified from
  existing engine-wide policy — see `AGENTS.md`'s Job System section) —
  that rule is about GPU-mesh-buffer-sharing specifically and does NOT
  apply to `PhysicsSystem::Update()` at all (which never touches a GPU
  buffer). Within `PhysicsSystem::Update()`'s own per-entity loop,
  nothing forbids parallelizing WORK *within* a single entity's own
  physics step, across that one model's own several INDEPENDENT
  chains (multiple dynamic bone chains on the same character share no data
  with each other and can safely run concurrently) — the same
  "batch/parallel-for is safe, cross-animator interleaving is not"
  distinction `AnimationSystem.cpp` already draws for CPU vertex skinning
  (see `Jobs::Dispatch()` there), applied here to `PhysicsSystem.cpp`
  instead.

## Step 3: The Plan

### 3.1 Simple sphere collision: `src/Physics/SphereCollider.h`/`.cpp` (new) - unchanged from v2

Bare-bone, no broad-phase, no generality — exactly enough to keep a
simulated bone chain off a character's own head:

```cpp
#pragma once
#include "VerletParticle.h"
#include "../Math/Vec3.h"

namespace gte {

// A single collision sphere in WORLD space (the caller - DynamicChainSolver,
// see 3.2 - is responsible for re-deriving this every step from the
// character's current animated head-bone world position; this struct
// itself carries no bone reference at all, keeping it exactly as pure/
// engine-data-free as every other Physics/ primitive).
struct SphereCollider {
    Vec3 center = Vec3::Zero();
    float radius = 0.0f;
};

// Pushes `particle.position` back out to `collider`'s surface along the
// center->particle direction if it has penetrated - a single, cheap
// Position-Based-Dynamics-style projection, run AFTER distance/goal
// constraints each iteration (see DynamicChainSolver.cpp's own ordering
// note in 3.2) so collision has the final say each pass. No-op for a
// pinned particle (matches every other constraint's own convention) or a
// non-positive radius. Does NOT modify particle.previousPosition - exactly
// like SolveDistanceConstraint/SolveGoalConstraint, so the position
// correction here contributes to (rather than erases) the particle's own
// implied velocity next step, giving the chain a visible "slide off the
// surface" response instead of simply freezing at the boundary.
void SolveSphereCollision(VerletParticle& particle, const SphereCollider& collider) noexcept;

} // namespace gte
```

Implementation: if `particle.pinned || collider.radius <= 0.0f` return;
`delta = particle.position - collider.center`; `distance = Length(delta)`;
if `distance >= collider.radius` return (not penetrating); if `distance <
kEpsilon`, push out along an arbitrary fixed axis (e.g. `Vec3::Up()`) to
avoid a divide-by-zero when the particle sits exactly on the center;
otherwise `particle.position = collider.center + (delta / distance) *
collider.radius`.

Add `tests/Physics/SphereColliderTests.cpp`: a particle fully outside the
sphere is untouched; a particle inside is projected exactly onto the
surface along the correct direction; a pinned particle is never moved; the
degenerate at-center case doesn't produce NaN.

### 3.2 Wire collision into `DynamicChainSolver.cpp`

Extend `DynamicChainDefinition` (Phase 2) with an optional collider reference —
LOCAL, per-chain data, since which collider a chain checks against is a
per-model/per-chain authoring decision:

```cpp
// Added to DynamicChainDefinition (src/Physics/DynamicChainDefinition.h):
bool hasHeadCollider = false;
std::int32_t headColliderBoneIndex = -1; // the bone this chain's collision sphere should track (e.g. the head bone).
float headColliderRadius = 0.0f;         // world-space radius - authored once (see 3.4), not derived automatically.
```

In `StepDynamicChain()` (`DynamicChainSolver.cpp`), when `hasHeadCollider` is
true: derive the collider's current world center via
`Animation/BoneWorldMatrixQuery.h`'s `ComputeBoneWorldMatrix()` against the
SAME `pose`/`skeleton` the caller already resolved this frame (the caller —
v3: `PhysicsSystem::Update()`, via its own `DynamicChainRigCache::ModelEntry::skeleton`
copy — must therefore resolve the collider center itself and pass a plain
`SphereCollider` value into `StepDynamicChain()`, keeping `DynamicChainSolver.h`'s
own signature free of a `SkeletonData`/`pose` dependency, consistent with
Phase 2's original design intent that this file stay pure position/particle
math). Call `SolveSphereCollision()` for every joint particle as the LAST
step of each constraint-iteration pass (after distance, after goal —
collision must have final say, matching PBD convention: structural, then
soft/goal, then hard collision).

### 3.3 Numerical safety clamps - unchanged from v2

Two small, cheap guards added directly inside `DynamicChainSolver.cpp`'s
`StepDynamicChain()`:

1. **Root-teleport guard**: if `Length(rootWorldPosition -
   <the root position used last call, stored as a new
   DynamicChainRuntimeState::lastRootWorldPosition field>) > maxPlausibleRootDelta`
   (a small new `DynamicChainDefinition` field, e.g. defaulting to something
   generous like several times the chain's own total rest length), treat
   this call exactly like the lazy-init case (Phase 2, step 1) — re-seed
   every particle to its `animatedJointWorldPositions[i]` with zero implied
   velocity — rather than integrating a spurious, implausibly large
   displacement. In EITHER case — whether this call re-seeded because of a
   teleport or ran the ordinary integrate+constrain path — `state.lastRootWorldPosition`
   MUST be set to this call's own `rootWorldPosition` before `StepDynamicChain()`
   returns, exactly once per call; forgetting this turns the guard into a
   permanent, one-shot trip.
2. **NaN/Inf guard**: after integration + constraints, if any particle's
   `position` fails `std::isfinite()` on any component, reset that ONE
   particle (not the whole chain) to its corresponding
   `animatedJointWorldPositions[i]` with zero implied velocity, and (in a
   debug/development build only) fire an assert so a real underlying bug is
   caught loudly during development rather than silently, permanently,
   corrupting that one joint for the rest of the session.

Add to `tests/Physics/DynamicChainSolverTests.cpp`: a root position that
jumps by an implausible amount between two calls does NOT produce a visible
"whip"; a THIRD call, immediately after the teleport-triggering second
call, with the root held stationary at its new position, must NOT
re-trigger the guard again; manually poisoning a particle's position with
`NaN` before a call results in a finite, sane position after the call
returns.

### 3.4 Performance: parallelize INDEPENDENT chains within one entity (v3/v4 — inside `PhysicsSystem.cpp`, never `AnimationSystem.cpp`)

In `PhysicsSystem.cpp`'s `Update()` (Phase 3/4), when an entity has more
than one detected chain (e.g. several independent dynamic bone chains on
the same model), dispatch the PER-CHAIN `StepDynamicChain()` +
`ApplyDynamicChainPhysicsToPose()` work across `gte::Jobs::Dispatch()`,
mirroring the EXACT existing `SkinningBatchContext`/`RunSkinningBatch()`
pattern already established in `AnimationSystem.cpp` (see `AGENTS.md`'s Job
System Phase 6 section) — with one correctness-critical difference:
`ApplyDynamicChainPhysicsToPose()` mutates the SHARED
`ResolvedAnimationPose::pose` vector in place, so two chains' own bone index
sets must be guaranteed DISJOINT (true by construction — Phase 4's
`DetectDynamicChains()` never lets one bone belong to two different chains)
and, even so, each chain's own job body must only ever write the specific
`pose[boneIndex]` entries its OWN `jointBoneIndices` names — never assume
it's safe to touch anything else. Gate this behind the same
`kMinVerticesToParallelize`-style threshold philosophy already established
in `AnimationSystem.cpp`: only dispatch when an entity has enough TOTAL
joints across all its chains to be worth the Job System's own per-
`Dispatch()` overhead (a new, small `constexpr kMinDynamicJointsToParallelize`
inside `PhysicsSystem.cpp`'s own anonymous namespace, e.g. 24), otherwise run
every chain serially, inline, exactly as Phase 3/4 already do. Because
`PhysicsSystem::Update()` iterates entities in its OWN loop (over
`ComponentStorage<DynamicChainRig>`, not `ComponentStorage<SkeletalAnimator>`),
and touches no GPU/Mesh state, this per-entity loop itself has no "must stay
sequential" constraint either — only the per-chain `Dispatch()`/
`WaitForJobs()` bracket within one entity's own processing needs the same
care `AnimationSystem.cpp`'s own skinning dispatch already takes (one
`WaitForJobs()` completes before the next entity's own chains begin, to
keep this phase's own scope minimal — a future phase could revisit
cross-entity concurrency here specifically, since unlike GPU mesh buffers,
no two entities' own `ResolvedAnimationPose` components ever alias the same
memory; not attempted in this phase).

### 3.4.1 Update `AGENTS.md`'s Job System thread-safety audit table

`AGENTS.md`'s own Job System section ends its classification table with an
explicit standing instruction: *"a future phase that needs to classify
something not listed here should add a new row rather than assume an
unlisted subsystem is safe by omission."* This phase is the FIRST point in
the whole `verlet-integration-1` campaign where any `src/Physics/` code
(and the shared `ResolvedAnimationPose::pose` vector) can actually run
INSIDE a `Jobs::Dispatch()` job body — Phases 1–4 only ever call these
functions inline, on the main thread, strictly sequentially. Add two new
rows to that table (next to the existing "Pure `src/Animation/*` modules"
row, which this closely mirrors):

- **`src/Physics/*` (`VerletIntegration`, `ChainConstraints`,
  `WindField`, `DynamicChainSolver`, `BoneChainPhysicsResolver`,
  `SphereCollider`, `FixedTimestepAccumulator`)** — **JOB-SAFE**, for the
  exact same reason the existing "Pure `src/Animation/*` modules" row is:
  every function is pure logic over only its own parameters, no static/
  global/singleton mutable state anywhere in the module, safe to call
  concurrently from any number of threads PROVIDED each individual call's
  own inputs/outputs (one chain's own `DynamicChainRuntimeState`) are never
  shared/aliased across two concurrent calls — true by construction here,
  since Phase 4's `DetectDynamicChains()` guarantees disjoint chains and
  this phase's own 3.4 dispatches exactly one job per chain, from
  `PhysicsSystem.cpp` (v3/v4 — never `AnimationSystem.cpp`).
- **Concurrent, DISJOINT-INDEX writes into ONE shared
  `ResolvedAnimationPose::pose` (`std::vector<BoneLocalOffset>`), from
  several job bodies at once (this phase's own 3.4)** — a NEW pattern, not
  covered by the existing `Registry`/ECS "mutation: NEVER" row above it
  (that row is about the ECS `Registry`/`ComponentStorage<T>`
  specifically, not the CONTENTS of one already-fetched component's own
  `std::vector` field) — **JOB-SAFE**, conditioned on TWO invariants this
  campaign's own design guarantees and any future edit must preserve: (1)
  `pose`'s SIZE is fixed and never resized/reallocated for the duration of
  the `Dispatch()`/`WaitForJobs()` bracket (no `push_back`/`resize` call
  anywhere inside a job body), and (2) every two concurrently-dispatched
  chains' own `jointBoneIndices` sets are provably DISJOINT (Phase 4's
  `DetectDynamicChains()` guarantee) — writing to genuinely disjoint indices
  of one fixed-size `std::vector` from different threads at once is safe
  (no reallocation, no false sharing of the SAME element), the same
  reasoning `Jobs::Dispatch()`'s own per-batch output-span writes already
  rely on for `skinnedPositions`/`skinnedNormals` in the existing CPU
  vertex-skinning row. Note this row is scoped to ONE entity's own `pose`
  at a time — `PhysicsSystem::Update()`'s own outer per-entity loop
  (3.4) stays serial in this phase, so no two entities' own `pose` vectors
  are ever touched concurrently either.

Add this to `AGENTS.md`'s "Job System" section as part of this phase's own
implementation work — not a documentation-only afterthought landed later.

### 3.5 Tests for this phase

- `tests/Physics/SphereColliderTests.cpp` — see 3.1.
- Extend `tests/Physics/DynamicChainSolverTests.cpp` with the collision,
  teleport-guard, and NaN-guard cases from 3.2/3.3.
- A new `tests/Game/Physics/PhysicsSystemParallelTests.cpp` (v3/v4 — moved
  from `tests/Game/Animation/`, Tier 1, mirroring
  `tests/Animation/VertexSkinningParityTests.cpp`'s own precedent exactly):
  a synthetic model with several independent dynamic bone chains, driven
  through `PhysicsSystem::Update()` directly against a hand-built
  `ResolvedAnimationPose` component (no `AnimationSystem`/`SkeletalAnimator`
  involved at all — further proof of independence), produces
  BYTE-IDENTICAL results whether `kMinDynamicJointsToParallelize` forces the
  serial path or the `Jobs::Dispatch()` path.
- Add a matching descriptive paragraph for the new `SphereColliderTests.cpp`
  and `PhysicsSystemParallelTests.cpp` files, and update the
  `DynamicChainSolverTests.cpp` entry, in `tests/CMakeLists.txt`'s own header
  "Test taxonomy" comment block, in the same style/level of detail as every
  existing entry there.

## Step 4: What We Will NOT Do

- We will **not** build general capsule/box/mesh collision — one sphere per
  chain (tracking one collider bone, typically the head) is enough for the
  secondary-motion use case this campaign targets; a more general
  body-vs-limbs capsule system is explicitly out of scope and would be its
  own follow-up campaign.
- We will **not** build chain-vs-chain (self) collision or chain-vs-world
  (level geometry) collision — only chain-vs-one-authored-sphere.
- We will **not** parallelize ACROSS different entities' own
  `DynamicChainRig` processing in this phase — 3.4's parallelism is strictly
  WITHIN one entity's own per-frame physics block, across that one
  model's own independent chains only (see 3.4's own closing note on why a
  future phase, not this one, may reconsider this).
- We will **not** add a general "chain sleeps when off-screen" LOD/culling
  system in this campaign — a reasonable, natural follow-up once this
  feature is shipped and profiled with real scenes, but speculative
  optimization without a measured need is explicitly against this engine's
  own stated philosophy.
- **(v3)** We will **not** reintroduce any physics-stepping logic into
  `AnimationSystem.cpp` while implementing this phase's parallelism work —
  every change in this phase's own 3.2/3.4 lands inside
  `src/Physics/DynamicChainSolver.cpp`/`src/Game/Physics/PhysicsSystem.cpp`
  only.

## Step 5: Their Role

1. Implement and test `SphereCollider.h`/`.cpp` (3.1) in isolation first.
2. Wire collision into `DynamicChainSolver.cpp` (3.2), extending
   `DynamicChainDefinition` with the three new fields, and add authoring for
   `headColliderBoneIndex`/`headColliderRadius` to Phase 4's
   `DetectDynamicChains()` (a reasonable default: the chain's own
   `rootBoneIndex`'s bone with a small heuristic radius, or left disabled —
   `hasHeadCollider = false` — until a human tunes it via the Inspector,
   extending Phase 4's own Inspector section, 3.5 there, with a new field).
3. Add the root-teleport and NaN/Inf guards (3.3) — cheap, defensive, and
   should be added regardless of whether a real teleport bug has been
   observed yet.
4. Add the `Jobs::Dispatch()` parallel path (3.4) LAST, inside
   `PhysicsSystem.cpp`, only after confirming (by simply running the
   engine) that a realistic scene actually has enough simultaneous dynamic
   chains for this to matter. Land the `AGENTS.md` audit-table update
   (3.4.1) in the SAME change as this step, never as a separate follow-up.
5. Once this phase's tests are green, the `verlet-integration-1` campaign is
   functionally complete: run the full `GreatTamanaEngineTests` suite one
   final time end-to-end before considering the campaign done. Confirm, by
   grep, that `AnimationSystem.h`/`.cpp` still contain no `#include` of
   anything under `src/Physics/` and no reference to `PhysicsSystem`,
   `DynamicChainDefinition`, `DynamicChainRuntimeState`, `DynamicChainSolver`,
   or `BoneChainPhysicsResolver` — the final, checkable proof that this
   whole campaign's v3 ECS-independence goal held all the way through.
