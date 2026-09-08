# PHASE4 — Runtime World-Space Collider Resolution and Solver Wiring

**v2** — same design/math as v1 (re-verified correct against the real
`PhysicsSystem.cpp`), with ONE fix: Step 3.1's instruction for extending
`DynamicChainBatchContext` was ambiguous about exactly where the new field
goes; it is now pinned to an exact position with the complete, final struct
body and aggregate-initializer call spelled out verbatim, so there is no
risk of a positional mismatch (see `PHASE0_MASTER_STRATEGY.md`'s "Revision
Notes (v2)", finding #5, for why this matters).

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2, PHASE3
Followed by: `PHASE5_EDITOR_INSPECTOR_UI_UPDATE.md`

---

## Step 1 — The Goal

Make collision **actually happen at runtime**: every frame, for every
entity carrying a `DynamicChainRig`, resolve every one of that model's
`ModelColliderDefinition`s (PHASE3) to its current WORLD-space position
**and orientation** (orientation is new — the old single sphere never
needed one), build one shared `std::vector<Collider>` **once per entity**
(never once per chain — every chain in the same entity sees the exact same
resolved list, mirroring how `skeleton`/`pose`/`entityWorldMatrix` are
already shared across an entity's chains today), and feed it into every
chain whose `collisionEnabled` is true. After this phase, a Furina-like
model with real PMX Static Sphere/Box/Capsule rigid bodies and at least one
chain with `collisionEnabled = true` genuinely collides against all three
shapes, end to end.

## Step 2 — The Situation

- `src/Game/Physics/PhysicsSystem.cpp::Update()` already computes, ONCE per
  entity, on the main thread, BEFORE any per-chain parallel dispatch:
  `entityWorldTransform` / `entityWorldMatrix` (`Mat4::TRS(position,
  rotation, Vec3::One())` — **unit scale**, confirmed by direct
  inspection) / `entityWorldMatrixInverse`. This is exactly the place to
  also resolve the shared collider list — same lifetime, same "resolved
  once, read by every chain" sharing pattern already established for
  `skeleton`/`pose`.
- `DynamicChainBatchContext` (the payload struct handed to
  `StepDynamicChainRange()`/the parallel `Dispatch()` job body) is defined,
  today, EXACTLY as follows (confirmed verbatim against the real
  `PhysicsSystem.cpp`, its anonymous namespace, near the top of the file) —
  reproduced here in FULL because Step 3.1 below edits it precisely:

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
      Mat4 entityWorldMatrix;
      Mat4 entityWorldMatrixInverse;
      bool frozen;
  };
  ```

  and it is constructed, today, at exactly this one call site inside
  `PhysicsSystem::Update()`, with this exact positional (aggregate
  initializer) argument list:

  ```cpp
  DynamicChainBatchContext context{ &model->skeleton, &model->chains, &rig.chainStates, &resolvedPose->pose,
      stepCount, m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind,
      entityWorldMatrix, entityWorldMatrixInverse, rig.frozen };
  ```

  Because this is a plain aggregate initializer, EVERY argument is
  positional — the field at position *k* in the struct receives the
  argument at position *k* in the initializer list, with ZERO compiler
  cross-checking beyond raw type-compatibility. Step 3.1 below therefore
  gives the exact, final, already-correctly-ordered text for BOTH the
  struct and the call site, rather than a relative "insert somewhere"
  instruction — copy them verbatim.
- Rotation extraction from a `Mat4` that is provably pure
  rotation+translation (unit scale) is an **already-established, working
  precedent** in this exact codebase:
  `src/Physics/BoneChainPhysicsResolver.cpp` already does
  `Quat::FromMat4(parentWorld)` / `Quat::FromMat4(grandparentWorld)` against
  `ComputeBoneWorldMatrix()` results composed the same way — this phase
  reuses that exact, proven pattern, not a new one.
- `ModelColliderDefinition::shape` is `Assets::RigidBodyShape` (Sphere/Box/
  Capsule); `Physics::Collider::shape` is the separate, engine-data-free
  `Physics::ColliderShape` (PHASE1) — these two enums share the exact same
  three enumerators in the exact same order **by construction** (this
  campaign wrote both), so a tiny, explicit mapping function is the correct,
  intentional way to convert between them (never a `static_cast` between two
  unrelated enum types, even though it happens to work numerically today —
  an explicit `switch` survives either enum being reordered/extended later
  without silently breaking).

## Step 3 — The Plan

### 3.1 — Edit `src/Game/Physics/PhysicsSystem.cpp`: replace the PHASE3 placeholder

Add includes: `#include "../../Physics/ModelColliderDefinition.h"` (likely
already present transitively via `DynamicChainRigCache.h`, but include it
explicitly for clarity) and confirm `#include "../../Physics/Collider.h"`
is present (added in PHASE3's own Step 3.6).

Add a small, local (anonymous-namespace) helper near the top of this file,
alongside its other private helpers:

```cpp
// task_manager/verlet-integration-9, PHASE4 - Assets::RigidBodyShape and
// Physics::ColliderShape intentionally share the same three enumerators in
// the same order (both written by this same campaign) - this explicit
// mapping is preferred over a raw static_cast so a future reordering/
// extension of either enum can never silently miscompute here.
ColliderShape ToColliderShape(RigidBodyShape shape) noexcept
{
    switch (shape) {
    case RigidBodyShape::Sphere: return ColliderShape::Sphere;
    case RigidBodyShape::Box: return ColliderShape::Box;
    case RigidBodyShape::Capsule: return ColliderShape::Capsule;
    }
    return ColliderShape::Sphere;
}
```

**(v2, exact and unambiguous) Step A — extend the struct.** Change
`DynamicChainBatchContext`'s definition from the 11-field version quoted in
Step 2 above to this 12-field version — the ONLY change is one new field,
`resolvedColliders`, inserted as the very LAST member, immediately after
`bool frozen;` (do not insert it anywhere else — this exact position is
what Step B's call-site text below already assumes):

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
    Mat4 entityWorldMatrix;
    Mat4 entityWorldMatrixInverse;
    bool frozen;

    // task_manager/verlet-integration-9, PHASE4 - every collider this
    // entity's model has, already resolved to WORLD space THIS frame (see
    // PhysicsSystem::Update()'s own construction of this list, right
    // before this context is built) - shared, read-only, by every chain in
    // this entity's own batch, exactly like `skeleton`/`pose` already are.
    // Empty whenever no chain in this entity wants collision at all (see
    // the `anyChainWantsCollision` guard where this is built) - passing an
    // empty list is behaviorally identical to every chain treating
    // collision as disabled, matching StepDynamicChain()'s own documented
    // "empty colliders list is a no-op" contract regardless of
    // collisionEnabled.
    const std::vector<Collider>* resolvedColliders;
};
```

**Step B — resolve the list and update the ONE call site.** Replace the
PHASE3 placeholder inside `StepDynamicChainRange()` (the
`const std::vector<Collider> colliders;` line) — **move this resolution OUT
of `StepDynamicChainRange()` entirely** (it must run exactly ONCE per
entity, not once per call into this function, and definitely not once per
chain) **into `PhysicsSystem::Update()`**, right after
`entityWorldMatrix`/`entityWorldMatrixInverse` are computed and right
before the `DynamicChainBatchContext context{ ... };` line:

```cpp
    // task_manager/verlet-integration-9, PHASE4 - resolve every one of this
    // model's Static-rigid-body colliders (PHASE3) to its CURRENT
    // world-space center/rotation, ONCE per entity per frame, shared
    // read-only by every chain this entity owns (mirrors entityWorldMatrix/
    // skeleton/pose's own existing "resolved once on the main thread,
    // shared by every chain" pattern) - only bothered with at all when at
    // least one of this entity's chains actually opted in
    // (collisionEnabled), and the model has at least one collider, so an
    // entity with collision disabled everywhere (today's default for every
    // chain) pays zero extra ComputeBoneWorldMatrix() calls per frame.
    std::vector<Collider> resolvedColliders;
    const bool anyChainWantsCollision = std::any_of(model->chains.begin(), model->chains.end(),
        [](const DynamicChainDefinition& c) { return c.collisionEnabled; });
    if (anyChainWantsCollision && !model->colliders.empty()) {
        resolvedColliders.reserve(model->colliders.size());
        for (const ModelColliderDefinition& colliderDef : model->colliders) {
            const Mat4 boneWorld
                = entityWorldMatrix * ComputeBoneWorldMatrix(model->skeleton, resolvedPose->pose, colliderDef.boneIndex);
            Collider collider;
            collider.shape = ToColliderShape(colliderDef.shape);
            collider.center = boneWorld.TransformPoint(colliderDef.localOffsetPosition);
            collider.rotation = Quat::FromMat4(boneWorld) * colliderDef.localOffsetRotation;
            collider.size = colliderDef.shapeSize;
            resolvedColliders.push_back(collider);
        }
    }
```

(`<algorithm>` for `std::any_of` — confirm it is already included in this
file; add it explicitly if not.)

Then change the existing `DynamicChainBatchContext context{ ... };`
construction from its current 11-argument form (quoted in Step 2 above) to
this exact 12-argument form — the ONLY change is one new, trailing
argument, `&resolvedColliders`, added at the very END of the list (matching
`resolvedColliders`'s position as the struct's last field in Step A above):

```cpp
    DynamicChainBatchContext context{ &model->skeleton, &model->chains, &rig.chainStates, &resolvedPose->pose,
        stepCount, m_globalSettings.fixedTimestepSeconds, m_globalSettings.gravity, m_globalSettings.wind,
        entityWorldMatrix, entityWorldMatrixInverse, rig.frozen, &resolvedColliders };
```

**Step C — use it inside `StepDynamicChainRange()`.** Inside
`StepDynamicChainRange()`, delete the PHASE3 placeholder
(`const std::vector<Collider> colliders;`) AND the old
`SphereCollider collider; bool hasCollider = false; if (chain.hasHeadCollider) { ... }`
block entirely (both are fully superseded — the collider list is now
resolved once per ENTITY in `Update()`, not once per CHAIN in this
function), and change the `StepDynamicChain(...)` call to pass
`*context.resolvedColliders` as its trailing argument:

```cpp
        if (!context.frozen) {
            for (int step = 0; step < context.stepCount; ++step) {
                StepDynamicChain(chain, rootWorldPos, animatedJointWorldPositions, state, context.fixedTimestepSeconds,
                    context.gravity, context.wind, *context.resolvedColliders);
            }
        }
```

(`StepDynamicChain()`'s own internal `if (definition.collisionEnabled)`
guard, from PHASE2, is what actually decides per-chain whether this list is
used at all — `StepDynamicChainRange()` itself never needs its own
per-chain gate; passing the same shared list to every chain regardless of
that chain's own `collisionEnabled` is correct and intentional, exactly
mirroring how `skeleton`/`pose` are already passed uniformly to every chain
regardless of what that specific chain does with them.)

### 3.2 — Confirm/adjust the `Mat4`/`Quat` includes

`PhysicsSystem.cpp` already includes `Animation/BoneWorldMatrixQuery.h`
(hence `Mat4`) — confirm `Math/Quat.h` is included too (it is very likely
already transitively available via `ECS/Components/Transform.h` or
`Animation/BoneWorldMatrixQuery.h`'s own includes; add it explicitly if the
compiler complains — never rely on an unconfirmed transitive include).

### 3.3 — New/updated tests in `tests/Game/Physics/`

This phase's own correctness (real per-frame world-space resolution,
including orientation) is significant enough to warrant its own dedicated,
real-`PhysicsSystem`-driven test file, following the exact same "hand-built
synthetic rigged model, no ECS/GPU/Renderer beyond a plain Registry"
convention as `tests/Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp`.
This phase adds it as `tests/Game/Physics/
PhysicsSystemModelColliderResolutionTests.cpp` (a companion to PHASE6's own
broader end-to-end test — this file focuses specifically on the
resolution math itself; PHASE6 focuses on the full multi-shape collision
outcome). Required cases:

- `ColliderTracksItsBoneAsTheBoneAnimates` — a synthetic 1-bone model with
  one registered `ModelColliderDefinition` (zero local offset), an entity
  whose `SkeletalAnimator`/pose rotates that bone by a known amount between
  two `Update()` calls, at least one chain with `collisionEnabled = true`
  and a joint positioned so it only avoids penetration if the collider
  genuinely moved with the bone — confirm the joint is still correctly
  pushed outside the (now-relocated) collider on the SECOND call, proving
  the resolution is genuinely re-derived every frame, not cached from
  registration time.
- `ColliderRespectsItsOwnBindPoseLocalOffsetAndRotation` — a
  `ModelColliderDefinition` with a deliberately non-zero
  `localOffsetPosition`/`localOffsetRotation`, bone held at bind pose —
  confirm the resolved `Collider.center`/`.rotation` matches the expected
  composed transform exactly (a direct, narrow regression test for the
  `boneWorld.TransformPoint(...)`/`Quat::FromMat4(...) * ...` composition
  itself).
- `NoChainWantingCollisionResolvesAnEmptyListEveryFrame` — a model with
  real colliders detected but every chain's `collisionEnabled == false` —
  confirm behavior (and, if convenient to observe/inject a counter, that
  the perf-guard genuinely skips the resolution loop) is identical to a
  model with zero colliders at all.
- `EntityWorldTransformRotationIsComposedIntoColliderOrientation` — same
  spirit as the existing `PhysicsSystemWorldSpaceRootMotionTests.cpp`
  precedent: rotate the OWNING ENTITY's own `Transform` (not a bone) and
  confirm a Box/Capsule collider's resolved orientation rotates along with
  it (proving `entityWorldMatrix` is genuinely composed into
  `collider.rotation`, not just `collider.center`).

Add all new test file(s) from this phase to `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES`, immediately after the existing
`Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp` line.

---

At the end of this phase: collision is **fully functional** end-to-end for
any entity/chain that opts in — a real Furina-shaped model's hair/skirt
chain, once `collisionEnabled` is switched on, genuinely collides against
every Sphere/Box/Capsule Static rigid body the PMX file describes, tracked
correctly as the character animates. PHASE5 exposes the new,
much-simplified opt-in toggle in the Editor Inspector (replacing the two
call sites that still reference the removed fields and would otherwise fail
to compile) — and, per this campaign's v2 revision, also fixes two more
call sites in the Bone Viewer that v1 missed entirely. PHASE6 adds the full
end-to-end regression proof and a final build-registration sweep.
