# PHASE1 — Generic Shape Collision Math: Box + Capsule + Unified `Collider`

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: nothing (first phase)
Followed by: `PHASE2_DATA_MODEL_GENERALIZED_COLLIDER_LIST.md`

---

## Step 1 — The Goal

Give the engine two brand-new, pure, fully-tested collision-response
primitives — `BoxCollider` (oriented box) and `CapsuleCollider` (oriented
capsule) — with exactly the same shape, contract, and code quality as the
existing `SphereCollider` (`src/Physics/SphereCollider.h`), plus a single
unified `Collider` struct + `SolveCollision()` dispatcher that lets a caller
hold one homogeneous list mixing all three shapes and resolve any of them
without a `switch` at every call site. Nothing in this phase touches
`DynamicChainDefinition`/`DynamicChainSolver`/`PhysicsSystem` — this phase is
100% new, additive, self-contained math + tests.

## Step 2 — The Situation

`src/Physics/SphereCollider.h/.cpp` already establishes the exact contract to
copy:

```cpp
struct SphereCollider {
    Vec3 center = Vec3::Zero();
    float radius = 0.0f;
};
void SolveSphereCollision(VerletParticle& particle, const SphereCollider& collider) noexcept;
```

`SolveSphereCollision()`: no-op if `particle.pinned` or `radius <= 0`; no-op
if the particle is not penetrating (`distance >= radius`); otherwise push
`particle.position` straight out to the surface along
`center → particle.position`, with a fixed degenerate fallback
(`+Vec3::Up() * radius`) when the particle sits exactly on `center`.
`particle.previousPosition` is never touched.

No Box/Capsule equivalent exists anywhere in the codebase. `Assets/
PhysicsData.h::RigidBody` already documents the exact per-shape
`shapeSize` convention any new code must match:

> Sphere uses only x (radius); Box uses x/y/z as half-extents; Capsule uses
> x (radius) and y (height)

and `src/Editor/RigidBodyWireframe.cpp`'s `BuildCapsuleWireframe()` confirms
the capsule's cylindrical axis is the shape's own **local +Y** before any
rotation is applied (`"Capsule axis = local +Y (see PhysicsData.h)"`).
`height` there is the **full** height (half-height = `shapeSize.y * 0.5f`).

`src/Math/Quat.h` already provides everything needed for orientation:
`Quat::Identity()`, `Quat::RotateVector()`, `Quat::Inverse()`,
`Quat::FromMat4()`. `src/Math/Vec3.h` provides `operator[](int)` (both
`const` and non-`const`) on `Vec3`, `Dot()`, `Cross()`, `Length()`,
`LengthSquared()`, `Normalize()`. `src/Math/MathTypes.h` provides `kEpsilon`,
`Clamp()`.

## Step 3 — The Plan

### 3.1 — New file `src/Physics/BoxCollider.h`

