# PHASE2 — Data Model: From "One Sphere" to "A Shared, Mixed-Shape Collider List"

**v2** — unchanged from v1 except for one new, trivial step (3.7, a
doc-comment-only fix found during this campaign's second-iteration re-audit;
see `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", finding #3).
Everything else in this file was re-verified against the real source tree
and confirmed correct as originally written.

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (needs `src/Physics/Collider.h`'s `Collider`/`SolveCollision()`)
Followed by: `PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md`

---

## Step 1 — The Goal

Change the **data model** so a chain no longer carries "one optional
sphere" — it carries a single `bool collisionEnabled` opt-in flag, and the
actual list of collidable shapes (mixed Sphere/Box/Capsule) becomes a
**shared, model-wide** list every opted-in chain tests against. Change
`StepDynamicChain()`'s signature accordingly. This phase is pure data-model
plumbing — it does NOT yet populate the new collider list from real PMX
data (that is PHASE3) and does NOT yet resolve world-space transforms
(PHASE4) — after this phase, the solver can accept and correctly apply an
arbitrary `std::vector<Collider>`, proven by updated unit tests, but nothing
in `PhysicsSystem.cpp` builds a non-empty one yet.

## Step 2 — The Situation

Exact current state (confirmed by direct inspection):

`src/Physics/DynamicChainDefinition.h`, `struct DynamicChainDefinition`,
currently has (near the end of the struct):

```cpp
    bool hasHeadCollider = false;
    std::int32_t headColliderBoneIndex = -1;
    float headColliderRadius = 0.0f;
```

`src/Physics/DynamicChainSolver.h` currently declares:

```cpp
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const SphereCollider* collider = nullptr);
```

`src/Physics/DynamicChainSolver.cpp`, step 5 of `StepDynamicChain()`,
currently:

```cpp
    if (definition.hasHeadCollider && collider != nullptr) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            SolveSphereCollision(state.particles[i], *collider);
        }
    }
```

`src/Physics/DynamicChainDetection.cpp`, Step G (inside the "discard chains
shorter than minimumChainLength" loop), currently:

```cpp
        chain.hasHeadCollider = false;
        chain.headColliderBoneIndex = chain.rootBoneIndex;
        chain.headColliderRadius = chain.jointBoneIndices.empty()
            ? 0.0f
            : (sumRestLengths / static_cast<float>(chain.jointBoneIndices.size())) * 0.5f;
```

`tests/Physics/DynamicChainSolverTests.cpp` has two tests that directly
reference the removed fields/signature:
`HeadColliderKeepsJointsOffItsSurfaceWhenChainFallsIntoIt` (uses
`definition.hasHeadCollider = true;` and
`StepDynamicChain(..., &collider)` with a `SphereCollider`) and
`ColliderIsIgnoredWhenHasHeadColliderIsFalse` (asserts
`ASSERT_FALSE(definition.hasHeadCollider);` and again passes a raw
`SphereCollider*`).

`src/Game/Physics/PhysicsSystem.h`'s own `Update()` doc comment (confirmed
at this file's current line 58) also names the field directly: `"... also
resolves each chain's OPTIONAL head-collision sphere
(DynamicChainDefinition::hasHeadCollider) fresh every step..."`. This is a
comment only (no code here references the field), but it must not be left
dangling once the field it describes no longer exists — see Step 3.7 below.

## Step 3 — The Plan

### 3.1 — Edit `src/Physics/DynamicChainDefinition.h`

Remove exactly these 3 lines (and their preceding "PHASE5 ... simple
head/body collision..." doc comment block, which describes the now-removed
behavior):

```cpp
    bool hasHeadCollider = false;
    std::int32_t headColliderBoneIndex = -1;
    float headColliderRadius = 0.0f;
```

Replace with:

```cpp
    // task_manager/verlet-integration-9 (PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md)
    // - REPLACES the old single-sphere `hasHeadCollider`/
    // `headColliderBoneIndex`/`headColliderRadius` trio entirely. Collision
    // is no longer authored per-chain at all: the actual collider SHAPES
    // (mixed Sphere/Box/Capsule) are auto-detected once per MODEL from every
    // RigidBodyMotionType::Static PMX rigid body (see Physics/
    // ModelColliderDetection.h, PHASE3) and shared by every chain belonging
    // to that model. `collisionEnabled` is the one remaining per-chain
    // knob: when true, EVERY joint particle of THIS chain is tested against
    // EVERY collider in that shared model-wide list (see
    // DynamicChainSolver.h's own StepDynamicChain() step 5) - when false
    // (the default, matching the old hasHeadCollider's own opt-in-only
    // default), collision is a complete no-op for this chain regardless of
    // how many colliders the model has. A human still opts in via the
    // Editor Inspector (see Editor/Panels/InspectorPanel.cpp, PHASE5) -
    // there is simply nothing left to hand-author beyond that one checkbox,
    // since shape/size/position/orientation now all come directly from the
    // model's own real PMX rigid-body data instead of a hand-picked bone
    // index + guessed radius.
    bool collisionEnabled = false;
```

