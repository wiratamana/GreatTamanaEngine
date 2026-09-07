# PHASE3 — World-Space, Root-Motion-Aware Dynamic Chain Simulation — v2

Part of the `verlet-integration-7` campaign — read `PHASE0_MASTER_STRATEGY.md`
first. This is the deepest fix in the campaign, and the one that directly
satisfies the user's own concrete acceptance test: *"i want to hair or skirt
get physically move by simulation if i drag model position left and right
without to actually run animation on it."* This phase fixes **Culprit C**.

**v2 notice:** this revision replaces the original plan's "compose the
entity's FULL world matrix (including scale), then take a generic inverse
with an Identity fallback" approach with a **scale-free (rotation +
translation only) physics-space matrix**. See `PHASE0_MASTER_STRATEGY.md`'s
"Revision Notes (v2)", Finding #1, for the full rationale. Everything else in
this phase (the world-space conversion itself, the teleport-guard
interaction, the overall test list) is unchanged from the original plan and
re-verified accurate against the current source tree.

## Step 1: The Goal (Where are we going?)

Translating or rotating a spawned model's own ECS `Transform` (directly, or
through a parent chain) must produce genuine Verlet **inertial lag** in every
one of its simulated dynamic chains — the hair/skirt visibly takes a moment
to catch up to the body's new position/orientation, exactly like it would in
reality — whether or not the model is animated. An entity with no `Transform`
component at all (or an explicit identity one) must keep producing EXACTLY
the same simulated result as today, byte-for-byte — this phase must not
regress a single existing test.

**(v2)** This must ALSO hold true regardless of the entity's own
`Transform::scale`: a chain simulated on a model spawned at `scale = (2,2,2)`
must produce the exact same `ResolvedAnimationPose::pose` (bone-local
offsets) as the identical chain on the same model spawned at
`scale = (1,1,1)` — the simulation itself is scale-invariant; only the
FINAL rendered geometry differs, because `RenderSystem`'s own untouched
model-matrix multiply (which already includes the real scale) is applied
afterward, exactly as it already is for a non-physics mesh today.

## Step 2: The Situation / The Problem (Where are we now?)

`PhysicsSystem::Update()`'s inner stepping helper,
`StepDynamicChainRange()` (`src/Game/Physics/PhysicsSystem.cpp`, lines
69-114), computes every position it feeds the Verlet solver via
`ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex)`
(`Animation/BoneWorldMatrixQuery.h`):

```cpp
const Mat4 rootWorld = ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.rootBoneIndex);
const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());
...
animatedJointWorldPositions.push_back(
    ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex).TransformPoint(Vec3::Zero()));
...
collider.center = ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.headColliderBoneIndex)
                       .TransformPoint(Vec3::Zero());
```

