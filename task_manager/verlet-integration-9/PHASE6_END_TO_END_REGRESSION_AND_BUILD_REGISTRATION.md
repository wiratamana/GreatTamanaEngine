# PHASE6 — End-to-End Regression Test and Full Build-Registration Sweep

**v2** — three fixes over v1 (see `PHASE0_MASTER_STRATEGY.md`'s "Revision
Notes (v2)" for the full background):
1. Step 3.1's fixture guidance now gives the EXACT, richer local test helper
   the new test file actually needs (the real, shared `MakeRigidBody()` in
   `DynamicChainDetectionTests.cpp` does not expose `shape`/`shapeSize`/
   `translate`/`rotateRadians` at all, so literally reusing it, as v1
   implied, is not possible).
2. Step 3.3's audit list now explicitly includes `src/Editor/
   BoneViewerWindow.cpp`/`.h` (PHASE5 v2's new scope).
3. New Step 3.5: update `AGENTS.md`'s Job System cross-thread-safety table
   to name the 3 new `src/Physics/*` files.

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2, PHASE3, PHASE4, PHASE5 (final phase)

---

## Step 1 — The Goal

Every previous phase was validated in isolation (unit tests scoped to one
new file/function at a time). This phase closes the loop with **one real,
full-pipeline regression test** that drives the actual
`PhysicsSystem::Update()` entry point end to end — synthetic model
registration → per-frame stepping → real collision outcome — proving a
chain genuinely collides correctly against a Sphere, a Box, AND a Capsule
simultaneously, exactly the scenario the user originally reported as
broken. This phase also performs the final, mechanical but essential sweep:
confirm every new file created across PHASE1–5 is actually registered in
both `CMakeLists.txt` and `tests/CMakeLists.txt` (a file that exists on disk
but is missing from either list silently never compiles/never runs — the
single most common way a multi-phase campaign like this quietly leaves dead
code behind), and that this campaign's own documentation footprint outside
`src/`/`tests/` (specifically `AGENTS.md`'s cross-thread-safety reference
table) is left internally consistent too.

## Step 2 — The Situation

`tests/Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp` and
`tests/Game/Physics/PhysicsSystemTests.cpp` already establish the exact,
proven pattern for a genuine, non-stub, end-to-end `PhysicsSystem` test:
build a hand-crafted `SkeletonData`, run it through
`PhysicsSystem::RegisterDynamicChains()` +
`PhysicsSystem::AttachDynamicChainRigIfNeeded()`, drive a plain
`ResolvedAnimationPose` component directly (no `AnimationSystem`/real PMX
file/GPU involved at all), call `PhysicsSystem::Update()` for many frames
with a real, non-zero `deltaSeconds` and gravity, and assert on the
resulting pose. This phase's own new test follows that exact same
convention, additionally exercising PHASE3's `PhysicsData`/`RigidBody`
Static-collider path (which those two existing files never touch at all —
neither builds a `PhysicsData` with any `RigidBody` entries).

**(v2) Important correction about the available test-fixture helpers.**
`tests/Physics/DynamicChainDetectionTests.cpp` has its own PRIVATE
(anonymous-namespace, file-local — never shared/exported) helper:

```cpp
RigidBody MakeRigidBody(std::int32_t boneIndex, RigidBodyMotionType motionType, float mass = 1.0f, float linearDamping = 0.0f)
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    body.mass = mass;
    body.linearDamping = linearDamping;
    return body;
}
```

Confirmed by direct inspection: this helper only ever sets
`boneIndex`/`motionType`/`mass`/`linearDamping` — every `RigidBody` it
builds is left at `shape = RigidBodyShape::Sphere`, `shapeSize =
Vec3::Zero()` (i.e. a degenerate, zero-radius, collision-eligibility-failing
sphere), `translate = Vec3::Zero()`, `rotateRadians = Vec3::Zero()`. This
is fine for `DynamicChainDetectionTests.cpp`'s own purposes (it only ever
cares about `boneIndex`/`motionType` for chain-topology detection), but this
phase's new test genuinely needs real, non-degenerate, deliberately-offset
Sphere/Box/Capsule bodies — so this exact 4-parameter helper, reused
verbatim, CANNOT build them. It is also private to that other `.cpp`'s own
anonymous namespace, so it cannot be `#include`d/linked from a different
test file even if it were extended. **Step 3.1 below gives the new test
file's own, independently-written, richer helper** — this mirrors the
codebase's own well-established precedent of small, private, per-file test
helpers rather than a shared header (see `RotationFromPmxEuler()` being
independently redeclared in multiple different `.cpp` files, each with a
comment cross-referencing the others, rather than factored into one shared
header).

Every file this whole campaign creates, listed once more for this final
audit pass (copied from `PHASE0_MASTER_STRATEGY.md`'s own manifest table —
re-verify each line against the actual, final state of both CMakeLists
files, since intermediate phases may have been implemented slightly
differently than drafted):

```
src/Physics/BoxCollider.h / .cpp
src/Physics/CapsuleCollider.h / .cpp
src/Physics/Collider.h / .cpp
src/Physics/ModelColliderDefinition.h
src/Physics/ModelColliderDetection.h / .cpp
tests/Physics/BoxColliderTests.cpp
tests/Physics/CapsuleColliderTests.cpp
tests/Physics/ColliderTests.cpp
tests/Physics/ModelColliderDetectionTests.cpp
tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp (PHASE4)
tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp (this phase)
```

(v2) Also re-confirm these EDIT-only files (no new file on disk, but every
one must have zero leftover reference to the removed API by the end of this
phase): `src/Physics/DynamicChainDefinition.h`, `DynamicChainSolver.h/.cpp`,
`DynamicChainDetection.cpp`, `src/Game/Physics/PhysicsSystem.h/.cpp`,
`DynamicChainRigCache.h`, `src/Editor/Panels/InspectorPanel.cpp`, **and,
new in v2, `src/Editor/BoneViewerWindow.h`/`.cpp`** (PHASE5 v2's expanded
scope — v1 of this document did not know about this file at all).

## Step 3 — The Plan

### 3.1 — New file `tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp`

Write this file's OWN local (anonymous-namespace) fixture helpers — do NOT
attempt to `#include` or reuse `DynamicChainDetectionTests.cpp`'s private
`MakeRigidBody()`/`MakeJoint()` (see Step 2's correction above). The richer
helper this file actually needs (name it `MakeRigidBody` locally too — same
established convention, a different file's own private copy, never a name
collision since each lives in its own anonymous namespace):

```cpp
namespace {

RigidBody MakeRigidBody(std::int32_t boneIndex, RigidBodyMotionType motionType, RigidBodyShape shape,
    const Vec3& shapeSize, const Vec3& translate = Vec3::Zero(), const Vec3& rotateRadians = Vec3::Zero())
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    body.shape = shape;
    body.shapeSize = shapeSize;
    body.translate = translate;
    body.rotateRadians = rotateRadians;
    return body;
}

Joint MakeJoint(std::int32_t rigidBodyAIndex, std::int32_t rigidBodyBIndex)
{
    Joint joint;
    joint.rigidBodyAIndex = rigidBodyAIndex;
    joint.rigidBodyBIndex = rigidBodyBIndex;
    return joint;
}

} // namespace
```

Build a synthetic model with:

- A skeleton: `bone 0` = a fixed "world" root (no parent); `bone 1` = the
  static anchor a chain hangs from (e.g. a stand-in for a hip bone); `bones
  2..N` = a short (2–3 joint) `Dynamic` hair/skirt-like chain hanging from
  bone 1; plus 3 SEPARATE bones (`boneSphere`, `boneBox`, `boneCapsule`),
  each with a `Static` `RigidBody` attached (built via the `MakeRigidBody()`
  helper above, passing real, non-degenerate `shape`/`shapeSize` and a
  deliberately non-zero `translate`/`rotateRadians` for at least the
  Box/Capsule ones, exercising PHASE3's bind-pose-relative-offset math for
  real, not just at zero offset) so that, once the chain is allowed to fall
  freely under gravity with `stiffness = 0`, at least one joint's
  un-collided trajectory would end up penetrating EACH of the three
  colliders in turn if that shape's collision math did not work.
- A `PhysicsData` containing all of the above `RigidBody` entries plus the
  `Joint` entries (via `MakeJoint()` above) needed for
  `DetectDynamicChains()` to detect the chain (same overall SHAPE of
  fixture `tests/Physics/DynamicChainDetectionTests.cpp` already
  establishes — reuse that file's OVERALL STRUCTURE/CONVENTIONS, e.g. one
  `Static` anchor body + several `Dynamic` chain-member bodies + `Joint`
  entries connecting them, but build this file's own local helpers/values,
  per Step 2's correction).

Then:

1. Call `PhysicsSystem::RegisterDynamicChains()` with this synthetic
   `SkinnedMeshData` (skeleton + physics), confirming (via an `ASSERT_*`)
   that at least one chain was actually detected AND that
   `model->colliders.size() == 3` (proves PHASE3's detection genuinely ran
   against this fixture, catching a fixture-construction mistake early
   rather than the test silently passing on an accidentally-empty chain).
2. Manually set that detected chain's own `collisionEnabled = true`
   (reaching it the same way `PhysicsSystemTests.cpp`'s own Phase 4 test
   already does — via the registered
   `DynamicChainRigCache::ModelEntry`/`TryGetMutable()`).
3. Attach the rig to an entity, drive a plain FK `ResolvedAnimationPose`
   directly (no animation playback needed — a static bind-ish pose is
   enough, matching the existing precedent), and call
   `PhysicsSystem::Update()` for enough frames/`deltaSeconds` for the chain
   to fall and settle against gravity (mirror the ~120–300-step budget
   already used by `DynamicChainSolverTests.cpp`'s own settling tests).
4. After stepping, read every joint's final WORLD position back out of the
   updated `ResolvedAnimationPose` (via `ComputeBoneWorldMatrix()`, same
   technique `PhysicsSystemWorldSpaceRootMotionTests.cpp` already uses) and
   assert, independently, for EACH of the three colliders:
   - Sphere: `Length(jointWorldPos - sphereWorldCenter) >= sphereRadius - epsilon`.
   - Box: transform `jointWorldPos` into the box's own local space (inverse
     rotate/translate, replicate `SolveBoxCollision()`'s own local-space
     math directly in the test, independently, rather than calling the
     production function itself — a genuine independent check, not a
     tautology) and assert it is NOT strictly inside every one of the three
     `[-halfExtent, +halfExtent]` ranges simultaneously.
   - Capsule: compute the closest point on the capsule's own resolved
     WORLD-space segment to `jointWorldPos` (again, independently
     re-derived in the test, not calling `SolveCapsuleCollision()` itself)
     and assert `Length(jointWorldPos - closestPointOnSegment) >= capsuleRadius - epsilon`.
5. Add one more assertion proving this is a genuine regression guard, not a
   trivially-true test: temporarily/conceptually confirm (e.g. via a
   parallel run with `collisionEnabled` left `false`) that AT LEAST ONE of
   the three joints WOULD have ended up penetrating at least one collider
   without collision enabled — i.e. the fixture's own geometry genuinely
   requires collision to do real work; a fixture where nothing would ever
   penetrate anyway would make this whole test worthless as a regression
   guard.

### 3.2 — Register the new test file

Add `Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp` to
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, immediately after PHASE4's own
`Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp` line (if PHASE4
already added its own test file under a different name than this document
drafted, match whatever name was actually used — the important invariant is
"every test file that exists on disk under `tests/` is listed here", not
the exact literal file name).

### 3.3 — Final CMakeLists.txt registration audit (do this literally, do not skip)

For every file in this document's own Step 2 manifest (and every test file
added by PHASE1/PHASE3/PHASE4 individually), open both `CMakeLists.txt` and
`tests/CMakeLists.txt` and confirm, by eye, that an exact matching line
exists. A file present on disk but ABSENT from these lists will silently
never be compiled — this is not caught by any compiler error (the rest of
the project still builds fine), only by this manual audit or a genuinely
missing symbol at LINK time for a `.cpp` whose declarations are still
`#include`d elsewhere. Specifically re-verify:

- `src/Physics/BoxCollider.h/.cpp`, `CapsuleCollider.h/.cpp`, `Collider.h/.cpp`
  (PHASE1) are in `add_library(gte_core STATIC ...)`.
- `src/Physics/ModelColliderDefinition.h` (PHASE2, header-only — no `.cpp`
  line expected) and `ModelColliderDetection.h/.cpp` (PHASE3) are in the
  same list.
- Every new test file from PHASE1/PHASE3/PHASE4/this phase is in
  `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`.
- No leftover reference to `hasHeadCollider`/`headColliderBoneIndex`/
  `headColliderRadius`/a raw `const SphereCollider*` parameter remains
  ANYWHERE in `src/` or `tests/` (search the whole tree for these exact
  identifiers) — if any is found, it is a missed call site from PHASE2/
  PHASE5 that must be fixed now, not deferred. **(v2) Specifically confirm
  this search also covers `src/Editor/BoneViewerWindow.cpp` and
  `src/Editor/BoneViewerWindow.h`** — v1 of this campaign's own documents
  never mentioned this file at all, so if PHASE5 was executed from an
  un-updated (v1) copy of that document, this exact file is the single most
  likely place a leftover reference still survives; if found, apply
  `PHASE5_EDITOR_INSPECTOR_UI_UPDATE.md` v2's Step 3.3/3.4 now.

### 3.4 — Update `tests/CMakeLists.txt`'s own top-of-file taxonomy comment

This file's own giant header comment documents, one paragraph per test
file, exactly what each Tier-1 test file covers (this is the project's real,
living design-doc for its own test suite — every prior phase already
appended entries here for its own new files, per each phase's own Step 3
instructions). As the very last step of this whole campaign, re-read that
comment block's `Physics/SphereColliderTests.cpp` entry and confirm
entries now also exist, immediately following it, for
`Physics/BoxColliderTests.cpp`, `Physics/CapsuleColliderTests.cpp`,
`Physics/ColliderTests.cpp`, and `Physics/ModelColliderDetectionTests.cpp` —
and that the `Game/Physics/PhysicsSystem*Tests.cpp` block has a new entry
for this phase's own `PhysicsSystemMultiShapeColliderTests.cpp` (and
PHASE4's `PhysicsSystemModelColliderResolutionTests.cpp`) describing what
each actually proves, in the same voice/detail level as every neighboring
entry.

### 3.5 — (v2, new) Update `AGENTS.md`'s Job System cross-thread-safety table

`AGENTS.md`'s "Job System" section has a reference table classifying which
subsystems are safe to call from inside a job body. Its `src/Physics/*` row
currently reads (confirmed verbatim against the real file):

```
| `src/Physics/*` (`VerletIntegration`, `ChainConstraints`, `WindField`, `DynamicChainSolver`, `BoneChainPhysicsResolver`, `SphereCollider`, `FixedTimestepAccumulator`) | **JOB-SAFE** | ...every function is pure logic over only its own parameters, no static/global/singleton mutable state anywhere in the module, safe to call concurrently...
```

`SolveSphereCollision()` is named here specifically because it genuinely
executes inside a job body (`StepDynamicChain()`, dispatched per-chain by
`RunDynamicChainBatchJob()` in `Game/Physics/PhysicsSystem.cpp`). This
campaign's new `SolveBoxCollision()`/`SolveCapsuleCollision()`/
`SolveCollision()` (PHASE1) execute through that exact same call path
(PHASE2 wires `SolveCollision()` into `StepDynamicChain()`'s own step 5) and
are equally pure, stateless functions with zero shared mutable state — add
their file names to this row so a future contributor never has to assume
job-safety by omission. Change the row's file list from:

```
`VerletIntegration`, `ChainConstraints`, `WindField`, `DynamicChainSolver`, `BoneChainPhysicsResolver`, `SphereCollider`, `FixedTimestepAccumulator`
```

to:

```
`VerletIntegration`, `ChainConstraints`, `WindField`, `DynamicChainSolver`, `BoneChainPhysicsResolver`, `SphereCollider`, `BoxCollider`, `CapsuleCollider`, `Collider`, `FixedTimestepAccumulator`
```

Do not add `ModelColliderDetection`/`ModelColliderDefinition` to this row —
`DetectModelColliders()` runs exactly once per entity per frame on the MAIN
thread only (PHASE4's own design, inside `PhysicsSystem::Update()`, BEFORE
any `Dispatch()` call), never inside a job body, so it does not belong in a
JOB-SAFE row at all (nor does it need any other row — it is plain
main-thread-only code, the unmarked default for everything not called out
in this table).

---

## Campaign completion checklist

- [ ] PHASE1: `BoxCollider`/`CapsuleCollider`/`Collider` compile, link, and
      pass their own dedicated Tier-1 tests.
- [ ] PHASE2: `DynamicChainDefinition::collisionEnabled` replaces the old
      trio everywhere; `StepDynamicChain()`'s new
      `const std::vector<Collider>&` signature is used at every call site;
      `DynamicChainSolverTests.cpp`'s two rewritten tests + one new
      multi-shape test all pass; `PhysicsSystem.h`'s stale doc comment is
      fixed (v2).
- [ ] PHASE3: `DetectModelColliders()` correctly reads real PMX Static
      rigid bodies of all three shapes, with bind-pose-relative offsets
      verified by round-trip tests; wired into
      `PhysicsSystem::RegisterDynamicChains()`.
- [ ] PHASE4: `PhysicsSystem::Update()` resolves the shared collider list
      once per entity per frame, including correct orientation composition
      (entity rotation ∘ bone rotation ∘ bind-pose local offset); dedicated
      resolution tests pass; `DynamicChainBatchContext`'s new field sits in
      the exact position PHASE4 v2 specifies (last field, last aggregate-init argument).
- [ ] PHASE5: Editor Inspector compiles again with the new single
      "Enable Collision" checkbox at both call sites; **(v2)**
      `BoneViewerWindow.cpp`/`.h` also compiles again, with a per-model
      (not per-chain) collider wireframe overlay driven by the new
      `RigidBodyEntry::motionType` field.
- [ ] PHASE6: the full end-to-end multi-shape regression test passes, the
      CMakeLists.txt registration audit found zero missing files and zero
      leftover references to the removed API (explicitly including
      `BoneViewerWindow.cpp`/`.h`), and `AGENTS.md`'s Job System table
      names all three new `src/Physics/*` collision files.

Once every box above is checked, the original user complaint — "the
collision perhaps only works with sphere collider... I want you to make
the verlet able to do collision check with [sphere, box, capsule]... all
these shapes can collide against each shape" — is fully and correctly
resolved.