```cpp
#pragma once
#include "VerletParticle.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

namespace gte {

// A single oriented collision BOX in WORLD space - the Box-shaped sibling of
// SphereCollider.h (see that file's own doc comment for the shared
// "bare-bone, no broad-phase" philosophy this campaign preserves). The
// caller is responsible for re-deriving `center`/`rotation` every step from
// the character's own current animated collider-bone world transform - see
// task_manager/verlet-integration-9, PHASE4.
//
// `rotation` follows this engine's ordinary Quat convention (Math/Quat.h) -
// `halfExtents` are measured along the box's own LOCAL axes, i.e. the box's
// world-space corners are `center + rotation.RotateVector({+-hx, +-hy,
// +-hz})`. This exactly matches Assets/PhysicsData.h's own RigidBody::
// shapeSize convention for RigidBodyShape::Box (x/y/z = half-extents), and
// src/Editor/RigidBodyWireframe.cpp's BuildBoxWireframe() (same corner
// construction, used there only for drawing).
struct BoxCollider {
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 halfExtents = Vec3::Zero();
};

// Pushes `particle.position` back out to the BOX's nearest face if it has
// penetrated (i.e. its position, expressed in the box's own local space,
// lies STRICTLY inside every one of the three [-halfExtent, +halfExtent]
// ranges) - a Position-Based-Dynamics-style shallow-projection along the
// single axis with the SMALLEST penetration depth (the standard "push out
// through the nearest face" OBB response), run with the exact same
// ordering/contract as SolveSphereCollision() (see SphereCollider.h): a
// no-op for a pinned particle; `previousPosition` is never modified.
//
// Degenerate/no-op cases (never NaN/Inf, matches SphereCollider.h's own
// "non-positive radius is a no-op" convention): if ANY of halfExtents.x/y/z
// is <= 0, the STRICT "< halfExtent" penetration test can never be
// satisfied on that axis for any real coordinate, so the box is
// automatically, correctly treated as a no-op collider on that (or any)
// degenerate axis - no separate early-return branch is needed for this,
// it falls out of the inequality itself.
void SolveBoxCollision(VerletParticle& particle, const BoxCollider& collider) noexcept;

} // namespace gte
```

### 3.2 — New file `src/Physics/BoxCollider.cpp`

Exact algorithm (write it precisely like this — every step matters for the
tests in 3.6):

```cpp
#include "BoxCollider.h"

#include "../Math/MathTypes.h" // kEpsilon

namespace gte {

void SolveBoxCollision(VerletParticle& particle, const BoxCollider& collider) noexcept
{
    if (particle.pinned) {
        return;
    }

    // Transform the particle into the box's own local space (inverse
    // rotate, translate) - a unit quaternion's Inverse() is its Conjugate()
    // scaled by 1/|q|^2 (see Math/Quat.h), safe even for a slightly
    // non-normalized input.
    const Vec3 worldOffset = particle.position - collider.center;
    const Vec3 local = collider.rotation.Inverse().RotateVector(worldOffset);

    // Penetration test: STRICTLY inside every axis range. See BoxCollider.h's
    // own doc comment for why a degenerate (<=0) half-extent on any axis
    // makes this always false - no separate early-return needed.
    const bool insideX = std::fabs(local.x) < collider.halfExtents.x;
    const bool insideY = std::fabs(local.y) < collider.halfExtents.y;
    const bool insideZ = std::fabs(local.z) < collider.halfExtents.z;
    if (!(insideX && insideY && insideZ)) {
        return; // Not penetrating (or a degenerate box).
    }

    // Escape distance (>= 0, since we just proved |local[axis]| < halfExtents[axis])
    // per axis - the axis with the SMALLEST escape distance is the box's
    // nearest face, the standard shallow-OBB-push-out choice.
    const float escapeX = collider.halfExtents.x - std::fabs(local.x);
    const float escapeY = collider.halfExtents.y - std::fabs(local.y);
    const float escapeZ = collider.halfExtents.z - std::fabs(local.z);

    int axis = 0;
    float smallestEscape = escapeX;
    if (escapeY < smallestEscape) { axis = 1; smallestEscape = escapeY; }
    if (escapeZ < smallestEscape) { axis = 2; smallestEscape = escapeZ; }

    // Push along the chosen axis's own sign, out to that face. Treat
    // exactly-zero as positive (a fixed, deterministic tie-break for the
    // particle sitting exactly on the box's own central plane along this
    // axis - mirrors SphereCollider.cpp's own "push along a fixed arbitrary
    // axis" convention for its degenerate at-center case).
    Vec3 pushedLocal = local;
    const float sign = pushedLocal[axis] >= 0.0f ? 1.0f : -1.0f;
    pushedLocal[axis] = sign * collider.halfExtents[axis];

    particle.position = collider.center + collider.rotation.RotateVector(pushedLocal);
}

} // namespace gte
```

Note: `Vec3::operator[]` (both `const` and mutable) already exists (see
`Math/Vec3.h`) — using it here for `pushedLocal[axis]`/`collider.halfExtents[axis]`
is intentional and idiomatic for this codebase (no new API needed). Add
`#include <cmath>` for `std::fabs` if not already transitively available (it
is not, via `MathTypes.h`'s own `<cmath>` include — verify at implementation
time and add explicitly if the compiler complains; prefer being explicit).

### 3.3 — New file `src/Physics/CapsuleCollider.h`