`ComputeBoneWorldMatrix()`'s own doc comment and implementation
(`Animation/BoneChainResolver.h`'s `ResolveSingleBoneChain()`) is explicit:
this function ONLY EVER walks `SkeletonData::Bone::parentBoneIndex` — it
takes no `Registry&`, has no notion an owning ECS entity or its `Transform`
even exists. Every name in this file says "world" (`rootWorldPos`,
`animatedJointWorldPositions`, `lastRootWorldPosition`) but every one of
these values is actually in **bone-local "model space"** — coincidentally
identical to true world space only when the owning entity's own resolved
Transform happens to be the identity matrix.

Meanwhile `RenderSystem::CollectRenderables()` (`src/Game/RenderSystem.cpp`,
lines 8-33) computes, for each mesh-part entity:

```cpp
const Mat4 model = ComputeWorldMatrix(registry, entity); // ECS/TransformHierarchy.h - walks the REAL Transform parent chain, TRS (includes scale).
commands.push_back(DrawCommand{ meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
```

— applied as one single rigid matrix multiply in the vertex shader, entirely
downstream of CPU skinning/physics. The direct, observable consequence:
translating a spawned model's `Transform.position` moves every vertex —
including every hair/skirt tip — by exactly that rigid delta, in the very
same render frame, with **zero lag**, because `PhysicsSystem` never learns
the entity moved at all. This is true for an animated model just as much as
a T-pose one — it is a strictly separate bug from Culprits A/B, and would
still exist even with Phases 1-2 fully landed.

`ECS/TransformHierarchy.h` already exposes exactly what is needed —
**deliberately its `ComputeWorldTransform()` overload, not
`ComputeWorldMatrix()`** (v2 — see Step 3.1 below for why):

```cpp
Mat4 ComputeWorldMatrix(Registry& registry, Entity entity);       // full TRS, includes scale - do NOT use this for physics.
Transform ComputeWorldTransform(Registry& registry, Entity entity); // decomposed position/rotation/scale - USE position+rotation only.
```

`ComputeWorldTransform()` returns a default-constructed `Transform`
(`position = Zero()`, `rotation = Identity()`, `scale = One()`) for an entity
with no `Transform` component at all (own doc comment) — which is precisely
what guarantees this phase cannot regress any existing Transform-less test
fixture, exactly like the original plan's reliance on `ComputeWorldMatrix()`'s
own identical fallback.

**(v2) Why scale must be excluded from the physics-space matrix:**
`DynamicChainDefinition::restLengths`, `headColliderRadius`, and
`maxPlausibleRootDelta` (`Physics/DynamicChainDefinition.h`) are each
precomputed exactly ONCE, at model-registration time
(`DynamicChainDetection.h`), in the model's own UNSCALED bind-pose units —
and are SHARED by every entity ever spawned from that same model path
(`DynamicChainRigCache::ModelEntry`, keyed by path, not by entity). If the
matrix used to convert bone-local positions into "world space" for physics
purposes includes the SPAWNED ENTITY's own `Transform::scale` (which is
per-instance, not per-model, and very much NOT `Vec3::One()` for e.g. a
deliberately shrunk/enlarged prop placed via the Editor's transform gizmo),
then `ChainConstraints.h::SolveDistanceConstraint()` — which pulls two
particles toward exactly `restLength` apart, in whatever space it's called
in — would be fighting to hold SCALED world distances at UNSCALED rest
lengths, every single relaxation iteration, every single step: a chain on a
model scaled up 2x would visibly appear to be under 2x more tension/
compression than intended (its rest lengths look "too short" relative to its
own scaled body), and a chain on a model scaled down would look "too slack."
`maxPlausibleRootDelta`'s teleport-guard threshold (an absolute distance,
also unscaled) would likewise misfire differently for a scaled-up vs.
scaled-down instance of the exact same drag gesture. None of this is
hypothetical or a corner case this codebase already guards against — nothing
downstream of `DynamicChainDetection.h` has ever had to reason about a
per-instance scale before, because `PhysicsSystem` never touched `Transform`
at all until this very phase.

## Step 3: The Plan (How will we get there?)

### 3.1 — `src/Game/Physics/PhysicsSystem.cpp`: resolve a SCALE-FREE world matrix once per entity, per frame

Add `#include "../../ECS/TransformHierarchy.h"` and
`#include "../../ECS/Components/Transform.h"`.

In `PhysicsSystem::Update()`'s existing per-rig loop (lines 198-250), right
after `const Entity entity = rigs.EntityAt(i);` and the existing
`ResolvedAnimationPose* resolvedPose = ...` null-check, add:

```cpp
// task_manager/verlet-integration-7, Phase 3 (v2) - the owning entity's
// REAL, fully-resolved world POSITION and ROTATION (walking its whole ECS
// parent chain via ECS/TransformHierarchy.h - the SAME underlying data
// RenderSystem::CollectRenderables() uses to place the rendered mesh),
// deliberately EXCLUDING scale - see PHASE0_MASTER_STRATEGY.md's Revision
// Notes (v2), Finding #1, for the full rationale: every chain's own
// restLengths/headColliderRadius/maxPlausibleRootDelta is precomputed once
// in UNSCALED bind-pose units and shared by every entity spawned from the
// same model path, so baking a per-instance Transform::scale into the
// matrix the solver simulates in would desync the solver's own authored
// rest data from the world distances it actually sees. `scale` is still
// applied, correctly, entirely downstream and untouched by this phase - see
// RenderSystem::CollectRenderables()'s own unmodified full-TRS model matrix.
//
// Resolved ONCE per entity, per frame, here on the main thread (never
// inside the per-chain parallel Dispatch() below) - every chain this entity
// owns reads the SAME already-resolved matrix by const reference, so this
// is exactly as safe under the existing parallel-dispatch path as
// `pose`/`skeleton` already are.
const Transform entityWorldTransform = ComputeWorldTransform(registry, entity);
const Mat4 entityWorldMatrix = Mat4::TRS(entityWorldTransform.position, entityWorldTransform.rotation, Vec3::One());

// A pure rotation+translation matrix (unit scale, and Mat4::FromQuat() of a
// normalized quaternion is always orthonormal) is ALGEBRAICALLY NEVER
// singular - unlike the original v1 plan's generic-inverse-with-Identity-
// fallback (which could silently teleport a live simulation to the world
// origin for a frame on an ordinary SCALED entity), TryInverse() here is
// expected to ALWAYS succeed for every normal input. It is kept (rather
// than the asserting Inverse()) purely as cheap, unconditional, debug-only-
// asserting insurance against a theoretically-malformed (e.g. non-
// normalized) input quaternion reaching this far - a case this codebase has
// no evidence can actually happen today, so the Identity() fallback below
// is genuinely last-resort/should-never-trigger territory, not a normal
// code path this phase relies on (contrast with v1's fallback, which WOULD
// trigger routinely for any merely-scaled entity).
Mat4 entityWorldMatrixInverse;
if (!entityWorldMatrix.TryInverse(entityWorldMatrixInverse)) {
    assert(false && "PhysicsSystem: entity world (rotation+translation) matrix was singular - "
                     "should be algebraically impossible; check for a non-normalized Transform::rotation.");
    entityWorldMatrixInverse = Mat4::Identity();
}
```

