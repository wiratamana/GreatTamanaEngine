# PHASE2 — Joint's Own PMX Rigid-Body Shape Becomes Its Collision Radius ("please follow pmx defined rigid body shape")

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (touches the same files; sequenced after so tests can
exercise both together, but this phase's own logic is otherwise independent
of PHASE1's group/mask filter)

---

## Step 1 — The Goal

Answering the user's own question directly: **"i believe still using sphere
collider?"** — today, for the MOVING side of a collision (the simulated
joint particle itself), the answer is actually narrower than "sphere": a
joint particle is a **zero-radius mathematical point**, full stop. It has no
shape at all. Meanwhile every jiggle bone in a real PMX file has its OWN
authored Dynamic/DynamicAndBoneMerge rigid body with a real `shape`
(Sphere/Box/Capsule) and `shapeSize` — exactly the same kind of data this
engine already reads perfectly correctly for the STATIC collider side
(`task_manager/verlet-integration-9`), but for the joint's OWN body, only
`mass`/`linearDamping` are ever read; `shape`/`shapeSize` are parsed into
`RigidBody` and then completely ignored.

**Goal of this phase:** "follow the PMX defined rigid body shape" for the
joint side too — give every simulated joint particle a real, PMX-derived
physical radius, so it visually clips less deeply into a Static collider than
a mathematical point does, matching how the real MMD/Bullet physics pipeline
(where BOTH sides of every collision test are real, sized rigid bodies, never
a point) actually behaves.

## Step 2 — The Situation

See `PHASE0_MASTER_STRATEGY.md`, Step 2, points 6-7 for the already-verified
investigation. The key structural insight this phase's entire design rests
on:

`CapsuleCollider.cpp`'s `SolveCapsuleCollision()` already delegates to
`SolveSphereCollision()`:

```cpp
    SolveSphereCollision(particle, SphereCollider{ closest, collider.radius });
```

**This means if `SolveSphereCollision()` itself is taught to read an
inflation radius directly off the `particle` it already receives by
reference, EVERY shape that ever calls (or delegates to) it — Sphere
directly, Capsule via delegation — gets the new behavior for free, with zero
duplicated logic and zero new function parameters anywhere.** Only
`SolveBoxCollision()` needs its own small, independent equivalent edit (it
does not delegate to the sphere function). `SolveCollision()`'s own
dispatcher (`Collider.cpp`) needs NO change at all — it already forwards
`particle` by reference to whichever per-shape function it calls.

This is why this phase adds the new radius field directly to
**`VerletParticle`** (not as a new function parameter threaded through every
`Solve*Collision()` signature) — the cleanest, smallest, most
backward-compatible design available, and the one this phase adopts.

## Step 3 — The Plan

### 3.1 — `src/Physics/VerletParticle.h`: add a trailing `collisionRadius` field

```cpp
struct VerletParticle {
    Vec3 position = Vec3::Zero();
    Vec3 previousPosition = Vec3::Zero();
    float inverseMass = 1.0f;
    bool pinned = false;

    // task_manager/verlet-integration-10, PHASE2 - this particle's own
    // physical collision extent, derived from its own PMX Dynamic/
    // DynamicAndBoneMerge RigidBody's real shape/shapeSize (see
    // Physics/DynamicChainDefinition.h's own DynamicJointSettings::
    // collisionRadius doc comment for exactly how this is derived per
    // shape) - seeded once per (re)seed by
    // DynamicChainSolver.cpp's SeedParticlesFromAnimatedPose(), exactly
    // like inverseMass already is. DEFAULT IS 0.0f - a zero-radius
    // mathematical point, i.e. EXACTLY today's pre-PHASE2 behavior - so
    // every existing hand-built VerletParticle in every existing test
    // (SphereColliderTests.cpp/BoxColliderTests.cpp/CapsuleColliderTests.cpp/
    // DynamicChainSolverTests.cpp, none of which ever mention this field)
    // continues to produce byte-identical results. Every
    // Solve*Collision() function inflates its own effective surface
    // distance by this value (see SphereCollider.cpp/BoxCollider.cpp) -
    // CapsuleCollider.cpp needs NO change at all, since
    // SolveCapsuleCollision() already delegates to SolveSphereCollision()
    // with the SAME `particle` reference, inheriting the inflation for
    // free.
    float collisionRadius = 0.0f;
};
```

