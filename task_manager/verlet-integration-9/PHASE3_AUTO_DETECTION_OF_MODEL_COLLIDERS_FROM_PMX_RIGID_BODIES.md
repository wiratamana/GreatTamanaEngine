# PHASE3 — Auto-Detection of Model Colliders From Real PMX Rigid Bodies

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2
Followed by: `PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md`

---

## Step 1 — The Goal

Replace the deleted fake single-sphere heuristic with a **real** detector,
`DetectModelColliders()`, that reads every genuine
`RigidBodyMotionType::Static` PMX rigid body of the model (any of Sphere /
Box / Capsule), precomputes its bind-pose-relative offset from its own
tracked bone exactly once, and stores the result on the model's cache entry
(`DynamicChainRigCache::ModelEntry`). After this phase, every model that
carries real PMX Static rigid-body data (which is the normal case for any
properly-rigged MMD/PMX character, "Furina" included) automatically has a
correct, ready-to-resolve collider list the moment it is registered — with
zero manual authoring. This phase does **not** yet resolve anything to
world space per-frame (PHASE4) and does not yet make `PhysicsSystem.cpp`
compile against `StepDynamicChain()`'s new signature beyond the minimal fix
needed to keep the build green (full wiring is PHASE4).

## Step 2 — The Situation

- `Assets/PhysicsData.h::RigidBody` (already read in full during this
  campaign's research) stores, per body: `boneIndex`, `shape`
  (`RigidBodyShape::{Sphere,Box,Capsule}`), `shapeSize` (per-shape
  convention — see PHASE1), `translate`/`rotateRadians` — an **absolute
  model-space bind-pose transform**, the exact same space as
  `SkeletonData::Bone::position` — and `motionType`
  (`Static`/`Dynamic`/`DynamicAndBoneMerge`).
- `Static` means "strictly follows the bone... a collider for e.g.
  hit-testing, not simulated" (RigidBody's own doc comment) — exactly the
  kinematic-obstacle semantics this campaign needs. `Dynamic`/
  `DynamicAndBoneMerge` bodies are the chain's own SIMULATED joints (already
  fully handled elsewhere by `DetectDynamicChains()`) — they are explicitly
  OUT OF SCOPE as collision obstacles in this campaign (colliding one
  chain's simulated output against another simulated body is a genuinely
  different, harder feature — full rigid-body-vs-rigid-body physics — not
  requested here and not to be attempted).
- `src/Animation/BoneWorldMatrixQuery.h::ComputeBoneWorldMatrix(skeleton,
  pose, boneIndex)` computes a bone's world matrix by walking its ancestor
  chain; passing an **empty** `pose` vector makes every bone default to
  `BoneLocalOffset{}` (identity offset) for every ancestor, which is exactly
  the model's **bind pose** — this is the precomputation this phase needs,
  requiring no runtime animated pose at all.
- `src/Physics/BoneChainPhysicsResolver.cpp` already establishes the exact,
  precedented pattern for composing/inverting/extracting rotation from such
  a `Mat4` safely (`Mat4::TryInverse()`, `Quat::FromMat4()`) — reuse that
  pattern, do not invent a new one.
- `src/Editor/RigidBodyWireframe.cpp`'s private `RotationFromPmxEuler()`
  helper (`Quat::FromEulerDegrees(RadToDeg(x), RadToDeg(y), RadToDeg(z))`) is
  the verified-correct PMX rotation convention — it is `static`/anonymous-
  namespace-local to that Editor-only `.cpp` file (which is furthermore only
  compiled under `GTE_ENABLE_PROJECT_PANEL`), so it cannot be `#include`d or
  linked against from an always-compiled `Physics/` file. This campaign's
  own `PHASE0_MASTER_STRATEGY.md` "cross-cutting conventions" section
  already directs: redeclare this exact one-line helper locally, with a
  comment cross-referencing `RigidBodyWireframe.cpp` — this mirrors the
  established precedent in `BoneChainPhysicsResolver.cpp` (which redeclares
  `IkSolver.cpp`'s own tolerance constants rather than sharing a header, by
  explicit design choice documented in that file).