### 3.2 — New file `src/Physics/ModelColliderDefinition.h`

```cpp
#pragma once
#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <cstdint>

namespace gte {

// One collider volume tracked from the model's own PMX Static rigid-body
// data (Assets/PhysicsData.h) - LOCAL, bind-pose-relative data, precomputed
// ONCE per model by Physics/ModelColliderDetection.h's DetectModelColliders()
// (task_manager/verlet-integration-9, PHASE3) and re-resolved to WORLD space
// EVERY FRAME by the caller (Game/Physics/PhysicsSystem.cpp, PHASE4) from
// `boneIndex`'s own current animated world transform - this exactly mirrors
// how the now-removed single SphereCollider used to be re-derived every
// step (see DynamicChainSolver.h's own now-superseded doc comment history),
// generalized here to the full Sphere/Box/Capsule shape set
// Assets::RigidBodyShape actually supports, and to a real per-shape
// ORIENTATION (meaningless for a Sphere, but required for a Box/Capsule to
// be positioned/aimed correctly).
//
// This struct deliberately lives in Physics/'s "data-driven" tier (like
// DynamicChainDefinition.h), not its "pure primitive" tier (like
// Physics/Collider.h) - see task_manager/verlet-integration-9,
// PHASE0_MASTER_STRATEGY.md's "Architectural tiering" note for why this
// split is intentional and must be preserved.
struct ModelColliderDefinition {
    // Bone this collider tracks every frame (RigidBody::boneIndex).
    std::int32_t boneIndex = -1;

    RigidBodyShape shape = RigidBodyShape::Sphere;
    // Same per-shape convention as RigidBody::shapeSize (Assets/PhysicsData.h)
    // and Physics/Collider.h's own `size` field - unaffected by any
    // transform.
    Vec3 shapeSize = Vec3::Zero();

    // The FIXED, bind-pose-relative offset of this collider from
    // `boneIndex`'s own bind-pose world transform - i.e. the unique
    // rotation+translation that, composed on top of `boneIndex`'s CURRENT
    // (animated) world matrix, reproduces exactly where this rigid body's
    // own authored (Assets::RigidBody::translate/rotateRadians, an absolute
    // MODEL-SPACE bind-pose transform - see PhysicsData.h's own doc
    // comment) shape sits at bind pose, and then correctly "rides along"
    // as the bone animates - the same "bind pose offset" principle vertex
    // skinning itself relies on. Precomputed exactly once by
    // DetectModelColliders() (PHASE3) using only bind-pose data (an EMPTY
    // pose vector - see Animation/BoneWorldMatrixQuery.h) - NEVER
    // recomputed per frame.
    Vec3 localOffsetPosition = Vec3::Zero();
    Quat localOffsetRotation = Quat::Identity();
};

} // namespace gte
```

Add this new header to `CMakeLists.txt`'s `add_library(gte_core STATIC ...)`
list, immediately after `src/Physics/DynamicChainDefinition.cpp` (there is
no matching `.cpp` for this file — it is a pure POD struct, exactly like
`VerletParticle.h`/`DynamicChainRuntimeState.h`, which are also listed with
no matching `.cpp`).

### 3.3 — Edit `src/Physics/DynamicChainSolver.h`

Add an include for the new header:

```cpp
#include "Collider.h"
```

(remove the now-unused `#include "SphereCollider.h"` only if nothing else
in this header still needs it — check first; `StepDynamicChain()`'s
signature below no longer references `SphereCollider` at all, so it should
be removed from this header specifically, though `SphereCollider.h` is
obviously still very much alive and used elsewhere).

Change the declaration from:

```cpp
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const SphereCollider* collider = nullptr);
```

to:

```cpp
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const std::vector<Collider>& colliders = {});
```

Update the doc comment's step 5 (currently reads `"5. Collision (PHASE5,
3.2) - exactly ONCE per call, AFTER the goal constraint (collision must
have the final say...): if definition.hasHeadCollider and collider is
non-null, SolveSphereCollision() every joint particle against it."`) to:

```
//   5. Collision (task_manager/verlet-integration-9, PHASE2) - exactly ONCE
//      per call, AFTER the goal constraint (collision must have the final
//      say, matching PBD convention: structural, then soft/goal, then hard
//      collision): if `definition.collisionEnabled`, every joint particle is
//      tested, in order, against EVERY entry of `colliders` (an
//      already-resolved, WORLD-space, mixed-shape list shared across every
//      chain belonging to the same model/entity this frame - see
//      Game/Physics/PhysicsSystem.cpp, PHASE4) via SolveCollision()
//      (Physics/Collider.h). An empty `colliders` list, or
//      `collisionEnabled == false`, is a complete, documented no-op -
//      exactly like the old `hasHeadCollider == false` contract it
//      replaces.
```

Also update the doc comment's parameter description for the removed
`collider` parameter (previously described as "PHASE5, 3.2 - an OPTIONAL,
already-resolved WORLD-space collision sphere...") to describe `colliders`
instead — an already-resolved, WORLD-space, mixed-shape list; empty by
default; the caller (`PhysicsSystem.cpp`) is responsible for having derived
every entry's `center`/`rotation` fresh this frame.

### 3.4 — Edit `src/Physics/DynamicChainSolver.cpp`

Change the function signature to match 3.3 exactly. Replace step 5's body:

```cpp
    if (definition.hasHeadCollider && collider != nullptr) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            SolveSphereCollision(state.particles[i], *collider);
        }
    }
```

with:

```cpp
    // 5. Collision (task_manager/verlet-integration-9, PHASE2) - exactly
    // ONCE per call, AFTER the goal constraint, so collision has the final
    // say (structural, then soft/goal, then hard collision). Every joint
    // particle is tested against EVERY collider in the shared,
    // already-resolved list - order among colliders never matters (each
    // SolveCollision() call is an independent, idempotent-if-already-outside
    // projection), so a particle penetrating more than one collider
    // simultaneously still ends up outside ALL of them by the end of this
    // loop (each subsequent call only ever pushes it further from whichever
    // surface it is CURRENTLY penetrating).
    if (definition.collisionEnabled) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            for (const Collider& collider : colliders) {
                SolveCollision(state.particles[i], collider);
            }
        }
    }
```

(Remove the now-unused `#include "SphereCollider.h"` from this .cpp file
too if nothing else in it still needs it — `SolveCollision()` is declared in
`Collider.h`, already included transitively via `DynamicChainSolver.h`.)

### 3.5 — Edit `src/Physics/DynamicChainDetection.cpp`, Step G

Delete exactly these 4 lines (including the blank line before them) from
inside the `for (DynamicChainDefinition& chain : preliminaryChains)` loop:

```cpp
        chain.hasHeadCollider = false;
        chain.headColliderBoneIndex = chain.rootBoneIndex;
        chain.headColliderRadius = chain.jointBoneIndices.empty()
            ? 0.0f
            : (sumRestLengths / static_cast<float>(chain.jointBoneIndices.size())) * 0.5f;
```

Nothing needs to replace them — `DynamicChainDefinition::collisionEnabled`
already defaults to `false` via its own in-class default member initializer
(3.1), so no explicit seeding is required here at all. Update the function's
own top-of-file Step G comment (currently: `"...then seed
jointSettings/maxPlausibleRootDelta/head-collider defaults exactly like the
old algorithm did."`) to remove the now-inaccurate "head-collider defaults"
phrase — collision detection is now handled entirely separately by
`DetectModelColliders()` (PHASE3), not by this function at all.

### 3.6 — Update `tests/Physics/DynamicChainSolverTests.cpp`

Add `#include "Physics/Collider.h"` near the top (alongside the existing
`#include "Physics/ChainConstraints.h"`).

Rewrite `HeadColliderKeepsJointsOffItsSurfaceWhenChainFallsIntoIt` (rename
it to `EnabledCollisionKeepsJointsOffEveryColliderSurfaceWhenChainFallsIntoIt`
for clarity) to use the new API:

```cpp
TEST(DynamicChainSolverTests, EnabledCollisionKeepsJointsOffEveryColliderSurfaceWhenChainFallsIntoIt)
{
    DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/0.0f, /*damping=*/0.05f);
    definition.collisionEnabled = true;

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};

    // A generous sphere centered right where the chain is expected to sag
    // to under gravity, so at least one joint would otherwise end up
    // strictly inside it - deliberately built as a Collider (not a raw
    // SphereCollider) to prove the new mixed-shape list API is exercised.
    const std::vector<Collider> colliders = { Collider{ ColliderShape::Sphere, Vec3(2.0f, -0.5f, 0.0f),
        Quat::Identity(), Vec3(1.0f, 0.0f, 0.0f) } };

    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, gravity, noWind, colliders);

        for (const VerletParticle& particle : state.particles) {
            const float distanceFromColliderCenter = Length(particle.position - colliders[0].center);
            EXPECT_GE(distanceFromColliderCenter, colliders[0].size.x - 1e-3f)
                << "A joint ended up inside the collider despite collision being enabled.";
        }
    }
}
```

Rewrite `ColliderIsIgnoredWhenHasHeadColliderIsFalse` (rename to
`CollidersAreIgnoredWhenCollisionEnabledIsFalse`):

```cpp
TEST(DynamicChainSolverTests, CollidersAreIgnoredWhenCollisionEnabledIsFalse)
{
    DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/0.0f, /*damping=*/0.05f);
    ASSERT_FALSE(definition.collisionEnabled);

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f) };
    const WindSettings noWind{};

    // A collider that would otherwise immediately swallow every joint.
    const std::vector<Collider> colliders
        = { Collider{ ColliderShape::Sphere, Vec3(2.0f, 0.0f, 0.0f), Quat::Identity(), Vec3(100.0f, 0.0f, 0.0f) } };

    StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, Vec3::Zero(), noWind, colliders);

    for (std::size_t i = 0; i < targets.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(state.particles[i].position, targets[i], 1e-3f))
            << "Collision must not apply at all when collisionEnabled is false, regardless of what colliders are passed in.";
    }
}
```

Also add ONE brand-new test proving the multi-shape/multi-collider list
itself actually iterates every entry, not just the first —
`MultipleCollidersOfDifferentShapesAreAllRespectedSimultaneously`: build a
list containing one `Sphere`, one `Box`, and one `Capsule` `Collider`,
positioned so each one is the ONLY thing blocking a different one of the
three joints, run several steps, and assert all three joints end up outside
their own respective collider (proving the `for (const Collider& collider :
colliders)` inner loop genuinely visits every element for every particle,
not just `colliders[0]`).

Every other existing test in this file that calls `StepDynamicChain(...)`
with the old default (no collider argument at all) needs **no change** —
the new `colliders` parameter defaults to `{}` (an empty vector), exactly
preserving every pre-existing call site's behavior. This includes
`tests/Physics/DynamicChainSolverIdleSettlingTests.cpp` (confirmed a
separate file, also calling `StepDynamicChain(...)` with no collider
argument) — nothing in that file needs to change either.

### 3.7 — (v2, new) Retire the stray `hasHeadCollider` doc-comment mention in `src/Game/Physics/PhysicsSystem.h`

Confirmed by direct inspection: `PhysicsSystem.h`'s own `Update()` method
doc comment (its current line 58) reads, in part:

```
    // ... PHASE5 (task_manager/verlet-integration-1/
    // PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md): also resolves
    // each chain's OPTIONAL head-collision sphere (DynamicChainDefinition::
    // hasHeadCollider) fresh every step, and - once an entity's own chains
    // carry enough TOTAL joints to be worth it - dispatches its INDEPENDENT
    // chains across the Job System's worker pool ...
```

No code in this header references the field (it is purely descriptive
prose), so this is not compile-breaking, but it must not be left describing
a field that no longer exists once this phase lands. Replace the
parenthetical `"also resolves each chain's OPTIONAL head-collision sphere
(DynamicChainDefinition::hasHeadCollider) fresh every step, and"` with:

```
    // ... also resolves this entity's shared, model-wide collider list
    // (task_manager/verlet-integration-9 - see Physics/
    // ModelColliderDefinition.h/PhysicsSystem.cpp's own PHASE4 resolution
    // code) fresh every step for whichever chains opted in via
    // DynamicChainDefinition::collisionEnabled, and
```

Leave the rest of the sentence (the Job System dispatch description)
completely unchanged — only this one parenthetical needs to change.

---

At the end of this phase: the solver, its tests, and the detection
algorithm all compile and pass against the new `collisionEnabled` +
`std::vector<Collider>` API. `PhysicsSystem.cpp` still only ever calls
`StepDynamicChain()` with the old, now-removed single-collider logic
deleted (it will fail to compile until PHASE3/PHASE4 update it) — do not
leave `PhysicsSystem.cpp` broken between phases; PHASE3's own Step 3
includes the minimal compile-fix (an empty `colliders` list) needed to keep
the whole engine building at every phase boundary, with the REAL wiring
following immediately after in PHASE4. `PhysicsSystem.h`'s own doc comment
(3.7 above) is also fully consistent again by the end of this phase.