```cpp
#pragma once
#include "VerletParticle.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

namespace gte {

// A single oriented collision CAPSULE in WORLD space - the Capsule-shaped
// sibling of SphereCollider.h (see that file's own doc comment). The
// capsule's cylindrical axis is the shape's own LOCAL +Y BEFORE `rotation`
// is applied - i.e. its world-space end-cap centers are
// `center +/- rotation.RotateVector(Vec3::Up()) * (height * 0.5f)` - this
// EXACTLY matches src/Editor/RigidBodyWireframe.cpp's own
// BuildCapsuleWireframe() convention ("Capsule axis = local +Y") and
// Assets/PhysicsData.h's RigidBody::shapeSize documented convention for
// RigidBodyShape::Capsule (x = radius, y = FULL height, not half-height).
struct CapsuleCollider {
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    float radius = 0.0f;
    float height = 0.0f; // full height of the cylindrical portion (see above).
};

// Pushes `particle.position` back out to the CAPSULE's surface if
// penetrated - computed as the closest point on the capsule's own central
// line segment to the particle, then delegating to the EXACT SAME
// SolveSphereCollision() logic (SphereCollider.h) against a sphere of that
// same `radius` centered at that closest point - this is both the
// textbook-correct capsule collision formula (a capsule is the Minkowski
// sum of a segment and a sphere) AND guarantees this function inherits
// SolveSphereCollision()'s own already-tested degenerate-at-center
// handling for free, with zero duplicated logic. Same contract as every
// other Solve*Collision() in this campaign: a no-op for a pinned particle
// or a non-positive radius; `previousPosition` is never modified. A
// non-positive `height` degrades to a pure sphere at `center` (both segment
// endpoints coincide) rather than being treated as invalid - matches
// RigidBodyWireframe.h's own "a non-positive shapeSize.y is a 'pure sphere'
// capsule" documented convention exactly.
void SolveCapsuleCollision(VerletParticle& particle, const CapsuleCollider& collider) noexcept;

} // namespace gte
```

### 3.4 — New file `src/Physics/CapsuleCollider.cpp`

```cpp
#include "CapsuleCollider.h"

#include "SphereCollider.h"
#include "../Math/MathTypes.h" // kEpsilon
#include "../Math/Vec3.h"

#include <algorithm>

namespace gte {

void SolveCapsuleCollision(VerletParticle& particle, const CapsuleCollider& collider) noexcept
{
    if (particle.pinned || collider.radius <= 0.0f) {
        return;
    }

    const float halfHeight = std::max(0.0f, collider.height) * 0.5f;
    const Vec3 axisWorld = collider.rotation.RotateVector(Vec3::Up());
    const Vec3 segStart = collider.center - axisWorld * halfHeight;
    const Vec3 segEnd = collider.center + axisWorld * halfHeight;

    Vec3 closest;
    const Vec3 segment = segEnd - segStart;
    const float segmentLengthSq = LengthSquared(segment);
    if (segmentLengthSq < kEpsilon) {
        // Degenerate (zero-height) segment - both endpoints coincide at
        // `center`; a pure sphere.
        closest = collider.center;
    } else {
        const float t = Clamp(Dot(particle.position - segStart, segment) / segmentLengthSq, 0.0f, 1.0f);
        closest = segStart + segment * t;
    }

    // Delegate to the already-tested sphere logic against a sphere of the
    // same radius centered at the closest segment point - see this file's
    // own header comment for why this is both correct and deliberate.
    SolveSphereCollision(particle, SphereCollider{ closest, collider.radius });
}

} // namespace gte
```

### 3.5 — New unified dispatcher: `src/Physics/Collider.h`