- `src/Game/Physics/DynamicChainRigCache.h`'s `ModelEntry` struct currently
  holds: `chains` (`std::vector<DynamicChainDefinition>`), `skeleton` (a
  private copy), `diagnostics`. It has no collider field yet.
- `src/Game/Physics/PhysicsSystem.cpp::RegisterDynamicChains()` is where
  `DetectDynamicChains()` is currently called and the result stored into a
  freshly-built `ModelEntry`.

## Step 3 — The Plan

### 3.1 — New file `src/Physics/ModelColliderDetection.h`

```cpp
#pragma once
#include "ModelColliderDefinition.h"
#include "../Assets/PhysicsData.h"
#include "../Assets/SkeletonData.h"

#include <vector>

namespace gte {

// task_manager/verlet-integration-9, PHASE3 - scans every
// RigidBodyMotionType::Static rigid body in `physics` (Assets/PhysicsData.h)
// and, for each one that is genuinely usable as a collision obstacle
// (attached to a real bone, non-degenerate shape - see
// ModelColliderDetection.cpp's own IsDegenerateColliderShape() for the
// exact per-shape thresholds, mirroring src/Editor/RigidBodyWireframe.h's
// own documented "nothing meaningful to draw" rules), precomputes its
// bind-pose-relative offset from its own tracked bone (see
// ModelColliderDefinition.h's own doc comment for exactly what that means
// and why) and returns the resulting list, SORTED by boneIndex ascending
// (tie-broken by the rigid body's own original PhysicsData::rigidBodies
// index) for full determinism, matching every other detection algorithm in
// this codebase (see Physics/DynamicChainDetection.h's own Step H).
//
// Deliberately independent of DetectDynamicChains() (Physics/
// DynamicChainDetection.h) - a Static rigid body is a collision obstacle
// regardless of whether the model has ANY dynamic chains at all, and a
// model's collider list never depends on which chains were detected. Both
// functions are called side-by-side by the SAME caller
// (Game/Physics/PhysicsSystem.cpp::RegisterDynamicChains(), PHASE3/PHASE4)
// against the SAME underlying PhysicsData.
//
// Returns an empty list (never null-derefs, never throws) if `physics` is
// nullptr or contains no eligible Static bodies - a model with no PMX
// physics data, or one whose Static bodies are all degenerate/unattached,
// simply has nothing to collide against, exactly like
// DetectDynamicChains()'s own "physics == nullptr -> empty result"
// contract.
//
// Explicitly Dynamic/DynamicAndBoneMerge bodies are NEVER included here -
// see this phase's own strategy document (task_manager/verlet-integration-9/
// PHASE3_AUTO_DETECTION_OF_MODEL_COLLIDERS_FROM_PMX_RIGID_BODIES.md, Step 2)
// for why colliding a chain against another SIMULATED body is out of scope.
std::vector<ModelColliderDefinition> DetectModelColliders(const SkeletonData& skeleton, const PhysicsData* physics);

} // namespace gte
```

### 3.2 — New file `src/Physics/ModelColliderDetection.cpp`