Add both fields to `DynamicChainBatchContext` (anonymous namespace, top of
`PhysicsSystem.cpp`):

```cpp
struct DynamicChainBatchContext {
    const SkeletonData* skeleton;
    const std::vector<DynamicChainDefinition>* chains;
    std::vector<DynamicChainRuntimeState>* chainStates;
    std::vector<BoneLocalOffset>* pose;
    int stepCount;
    float fixedTimestepSeconds;
    Vec3 gravity;
    WindSettings wind;
    Mat4 entityWorldMatrix;         // Phase 3 (v2) - rotation + translation ONLY, scale excluded. See this file's own header comment above.
    Mat4 entityWorldMatrixInverse;  // Phase 3 (v2) - inverse of the above.
};
```

...and pass them through at the existing `DynamicChainBatchContext context{
...}` construction site (line 222).

### 3.2 — `StepDynamicChainRange()`: simulate in true (scale-free) world space, convert back before writing the pose

Compose `context.entityWorldMatrix` with each bone-local matrix BEFORE
extracting a position, for the root, every joint, and the collider:

```cpp
const Mat4 rootWorld = context.entityWorldMatrix
    * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.rootBoneIndex);
const Vec3 rootWorldPos = rootWorld.TransformPoint(Vec3::Zero());

std::vector<Vec3> animatedJointWorldPositions;
animatedJointWorldPositions.reserve(chain.jointBoneIndices.size());
for (std::int32_t boneIndex : chain.jointBoneIndices) {
    const Mat4 jointWorld = context.entityWorldMatrix
        * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, boneIndex);
    animatedJointWorldPositions.push_back(jointWorld.TransformPoint(Vec3::Zero()));
}

SphereCollider collider;
bool hasCollider = false;
if (chain.hasHeadCollider) {
    const Mat4 colliderWorld = context.entityWorldMatrix
        * ComputeBoneWorldMatrix(*context.skeleton, *context.pose, chain.headColliderBoneIndex);
    collider.center = colliderWorld.TransformPoint(Vec3::Zero());
    collider.radius = chain.headColliderRadius; // Unaffected by scale, same as before this phase - see PHASE0's "What We Will NOT Do".
    hasCollider = true;
}
```

`StepDynamicChain()` itself (`Physics/DynamicChainSolver.cpp`) is
**completely untouched** — it has no idea what coordinate space its inputs
are expressed in; it only needs internal consistency across calls, which is
preserved here (every position fed to it is now consistently TRUE,
scale-free world space, every call, forever, for this
`DynamicChainRuntimeState`'s whole lifetime — `VerletParticle::position`/
`previousPosition` are therefore now genuinely WORLD-space (rotation +
translation only) quantities; update `Physics/VerletParticle.h`'s own doc
comment and `Physics/DynamicChainRuntimeState.h`'s `lastRootWorldPosition`
doc comment to say so explicitly, including the scale-free clarification).

After the per-step `StepDynamicChain()` call, when converting the resulting
`state.particles` positions to feed `ApplyDynamicChainPhysicsToPose()`
(which itself is **completely untouched** — it only ever operates in
bone-local/model space, exactly like every other bone in `pose`), convert
each simulated WORLD position back into model-local space first:

```cpp
std::vector<Vec3> simulatedPositions;
simulatedPositions.reserve(state.particles.size());
for (const VerletParticle& particle : state.particles) {
    simulatedPositions.push_back(context.entityWorldMatrixInverse.TransformPoint(particle.position));
}
ApplyDynamicChainPhysicsToPose(*context.skeleton, chain, simulatedPositions, *context.pose);
```

### 3.3 — The teleport-guard interaction (must be explicitly verified, not just assumed)

`DynamicChainDefinition::maxPlausibleRootDelta`/
`DynamicChainRuntimeState::lastRootWorldPosition` (`StepDynamicChain()`,
`Physics/DynamicChainSolver.cpp`, lines 59-73) already compares consecutive
calls' root positions and re-seeds every particle if the delta is
implausibly large — this logic is **completely untouched** by this phase,
but it now operates on the NEW, composed true-world-space (scale-free) root
position instead of model-local. Because scale is deliberately excluded
(v2), this threshold now behaves IDENTICALLY regardless of the entity's own
`Transform::scale` — a real fix over the original v1 plan, where a
scaled-up instance would have tripped this guard more easily (and a
scaled-down one less easily) for the exact same drag gesture:

- A genuine one-frame **teleport** (e.g. an Editor "reset transform" button,
  a scene reload) must still trip this guard and cleanly re-seed — no
  regression.
- A smooth, continuous **drag** (a mouse-move-driven Transform update, one
  small delta per rendered frame) must stay comfortably under
  `maxPlausibleRootDelta`'s existing generous default (10.0f) or
  `DynamicChainDetection.h`'s own per-chain override (scaled from the
  chain's own combined rest length) — meaning it will NOT trip the re-seed
  guard, and will therefore show genuine, visible per-frame lag, exactly the
  behavior being added. This needs no code change — it is a direct,
  automatic consequence of 3.1/3.2 above — but it MUST be covered by an
  explicit regression test (3.4, test 2) proving both ends of this
  interaction hold, since a silent regression here (e.g. a future change that
  makes `maxPlausibleRootDelta` too small) would quietly defeat this whole
  phase's purpose without breaking any existing test.

### 3.4 — Tests: new file `tests/Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp`

A new, dedicated Tier-1 test file (plain `Registry`, mirrors
`PhysicsSystemTests.cpp`'s own synthetic 4-bone rig fixture construction
style exactly — reuse the same `Static` anchor + two `Dynamic`-body,
perpendicular-to-gravity chain shape):

1. `EntityTransformTranslationProducesInertialLagInSimulatedChain` — spawn
   the synthetic chain, attach a real `Transform` component
   (`registry.AddComponent<Transform>(entity)`, left at the identity), let it
   settle under gravity for ~60 frames (mirrors the existing settle-then-
   assert pattern), record the tip joint's reconstructed world position
   (`entityWorldMatrix * ComputeBoneWorldMatrix(...)`, computed the same way
   the production code now does, from the test itself). Then, in ONE
   subsequent frame, set `transform->position += Vec3(2.0f, 0.0f, 0.0f)` (well
   under the default `maxPlausibleRootDelta`) and call `Update()` exactly
   once more. Assert the tip's NEW reconstructed world position has moved by
   **strictly less than** exactly `(2.0f, 0.0f, 0.0f)` relative to its
   pre-drag position (i.e. it lagged behind the rigid root delta) — the
   direct, literal, automated version of the user's own drag-test.
2. `ASequenceOfSmallContinuousTransformDeltasNeverTripsTheTeleportGuardWhileALargeSingleFrameJumpStillDoes`
   — (a) apply ~10 small per-frame Transform position deltas (e.g. 0.05
   units each, at 60 fps — a plausible mouse-drag speed) and assert the chain
   never snaps discontinuously (no single-frame jump in the simulated tip's
   world position anywhere close to the full accumulated delta, proving no
   re-seed fired); (b) in a FRESH scenario, apply one single-frame delta far
   beyond `maxPlausibleRootDelta` (e.g. `Vec3(500, 0, 0)`) and assert the
   chain re-seeds cleanly (no NaN/Inf; the tip's new position lands
   consistently near the NEW animated/bind target, proving the safety net
   still works, unregressed).
3. `IdentityOrMissingTransformProducesByteIdenticalResultsToPreWorldSpaceBehavior`
   — run the SAME synthetic-chain-under-gravity scenario twice: once with NO
   `Transform` component on the entity at all, once with an explicit,
   untouched default `Transform` (`position = Zero()`, `rotation =
   Identity()`, `scale = One()`); assert both produce identical
   `ResolvedAnimationPose::pose` results after N steps, via
   `ApproximatelyEqual`/`RepresentSameRotation` — this is the hard proof that
   `ComputeWorldTransform()`'s own identity fallback keeps this phase fully
   backward-compatible with every pre-existing test.