### 3.2 — `src/Physics/SphereCollider.cpp`: inflate by `particle.collisionRadius`

Replace the body of `SolveSphereCollision()`:

```cpp
void SolveSphereCollision(VerletParticle& particle, const SphereCollider& collider) noexcept
{
    // task_manager/verlet-integration-10, PHASE2 - the EFFECTIVE surface
    // distance is the collider's own authored radius PLUS this particle's
    // own physical extent (its own PMX rigid body's shape-derived radius,
    // 0.0f for every particle that never had one seeded - see
    // VerletParticle::collisionRadius's own doc comment). A negative
    // collisionRadius should never occur (DynamicChainDetection.cpp only
    // ever derives a non-negative value - see this phase's own Step 3.5),
    // but is defensively clamped to 0 here anyway, matching this codebase's
    // "never trust an upstream invariant blindly in a leaf math function"
    // convention (see BoxCollider.cpp's own defensive degenerate-shape
    // handling for the established precedent).
    const float effectiveRadius = collider.radius + std::max(0.0f, particle.collisionRadius);

    if (particle.pinned || effectiveRadius <= 0.0f) {
        return;
    }

    const Vec3 delta = particle.position - collider.center;
    const float distance = Length(delta);

    if (distance >= effectiveRadius) {
        return; // Not penetrating.
    }

    if (distance < kEpsilon) {
        particle.position = collider.center + Vec3::Up() * effectiveRadius;
        return;
    }

    particle.position = collider.center + (delta / distance) * effectiveRadius;
}
```

(`<algorithm>` for `std::max` — add `#include <algorithm>` to this `.cpp` if
not already present; check first, this file may already include it
transitively but an explicit include is cheap insurance.)

Update `SphereCollider.h`'s own `SolveSphereCollision()` doc comment to
mention the new `particle.collisionRadius` inflation and its exact
zero-default backward-compatibility guarantee.

### 3.3 — `src/Physics/BoxCollider.cpp`: inflate by `particle.collisionRadius`

Replace the body of `SolveBoxCollision()`:

```cpp
void SolveBoxCollision(VerletParticle& particle, const BoxCollider& collider) noexcept
{
    if (particle.pinned) {
        return;
    }

    const Vec3 worldOffset = particle.position - collider.center;
    const Vec3 local = collider.rotation.Inverse().RotateVector(worldOffset);

    // task_manager/verlet-integration-10, PHASE2 - the EFFECTIVE half-extent
    // on every axis is the box's own authored half-extent PLUS this
    // particle's own physical radius (see SphereCollider.cpp's own
    // identical rationale) - an approximation (a true "rounded box"
    // Minkowski-sum surface is not flat-faced near an edge/corner, unlike
    // this per-axis-inflated approximation), but a deliberately SAFE one:
    // it never UNDER-estimates the true rounded-box surface anywhere,
    // meaning a particle is guaranteed to be pushed AT LEAST as far away
    // as its own true physical radius requires, never less - the same
    // "conservative, never wrong in the unsafe direction" trade-off this
    // codebase already accepts for Box collision's own "push out along the
    // single smallest-escape axis" simplification versus a true SAT-based
    // OBB response.
    const float radius = std::max(0.0f, particle.collisionRadius);
    const Vec3 effectiveHalfExtents = collider.halfExtents + Vec3(radius, radius, radius);

    const bool insideX = std::fabs(local.x) < effectiveHalfExtents.x;
    const bool insideY = std::fabs(local.y) < effectiveHalfExtents.y;
    const bool insideZ = std::fabs(local.z) < effectiveHalfExtents.z;
    if (!(insideX && insideY && insideZ)) {
        return;
    }

    const float escapeX = effectiveHalfExtents.x - std::fabs(local.x);
    const float escapeY = effectiveHalfExtents.y - std::fabs(local.y);
    const float escapeZ = effectiveHalfExtents.z - std::fabs(local.z);

    int axis = 0;
    float smallestEscape = escapeX;
    if (escapeY < smallestEscape) { axis = 1; smallestEscape = escapeY; }
    if (escapeZ < smallestEscape) { axis = 2; smallestEscape = escapeZ; }

    Vec3 pushedLocal = local;
    const float sign = pushedLocal[axis] >= 0.0f ? 1.0f : -1.0f;
    pushedLocal[axis] = sign * effectiveHalfExtents[axis];

    particle.position = collider.center + collider.rotation.RotateVector(pushedLocal);
}
```