```cpp
#include "ModelColliderDetection.h"

#include "../Animation/BoneWorldMatrixQuery.h"
#include "../Math/Mat4.h"
#include "../Math/MathTypes.h" // kEpsilon, RadToDeg

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Same PMX Euler-rotation convention as src/Editor/RigidBodyWireframe.cpp's
// own (private, Editor-only) RotationFromPmxEuler() - redeclared locally
// rather than shared/exported, matching this campaign's own
// PHASE0_MASTER_STRATEGY.md convention (mirrors the established precedent
// in Physics/BoneChainPhysicsResolver.cpp, which redeclares Animation/
// IkSolver.cpp's own tolerance constants for the identical reason: the
// original file is not always compiled/linked, e.g. RigidBodyWireframe.cpp
// only exists under GTE_ENABLE_PROJECT_PANEL). Verified against
// saba::MMDPhysics.cpp's own `rotMat = ry * rx * rz` - do not change this
// to a different Euler order.
Quat RotationFromPmxEuler(const Vec3& rotateRadians) noexcept
{
    return Quat::FromEulerDegrees(RadToDeg(rotateRadians.x), RadToDeg(rotateRadians.y), RadToDeg(rotateRadians.z));
}

// Mirrors src/Editor/RigidBodyWireframe.h's own documented per-shape
// "nothing meaningful to draw/collide against" degenerate thresholds
// EXACTLY (that file's own doc comment: Sphere - radius <= 0; Box - ALL of
// x/y/z <= 0; Capsule - radius <= 0, non-positive height is still valid -
// a "pure sphere" capsule). Redeclared here (rather than shared) for the
// same reason as RotationFromPmxEuler() above - RigidBodyWireframe.cpp is
// an Editor-only file, not always compiled/linked.
bool IsDegenerateColliderShape(RigidBodyShape shape, const Vec3& shapeSize) noexcept
{
    switch (shape) {
    case RigidBodyShape::Sphere:
        return shapeSize.x <= kEpsilon;
    case RigidBodyShape::Box:
        return shapeSize.x <= kEpsilon && shapeSize.y <= kEpsilon && shapeSize.z <= kEpsilon;
    case RigidBodyShape::Capsule:
        return shapeSize.x <= kEpsilon;
    }
    return true;
}

} // namespace

std::vector<ModelColliderDefinition> DetectModelColliders(const SkeletonData& skeleton, const PhysicsData* physics)
{
    std::vector<ModelColliderDefinition> result;
    if (physics == nullptr) {
        return result;
    }

    const std::size_t boneCount = skeleton.bones.size();
    const std::vector<BoneLocalOffset> bindPose; // empty - ComputeBoneWorldMatrix() defaults every entry to identity.

    for (std::size_t rigidBodyIndex = 0; rigidBodyIndex < physics->rigidBodies.size(); ++rigidBodyIndex) {
        const RigidBody& body = physics->rigidBodies[rigidBodyIndex];
        if (body.motionType != RigidBodyMotionType::Static) {
            continue; // Only a kinematic, bone-following body is a valid collision obstacle - see this file's own header comment.
        }
        if (body.boneIndex < 0 || static_cast<std::size_t>(body.boneIndex) >= boneCount) {
            continue; // Unattached - nothing to track every frame.
        }
        if (IsDegenerateColliderShape(body.shape, body.shapeSize)) {
            continue; // Nothing meaningful to collide against.
        }

        const Mat4 bindBoneWorld = ComputeBoneWorldMatrix(skeleton, bindPose, body.boneIndex);
        Mat4 bindBoneWorldInverse;
        if (!bindBoneWorld.TryInverse(bindBoneWorldInverse)) {
            continue; // Algebraically should never happen (pure bind-pose TRS) - defensive only, matches
                      // BoneChainPhysicsResolver.cpp's own identical precedent.
        }

        const Mat4 rigidBodyBindWorld = Mat4::TRS(body.translate, RotationFromPmxEuler(body.rotateRadians), Vec3::One());
        const Mat4 localOffsetMat = bindBoneWorldInverse * rigidBodyBindWorld;

        ModelColliderDefinition def;
        def.boneIndex = body.boneIndex;
        def.shape = body.shape;
        def.shapeSize = body.shapeSize;
        def.localOffsetPosition = localOffsetMat.TransformPoint(Vec3::Zero());
        def.localOffsetRotation = Quat::FromMat4(localOffsetMat);
        result.push_back(def);
    }

    // Full determinism - ascending boneIndex, ties broken by original
    // rigid-body index (stable_sort preserves the ascending-rigidBodyIndex
    // insertion order for equal boneIndex keys, matching this codebase's
    // own established "ascending index" tie-break convention elsewhere -
    // see e.g. DynamicChainDetection.cpp's Step C, pass 3).
    std::stable_sort(result.begin(), result.end(),
        [](const ModelColliderDefinition& a, const ModelColliderDefinition& b) { return a.boneIndex < b.boneIndex; });

    return result;
}

} // namespace gte
```