4. `ATransformParentedUnderAMovingAncestorStillProducesCorrectlyComposedWorldSpaceSimulation`
   — parent the physics entity's `Transform` under a second "vehicle" entity
   (`TransformHierarchy.h::SetParent()`), move the VEHICLE's own Transform
   across several frames, and assert the chain's simulated shape reacts to
   the vehicle's motion exactly as if the physics entity's own Transform had
   moved by the same fully-resolved amount (proving `ComputeWorldTransform()`'s
   full recursive parent-chain walk, not just a one-level lookup, is what
   feeds this phase's math).
5. **(v2, new)** `EntityTransformScaleNeverAffectsTheSimulatedPoseOrTheTeleportGuardThreshold`
   — run the SAME synthetic-chain-under-gravity-and-drag scenario (test 1's
   own scenario, verbatim) THREE times, on three separate entities built from
   the exact same `DynamicChainDefinition`/registered model: once with
   `transform->scale = Vec3::One()`, once with `Vec3(2.0f, 2.0f, 2.0f)`, once
   with `Vec3(0.25f, 0.25f, 0.25f)`. Assert all three produce IDENTICAL
   `ResolvedAnimationPose::pose` results (`ApproximatelyEqual`/
   `RepresentSameRotation`) after the same number of steps AND after the same
   drag gesture — the direct, automated proof of Finding #1's fix: the
   simulation itself, and the teleport guard's own threshold behavior, are
   completely scale-invariant. (A SEPARATE, already-existing/untouched
   concern — `RenderSystem`'s own full-TRS model matrix — is what makes the
   FINAL rendered mesh actually look bigger/smaller; this test only proves
   the physics layer never has to know about scale at all.)
6. Explicitly re-run (unmodified) `tests/Game/Physics/PhysicsSystemTests.cpp`
   and `tests/Game/Physics/PhysicsSystemParallelTests.cpp` after this
   phase's change — every existing fixture in both files constructs its
   entity with no `Transform` component at all, so test 3 above's own
   guarantee is exactly what keeps both files passing unmodified; call this
   out explicitly as a required manual confirmation step (a real compile +
   test-run, not merely "should be fine").

## Step 4: What We Will NOT Do (Focus, this phase)

- We will **not** change `StepDynamicChain()`, `ApplyDynamicChainPhysicsToPose()`,
  `ChainConstraints.cpp`, or `VerletIntegration.cpp` at all — every change in
  this phase lives entirely in `PhysicsSystem.cpp`'s own call-site math,
  converting INTO and OUT OF world space around those already-correct,
  space-agnostic functions.
- We will **not** weaken or remove `maxPlausibleRootDelta`'s teleport guard —
  Step 3.3/Test 2 exist specifically to prove it still works correctly in
  the new coordinate space, now scale-invariant per Finding #1.
- We will **not** change the parallel-dispatch disjoint-chain invariant
  (`PhysicsSystem.cpp`'s existing `kMinDynamicJointsToParallelize`/
  `Jobs::Dispatch()` path) — `entityWorldMatrix`/`entityWorldMatrixInverse`
  are resolved once, read-only, per entity, on the main thread, BEFORE any
  chain-level parallel dispatch begins, and shared by const reference by
  every chain in that one entity's own batch, exactly like `skeleton`/`pose`
  already are.
- We will **not** attempt to make gravity/wind "relative to the character's
  own local down/forward" — gravity remains genuine WORLD-down regardless of
  the entity's own rotation, which is the physically-correct, expected
  behavior for a rotated/tilted character's hair to still fall toward real
  down (confirmed as intentional and requiring no further change here —
  Phase 5 references this explicitly).
- We will **not** make the simulation aware of scale in ANY form (not even a
  uniform-only "extract the average axis scale and re-scale restLengths on
  the fly" compromise) — excluding scale entirely from the physics-space
  matrix is simpler, cheaper (a provably-non-singular inverse instead of a
  generic one), and fully correct, because the render pipeline already
  applies scale correctly, entirely downstream, with zero physics
  involvement needed. A future campaign that specifically wants
  scale-relative collider radii remains free to add that later — out of
  scope here (see `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do").