Note: when `particle.collisionRadius == 0.0f` (the default), `effectiveHalfExtents
== collider.halfExtents` exactly, and every line above is textually/
behaviorally identical to the pre-PHASE2 function — this is what guarantees
`BoxColliderTests.cpp`'s existing tests (all of which use a plain
default-constructed `VerletParticle`) keep passing unmodified.

Update `BoxCollider.h`'s own `SolveBoxCollision()` doc comment to mention the
new inflation and this exact zero-default guarantee, plus the "conservative
approximation, never unsafe" note above.

### 3.4 — `src/Physics/CapsuleCollider.h`/`.cpp`: doc comment only, ZERO code change

Add one sentence to `CapsuleCollider.h`'s own `SolveCapsuleCollision()` doc
comment: *"task_manager/verlet-integration-10, PHASE2: this function inherits
`particle.collisionRadius` inflation for free through its own existing
delegation to `SolveSphereCollision()` immediately below — no code in this
file needed to change for that."* Do not touch `CapsuleCollider.cpp` at all;
its existing single-line delegation already passes `particle` (the same
object, with whatever `collisionRadius` it now carries) straight through.

### 3.5 — `src/Physics/DynamicChainDefinition.h`: add trailing `collisionRadius` to `DynamicJointSettings`

```cpp
struct DynamicJointSettings {
    float damping = 0.4f;
    float stiffness = 0.02f;
    float mass = 1.0f;
    std::uint8_t group = 0;             // (PHASE1)
    std::uint16_t collisionMask = 0xFFFF; // (PHASE1)

    // task_manager/verlet-integration-10, PHASE2 - this joint's own
    // effective collision radius, derived once by
    // DynamicChainDetection.cpp (Step G) from whichever Dynamic/
    // DynamicAndBoneMerge RigidBody this joint's own bone matched during
    // detection - see that file's own DeriveJointCollisionRadius() doc
    // comment for the exact per-shape derivation rule. Copied into
    // VerletParticle::collisionRadius once per (re)seed by
    // DynamicChainSolver.cpp's SeedParticlesFromAnimatedPose(), mirroring
    // exactly how `mass` is already copied into VerletParticle::inverseMass
    // at that same point. DEFAULT 0.0f - a hand-built chain (every existing
    // test fixture) that never sets this reproduces the exact pre-PHASE2
    // zero-radius-point behavior.
    float collisionRadius = 0.0f;
};
```

### 3.6 — `src/Physics/DynamicChainDetection.cpp`: derive and seed `collisionRadius`

Add a new small, local, pure helper function near this file's own existing
anonymous-namespace helpers (`RoleOf()`, etc.) — place it near the top,
alongside the file's other small free functions:

```cpp
// task_manager/verlet-integration-10, PHASE2 - approximates a PMX rigid
// body's own shape as a single effective collision radius, for use as a
// simulated joint particle's own physical "thickness" (VerletParticle::
// collisionRadius, via DynamicJointSettings::collisionRadius). This is
// DELIBERATELY an approximation (a Box or an off-axis Capsule cannot be
// exactly reduced to one scalar radius) chosen to be SAFE (never larger
// than the body's own true minimum half-thickness, so this can never push a
// joint further away from a collider than its real PMX geometry would
// justify):
//   Sphere  -> shapeSize.x directly (already an exact radius).
//   Capsule -> shapeSize.x directly (the capsule's own radius - its height
//              is a SEGMENT length along the bone chain's own direction,
//              already fully accounted for by the chain's own restLengths;
//              re-using it here as an additional radius would double-count
//              the joint's own reach along its own chain axis).
//   Box     -> the SMALLEST of the three half-extents (shapeSize.x/y/z) -
//              the inscribed-sphere radius, i.e. the largest sphere that
//              still fits entirely inside the box on its own thinnest axis -
//              conservative/safe by construction (never larger than the
//              box's own true minimum half-thickness on any axis).
// A non-positive result (a degenerate/zero-sized authored shape) is clamped
// to exactly 0.0f - "no meaningful physical extent," matching
// IsDegenerateColliderShape()'s own sibling convention in
// Physics/ModelColliderDetection.cpp.
float DeriveJointCollisionRadius(RigidBodyShape shape, const Vec3& shapeSize) noexcept
{
    float radius = 0.0f;
    switch (shape) {
    case RigidBodyShape::Sphere:
        radius = shapeSize.x;
        break;
    case RigidBodyShape::Capsule:
        radius = shapeSize.x;
        break;
    case RigidBodyShape::Box:
        radius = std::min({ shapeSize.x, shapeSize.y, shapeSize.z });
        break;
    }
    return std::max(0.0f, radius);
}
```

(`<algorithm>` for `std::min({...})`/`std::max` — this file already includes
`<algorithm>`.)

Then, in the existing Step G loop (already extended once by PHASE1, Step
3.5), add one more line immediately after the `collisionMask` line PHASE1
added:

```cpp
                chain.jointSettings[j].collisionRadius = DeriveJointCollisionRadius(body.shape, body.shapeSize);
```

The full, final loop body (both this phase's and PHASE1's additions applied
together) reads:

```cpp
        for (std::size_t j = 0; j < chain.jointBoneIndices.size(); ++j) {
            const auto rbIt = boneIndexToRigidBodyIndex.find(chain.jointBoneIndices[j]);
            if (rbIt != boneIndexToRigidBodyIndex.end()) {
                const RigidBody& body = physics->rigidBodies[static_cast<std::size_t>(rbIt->second)];
                chain.jointSettings[j].mass = body.mass;
                chain.jointSettings[j].damping = body.linearDamping;
                chain.jointSettings[j].group = body.group;
                chain.jointSettings[j].collisionMask = body.collisionGroupMask;
                chain.jointSettings[j].collisionRadius = DeriveJointCollisionRadius(body.shape, body.shapeSize);
            }
        }
```

### 3.7 — `src/Physics/DynamicChainSolver.cpp`: seed the particle's own radius

Locate `SeedParticlesFromAnimatedPose()` (already shown in full in this
codebase — see `PHASE0_MASTER_STRATEGY.md`'s own quoted excerpt style for
`DynamicChainDetection.cpp`; the actual function body):

```cpp
void SeedParticlesFromAnimatedPose(
    const DynamicChainDefinition& definition, const std::vector<Vec3>& animatedJointWorldPositions,
    DynamicChainRuntimeState& state, std::size_t jointCount)
{
    state.particles.resize(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        particle.position = animatedJointWorldPositions[i];
        particle.previousPosition = animatedJointWorldPositions[i];
        particle.inverseMass = 1.0f / std::max(definition.jointSettings[i].mass, kMinMass);
        particle.pinned = false;
    }
}
```

Add one line:

```cpp
        particle.collisionRadius = definition.jointSettings[i].collisionRadius;
```

placed immediately after the `particle.inverseMass = ...` line (so the final
loop body sets `position`/`previousPosition`/`inverseMass`/`collisionRadius`/
`pinned`, in that order — keep `pinned = false;` last, matching the existing
convention of leaving the boolean flag as the final field touched).

### 3.8 — New/extended tests

**Extend `tests/Physics/SphereColliderTests.cpp`** — append (do not modify
any existing test) two new `TEST(...)` blocks:

- `ParticleWithNonZeroCollisionRadiusIsPushedFartherThanAZeroRadiusParticle` —
  build two otherwise-identical scenarios (same `SphereCollider`, same
  starting `particle.position` strictly inside the sphere), one with
  `particle.collisionRadius = 0.0f` and one with, e.g., `0.3f`; confirm the
  second particle ends up strictly farther from `collider.center` than the
  first, by approximately the radius difference (`EXPECT_NEAR` against
  `collider.radius + 0.3f`).