```cpp
#pragma once
#include "VerletParticle.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <cstdint>

namespace gte {

// Which of the three PMX-derived collision shapes a Collider (below)
// represents - deliberately its own small enum (rather than reusing
// Assets/PhysicsData.h's RigidBodyShape directly) so this file - and every
// other Physics/ primitive it sits alongside (VerletParticle, SphereCollider,
// WindSettings, ...) - stays completely engine-data-free, with zero
// dependency on Assets/ (see task_manager/verlet-integration-9,
// PHASE0_MASTER_STRATEGY.md's "Architectural tiering" note). The one place
// that DOES need to convert a real Assets::RigidBodyShape into this enum is
// Physics/ModelColliderDetection.h (task_manager/verlet-integration-9,
// PHASE3) - that data-driven tier file is exactly where such a conversion
// belongs.
enum class ColliderShape : std::uint8_t {
    Sphere,
    Box,
    Capsule,
};

// A single WORLD-space collision volume of ANY of the three supported
// shapes, tagged by `shape` - lets a caller (Physics/DynamicChainSolver.h,
// task_manager/verlet-integration-9 PHASE2) hold one homogeneous list
// mixing Sphere/Box/Capsule colliders and resolve each one generically via
// SolveCollision() below, without a switch at every call site. `size`
// reuses the EXACT SAME per-shape field convention as Assets/
// PhysicsData.h's own RigidBody::shapeSize (documented there and mirrored
// by BoxCollider.h/CapsuleCollider.h): Sphere -> size.x = radius; Box ->
// size.xyz = half-extents; Capsule -> size.x = radius, size.y = FULL
// height. `rotation` is meaningless for Sphere (a sphere has no
// orientation) but always present so this struct's own size/shape stays
// uniform regardless of which shape it holds.
struct Collider {
    ColliderShape shape = ColliderShape::Sphere;
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 size = Vec3::Zero();
};

// Dispatches to SolveSphereCollision()/SolveBoxCollision()/
// SolveCapsuleCollision() (SphereCollider.h/BoxCollider.h/CapsuleCollider.h)
// based on `collider.shape` - each underlying function ALREADY checks
// `particle.pinned` and its own shape-specific degenerate-size case
// internally, so this dispatcher adds no extra logic of its own beyond the
// plain field-repacking needed to call the right one.
void SolveCollision(VerletParticle& particle, const Collider& collider) noexcept;

} // namespace gte
```

### 3.6 — New file `src/Physics/Collider.cpp`

```cpp
#include "Collider.h"

#include "BoxCollider.h"
#include "CapsuleCollider.h"
#include "SphereCollider.h"

namespace gte {

void SolveCollision(VerletParticle& particle, const Collider& collider) noexcept
{
    switch (collider.shape) {
    case ColliderShape::Sphere:
        SolveSphereCollision(particle, SphereCollider{ collider.center, collider.size.x });
        return;
    case ColliderShape::Box:
        SolveBoxCollision(particle, BoxCollider{ collider.center, collider.rotation, collider.size });
        return;
    case ColliderShape::Capsule:
        SolveCapsuleCollision(
            particle, CapsuleCollider{ collider.center, collider.rotation, collider.size.x, collider.size.y });
        return;
    }
}

} // namespace gte
```

### 3.7 — Register the 6 new source files in `CMakeLists.txt`

In the root `CMakeLists.txt`, inside `add_library(gte_core STATIC ...)`,
immediately after the existing lines

```
    src/Physics/SphereCollider.h
    src/Physics/SphereCollider.cpp
```

insert:

```
    src/Physics/BoxCollider.h
    src/Physics/BoxCollider.cpp
    src/Physics/CapsuleCollider.h
    src/Physics/CapsuleCollider.cpp
    src/Physics/Collider.h
    src/Physics/Collider.cpp
```

### 3.8 — New test file `tests/Physics/BoxColliderTests.cpp`

Mirror `tests/Physics/SphereColliderTests.cpp`'s own structure/tone
exactly. Required cases:

- `ParticleFullyOutsideBoxIsUntouched` — particle well outside every axis
  range → position AND previousPosition both unchanged.
- `ParticlePenetratingAlongShortestAxisIsPushedToThatFace` — build a box
  with distinctly different half-extents per axis (e.g. `{2,1,3}`), place a
  particle near-center but closer to the Y-face than the X/Z faces, assert
  the result lands EXACTLY on the Y face (`fabs(local.y) == halfExtents.y`
  within tolerance) and the OTHER two local axes are unchanged from the
  particle's pre-solve local coordinates.