Verify `Mat4.h` actually exposes `Mat4::TRS(position, rotation, scale)`,
`Mat4::TryInverse(Mat4&)`, `Mat4::TransformPoint(Vec3)`, and
`operator*(Mat4,Mat4)` (all already used identically elsewhere in this
codebase — `PhysicsSystem.cpp`'s own `entityWorldMatrix` construction and
`BoneChainPhysicsResolver.cpp`'s `anchorWorld`/`anchorWorldInverse` — so
this is a confirmed-safe reuse, not a new API surface).

### 3.3 — Register the 2 new source files in `CMakeLists.txt`

Immediately after the (already-added, PHASE2) `src/Physics/
ModelColliderDefinition.h` line, insert:

```
    src/Physics/ModelColliderDetection.h
    src/Physics/ModelColliderDetection.cpp
```

### 3.4 — Edit `src/Game/Physics/DynamicChainRigCache.h`

Add `#include "../../Physics/ModelColliderDefinition.h"` and, inside
`struct ModelEntry`, add a new field alongside the existing `chains`:

```cpp
    // task_manager/verlet-integration-9, PHASE3 - every Static rigid-body
    // collider detected for this model (DetectModelColliders(),
    // Physics/ModelColliderDetection.h), in LOCAL/bind-pose-relative form -
    // shared read-only by every chain belonging to this model whose own
    // DynamicChainDefinition::collisionEnabled is true (see
    // Game/Physics/PhysicsSystem.cpp, PHASE4, which resolves this list to
    // WORLD space fresh every frame before stepping any chain).
    std::vector<ModelColliderDefinition> colliders;
```

### 3.5 — Edit `src/Game/Physics/PhysicsSystem.cpp::RegisterDynamicChains()`

Add `#include "../../Physics/ModelColliderDetection.h"` to this file's
includes. Immediately after the existing:

```cpp
    DynamicChainDetectionResult detection
        = DetectDynamicChains(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr, defaults);
```

add:

```cpp
    // task_manager/verlet-integration-9, PHASE3 - independent of chain
    // detection above (see DetectModelColliders()'s own doc comment for
    // why): every Static rigid body of any shape becomes a collision
    // obstacle candidate for every chain belonging to this same model, once
    // opted in per-chain (DynamicChainDefinition::collisionEnabled, PHASE2).
    std::vector<ModelColliderDefinition> colliders
        = DetectModelColliders(data.skeleton, data.physics.has_value() ? &*data.physics : nullptr);
```

and, further down where `entry.chains`/`entry.skeleton`/`entry.diagnostics`
are assigned onto the freshly-built `DynamicChainRigCache::ModelEntry`, add:

```cpp
    entry.colliders = std::move(colliders);
```

### 3.6 — Minimal compile-fix for `StepDynamicChainRange()` (full wiring is PHASE4)

At the end of PHASE2, `StepDynamicChainRange()`'s existing
`SphereCollider`-based block (the one that reads `chain.hasHeadCollider` /
`chain.headColliderBoneIndex` / `chain.headColliderRadius`, none of which
exist anymore) will fail to compile. To keep the whole engine building
GREEN at this phase boundary (this campaign's own convention: never leave
an intermediate phase in a broken state), replace that entire block with
the following MINIMAL placeholder — this makes the build succeed and every
chain behaviorally identical to "collision fully disabled" (since
`collisionEnabled` still defaults to `false` for every existing/detected
chain, and no code yet calls `SolveCollision` for real) — the REAL
per-frame world-space resolution replacing this placeholder is PHASE4's own
Step 3.1, applied to this exact same call site:

```cpp
        // task_manager/verlet-integration-9 - placeholder until PHASE4 wires
        // real per-frame world-space collider resolution here; an empty
        // list keeps every chain's own StepDynamicChain() call behaviorally
        // identical to "collision fully disabled" in the meantime.
        const std::vector<Collider> colliders;
```

and change the `StepDynamicChain(...)` call inside the `for (int step ...)`
loop from its current trailing argument
(`hasCollider ? &collider : nullptr`) to simply `colliders`. Add
`#include "../../Physics/Collider.h"` to this file's includes (replacing
the now-unused `#include "../../Physics/SphereCollider.h"` if nothing else
in the file still needs it — check first).

### 3.7 — New test file `tests/Physics/ModelColliderDetectionTests.cpp`

Mirror `tests/Physics/DynamicChainDetectionTests.cpp`'s own
hand-built-`SkeletonData`/`PhysicsData` fixture style. Required cases:

- `StaticSphereBodyAtBoneOriginProducesAColliderWithZeroLocalOffset` — one
  bone at a known position, one `Static` `RigidBody` with
  `shape=Sphere`, `shapeSize={radius,0,0}`, `translate` set to EXACTLY that
  bone's own bind position, `rotateRadians={0,0,0}` → resulting
  `ModelColliderDefinition.localOffsetPosition` must be
  `ApproximatelyEqual(Vec3::Zero())` and `localOffsetRotation` must
  `RepresentSameRotation(Quat::Identity())`.
- `StaticBoxBodyOffsetFromItsBoneProducesTheCorrectLocalOffset` — a bone at
  one position, a `Static` `Box` body whose `translate` is offset by a known
  delta from that bone's own bind position and whose `rotateRadians` is
  non-zero — assert `localOffsetPosition`/`localOffsetRotation` reproduce
  exactly that authored delta (verify by composing `bindBoneWorld *
  Mat4::TRS(localOffsetPosition, localOffsetRotation, Vec3::One())` and
  checking it equals `Mat4::TRS(body.translate, expectedRotation,
  Vec3::One())` within tolerance — i.e. a genuine round-trip proof, not just
  an isolated field check).
- `DynamicAndDynamicAndBoneMergeBodiesAreNeverIncluded` — a model with one
  `Static` and one `Dynamic` and one `DynamicAndBoneMerge` body, all
  otherwise valid/non-degenerate → result contains exactly one entry (the
  Static one).
- `UnattachedOrOutOfRangeBoneIndexIsExcluded` — `boneIndex = -1` and
  `boneIndex = 999` on an otherwise-valid Static body → both excluded, no
  crash.
- `DegenerateShapesOfEachKindAreExcluded` — one sub-case per shape
  (`Sphere` radius `0`; `Box` all-zero half-extents; `Capsule` radius `0`)
  → all excluded. Also confirm a `Capsule` with a non-positive `height` but
  a positive radius IS still included (matches the documented "non-positive
  height is still valid" rule).
- `NullPhysicsDataDetectsNothing`.
- `ResultIsSortedByAscendingBoneIndex` — several Static bodies attached to
  bones in a deliberately scrambled order → result's `boneIndex` sequence
  is strictly non-decreasing.

### 3.8 — Register the new test file in `tests/CMakeLists.txt`

In `GTE_TEST_SOURCES`, immediately after the (already-added, PHASE1)
`Physics/ColliderTests.cpp` line, insert:

```
    Physics/ModelColliderDetectionTests.cpp
```

---

At the end of this phase: every model's collider list is correctly
detected and cached, fully covered by unit tests — but `PhysicsSystem.cpp`
still resolves it to an always-empty runtime list (3.6's placeholder), so
no chain in the running game/Editor actually collides against anything new
yet. PHASE4 replaces that placeholder with the real, per-frame, world-space
resolution.