- `ZeroCollisionRadiusReproducesExactPreExistingBehavior` — a direct,
  explicit regression guard: construct a particle with
  `collisionRadius = 0.0f` (the default) and confirm the result is
  `ApproximatelyEqual` to calling the function with a particle that never
  even sets the field (i.e. this test is allowed to feel slightly redundant
  with "the default is already 0" — write it anyway, as an explicit,
  permanent, campaign-owned regression guard rather than an implicit
  assumption).

**Extend `tests/Physics/BoxColliderTests.cpp`** — append one new test:

- `ParticleWithNonZeroCollisionRadiusIsPushedToAnInflatedFace` — reuse the
  existing `ParticlePenetratingAlongShortestAxisIsPushedToThatFace` fixture's
  own numbers, but with a non-zero `particle.collisionRadius`; confirm the
  final pushed-out position sits at `collider.halfExtents.y +
  particle.collisionRadius` along Y (not merely `collider.halfExtents.y`).

**New file `tests/Physics/DynamicChainDetectionJointRadiusTests.cpp`** —
exercises `DetectDynamicChains()` directly (mirrors this file's own sibling
`DynamicChainDetectionTests.cpp` structure/local `MakeRigidBody()`-style
helper convention, extended with `shape`/`shapeSize` parameters since the
existing local helper in that file only has `mass`/`linearDamping`
parameters — write this file's OWN richer local helper, per this campaign's
own `PHASE0_MASTER_STRATEGY.md` "reuse vs. reinvent locally" convention,
already independently established by verlet-integration-9's own PHASE0 v2
finding #2):

- `SphereShapedDynamicBodyProducesCollisionRadiusEqualToItsOwnAuthoredRadius`
- `CapsuleShapedDynamicBodyProducesCollisionRadiusEqualToItsOwnRadiusIgnoringHeight`
- `BoxShapedDynamicBodyProducesCollisionRadiusEqualToItsSmallestHalfExtent`
- `DynamicBodyWithNoMatchedRigidBodyLeavesCollisionRadiusAtZero` — a chain
  member bone with no matched `RigidBody` at all in `boneIndexToRigidBodyIndex`
  (should not normally happen given how chains are detected, but exercise the
  `if (rbIt != boneIndexToRigidBodyIndex.end())` guard's else-branch
  explicitly anyway) leaves `jointSettings[j].collisionRadius == 0.0f`
  (the struct's own default, never written).

### 3.9 — Register the new test file

Add `Physics/DynamicChainDetectionJointRadiusTests.cpp` to
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, alongside its sibling
`Physics/DynamicChainDetectionTests.cpp`, plus one taxonomy-comment entry.
`SphereColliderTests.cpp`/`BoxColliderTests.cpp` are already registered (only
new `TEST()` blocks were appended to them, not new files) — no
`CMakeLists.txt` change needed for those two.

## Step 4 — Confirmed zero regression

- Every existing `SolveSphereCollision()`/`SolveBoxCollision()`/
  `SolveCapsuleCollision()` call site in the entire test suite constructs its
  `VerletParticle` via `VerletParticle particle;` (default-constructed) or
  only ever sets `.position`/`.previousPosition`/`.pinned`/`.inverseMass` by
  hand — never `.collisionRadius` — so `effectiveRadius`/
  `effectiveHalfExtents` collapse to exactly `collider.radius`/
  `collider.halfExtents` for every one of them, reproducing the pre-PHASE2
  math exactly, term for term.
- `DynamicChainSolverTests.cpp`'s existing collision tests build their chains
  via `BuildThreeJointChainDefinition()`, whose `DynamicJointSettings{...}`
  3-arg positional init leaves the new trailing `collisionRadius` at its
  `0.0f` default — every joint particle seeded from such a chain ends up with
  `collisionRadius == 0.0f`, so every existing collision assertion in that
  file is unaffected.