- `ParticleAtExactCenterPicksAFixedDeterministicFaceWithoutNaN` — particle
  exactly at `collider.center` (every local axis is 0) → result must be
  finite, and must land exactly on the smallest-half-extent axis's positive
  face (per the "sign(0) treated as positive" rule).
- `RotatedBoxPushesOutAlongItsOwnLocalAxes` — a box rotated 90° around one
  axis, particle penetrating; confirm the push-out direction is expressed in
  the ROTATED frame (i.e. re-verify by transforming the result back into the
  box's local space and checking it lands on a face there), proving
  `rotation` is actually being applied, not ignored.
- `PinnedParticleIsNeverMoved` — same shape as
  `SphereColliderTests.PinnedParticleIsNeverMoved`.
- `DegenerateBoxWithOneOrMoreNonPositiveHalfExtentIsANoOp` — three
  sub-cases: `halfExtents = {0,1,1}`, `{-1,1,1}`, `{0,0,0}` — every one must
  leave the particle completely untouched even when placed exactly at
  `collider.center` (which would otherwise be deeply "inside" a non-degenerate
  box).

### 3.9 — New test file `tests/Physics/CapsuleColliderTests.cpp`

Required cases:

- `ParticleFullyOutsideCapsuleIsUntouched`.
- `ParticleBesideTheCylindricalBodyIsProjectedRadiallyOutward` — particle
  near the segment's midpoint, penetrating sideways; confirm the result's
  distance from the SEGMENT (not from `center`) equals `radius`.
- `ParticleBeyondTheEndCapIsProjectedFromTheNearestEndpointNotTheInfiniteLine` —
  particle positioned past one end of the segment (i.e. closest point is
  clamped to `t=0` or `t=1`, not the unclamped line) — confirm the result's
  distance from that END POINT (not the infinite line) equals `radius`.
- `RotatedCapsuleUsesItsOwnLocalPlusYAxis` — capsule rotated 90°, confirm
  the effective segment direction actually rotated with it (e.g. by placing
  the particle somewhere that would only penetrate if the axis were the
  ROTATED one, not the unrotated local +Y).
- `NonPositiveHeightDegradesToAPureSphereAtCenter` — `height <= 0`, particle
  penetrating near `center` → pushed to distance `radius` from `center`
  itself (both segment endpoints coincide).
- `PinnedParticleIsNeverMoved`.
- `NonPositiveRadiusIsANoOp`.

### 3.10 — New test file `tests/Physics/ColliderTests.cpp`

Required cases (thin dispatcher — keep these short, they exist purely to
prove `SolveCollision()` actually routes to the right underlying function
and repacks fields correctly, NOT to re-test the underlying math already
covered above):

- `SphereShapedColliderMatchesSolveSphereCollisionDirectly` — build a
  `Collider{Sphere, center, Identity, {radius,0,0}}`, run `SolveCollision()`,
  independently run `SolveSphereCollision()` against an equivalent
  `SphereCollider` on a COPY of the same starting particle, and assert the
  two results are `ApproximatelyEqual`.
- `BoxShapedColliderMatchesSolveBoxCollisionDirectly` — same idea, `size` =
  half-extents.
- `CapsuleShapedColliderMatchesSolveCapsuleCollisionDirectly` — same idea,
  `size.x` = radius, `size.y` = height.

### 3.11 — Register the 3 new test files in `tests/CMakeLists.txt`

In `GTE_TEST_SOURCES`, immediately after the existing line
`Physics/SphereColliderTests.cpp`, insert:

```
    Physics/BoxColliderTests.cpp
    Physics/CapsuleColliderTests.cpp
    Physics/ColliderTests.cpp
```

(These are genuinely Tier 1 — no ECS/GPU/Renderer/SkeletonData involved,
exactly like `SphereColliderTests.cpp` — add a one-line entry to that file's
own top-of-file test-taxonomy comment block describing the three new files,
matching the existing convention for every other listed test file.)

---

At the end of this phase: `gte_core` and `GreatTamanaEngineTests` both
compile and link with three new, fully independent, fully tested collision
primitives — but **nothing in the rest of the engine calls them yet**. That
wiring is PHASE2 onward.
