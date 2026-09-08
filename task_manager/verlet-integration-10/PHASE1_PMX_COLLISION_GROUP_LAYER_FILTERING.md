# PHASE1 (v2) — PMX Collision Group/Layer Filtering ("use pmx defined rigid body layer rule with collision map")

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: none (first phase)

**v2 change summary (see `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes
(v2)" for the full campaign-wide list):** this phase's `GroupsMayCollide()`
now masks `group` to its documented 4-bit range before shifting (finding #2 —
an undefined-behavior fix, not a style nit), a new regression test proves
this explicitly, and the `saba::MMDPhysics.cpp` line-number citation for
`GetGroup()`/`GetGroupMask()` is corrected from a stale "~613-615" to the
confirmed-accurate 632-637 (finding #3). Nothing else in this phase changed —
its overall design was re-verified against the live source tree and found
correct.

---

## Step 1 — The Goal

Every PMX `RigidBody` already carries real collision-filtering data
(`group`, a 0-15 single-bit "which layer am I on" value; `collisionGroupMask`,
a 16-bit "which layers am I allowed to hit" bitmask) — already parsed
correctly by `PmxLoader.cpp`, already documented correctly, but **completely
unused** by the actual Verlet collision solver today: every joint of every
collision-opted-in chain is tested against every detected Static collider,
unconditionally, regardless of what group either one belongs to.

**Goal of this phase:** make the solver's own collision loop respect this
data exactly the way the reference `saba::MMDPhysics` backend does — a joint
particle (carrying the group/mask of ITS OWN PMX Dynamic/DynamicAndBoneMerge
rigid body) only actually collides against a Static collider (carrying the
group/mask of ITS OWN PMX rigid body) when the two are mutually "visible" to
each other under Bullet's own symmetric group/mask AND-test — **and doing so
safely, even if a malformed/adversarial `.pmx` file supplied a `group` byte
outside its documented 0-15 range** (see Step 2, point 5 below).

## Step 2 — The Situation

See `PHASE0_MASTER_STRATEGY.md`, Step 2, points 1-5, 8, and 13 for the full,
already-verified source-level investigation this phase builds on. The
essential chain of data flow this phase must extend, end to end:

```
PMX file
  -> RigidBody::group / RigidBody::collisionGroupMask        (Assets/PhysicsData.h - ALREADY CORRECT, no change)
    -> [Static bodies]  ModelColliderDefinition               (Physics/ModelColliderDetection.cpp - MISSING today)
        -> [resolved every frame]  Collider                   (Game/Physics/PhysicsSystem.cpp - MISSING today)
    -> [Dynamic/DynamicAndBoneMerge bodies]  DynamicJointSettings  (Physics/DynamicChainDetection.cpp, Step G - MISSING today)
        -> [seeded once]  VerletParticle                       (Physics/DynamicChainSolver.cpp - N/A, see below)
  -> DynamicChainSolver.cpp's collision loop: filter BEFORE calling SolveCollision()  (MISSING today)
```

Note the asymmetry: a **collider** (Static body) needs its group/mask stored
on the long-lived, once-per-model `ModelColliderDefinition`, then copied into
the once-per-frame resolved `Collider`. A **joint** (Dynamic body) needs its
group/mask stored on the long-lived, once-per-model `DynamicJointSettings`
(exactly where `mass`/`damping` already live) — it does NOT need to live on
`VerletParticle` at all, because, unlike `collisionRadius` (PHASE2, which the
low-level `Solve*Collision()` functions need to read directly off the
particle), the group/mask FILTER decision is made once, by the caller
(`DynamicChainSolver.cpp`), BEFORE ever calling `SolveCollision()` — the
per-shape math functions themselves never need to know about filtering at
all. Keep this distinction in mind; it is why this phase touches
`DynamicJointSettings` but not `VerletParticle` (PHASE2 is the one that
touches `VerletParticle`).

**v2 addition — why the group value cannot be trusted blindly (see
`PHASE0_MASTER_STRATEGY.md`, Step 2 point 13, and Revision Notes finding
#2):** `RigidBody::group` is documented as "0-15" but is populated verbatim,
with zero range validation, straight from a `.pmx` file's own raw byte
(`PmxLoader.cpp::ConvertRigidBody()`, confirmed: `out.group = body.m_group;`).
A well-formed model authored by MikuMikuDance or a compliant tool will always
respect the 0-15 range, but this engine has no way to guarantee the FILE
itself is well-formed — a corrupted or hand-edited `.pmx` could contain any
byte value 0-255 in that field. `1u << group` is undefined behavior in C++ the
moment `group >= 32` (the bit width of the `unsigned int` the shift operand
promotes to). This phase's filter function must mask `group` to its
documented range BEFORE shifting, once, in one shared place, so every caller
gets this safety for free.

## Step 3 — The Plan

### 3.1 — `src/Physics/Collider.h`: add trailing `group`/`collisionMask` fields

Edit the `Collider` struct (append AFTER `size`, per PHASE0's
"append at the end" rule):

```cpp
struct Collider {
    ColliderShape shape = ColliderShape::Sphere;
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 size = Vec3::Zero();

    // task_manager/verlet-integration-10, PHASE1 - this collider's own PMX
    // collision-group membership/mask (Assets/PhysicsData.h's own
    // RigidBody::group/collisionGroupMask, copied verbatim by
    // Physics/ModelColliderDetection.h's DetectModelColliders() and
    // Game/Physics/PhysicsSystem.cpp's own per-frame resolution - see
    // DynamicJointSettings::group/collisionMask below for the joint-side
    // half of this same filter). `group` is a single 0-15 value (this
    // collider's own layer); `collisionMask` is which layers this collider
    // is willing to be hit BY. DEFAULT VALUES ARE DELIBERATE: `group = 0`,
    // `collisionMask = 0xFFFF` (every bit set) - this is "belongs to layer 0,
    // collides with every layer", which is EXACTLY the old (pre-PHASE1)
    // behavior for any Collider that never had real PMX group/mask data
    // copied into it (every existing hand-built test fixture in
    // tests/Physics/{Collider,DynamicChainSolver}Tests.cpp constructs a
    // Collider via plain positional aggregate-init that never mentions these
    // two new trailing fields) - see this campaign's own
    // PHASE0_MASTER_STRATEGY.md, Step 2 point 12, for why this is not a
    // coincidence. NOTE: `group` is never range-validated anywhere in this
    // engine (see PHASE0's Step 2 point 13) - any code that turns this value
    // into a shift amount (`1u << group`) MUST mask it first
    // (`group & 0x0Fu`); see DynamicChainSolver.cpp's own GroupBit() helper
    // below for the one shared, safe implementation.
    std::uint8_t group = 0;
    std::uint16_t collisionMask = 0xFFFF;
};
```

Add `#include <cstdint>` to `Collider.h` if not already present (it already
is, per the existing `ColliderShape : std::uint8_t` enum).

Update `SolveCollision()`'s own doc comment to mention that filtering is the
CALLER's responsibility (`DynamicChainSolver.cpp`), not this dispatcher's —
`SolveCollision()` itself needs zero code changes; it only ever repacks
`shape`/`center`/`rotation`/`size` into the per-shape structs, never touches
`group`/`collisionMask`.

### 3.2 — `src/Physics/ModelColliderDefinition.h`: add trailing `group`/`collisionMask` fields

Append, after `localOffsetRotation`:

```cpp
    // task_manager/verlet-integration-10, PHASE1 - copied verbatim from the
    // Static RigidBody this collider was detected from
    // (Assets/PhysicsData.h::RigidBody::group/collisionGroupMask) - see
    // Physics/Collider.h's own `group`/`collisionMask` doc comment for the
    // full filtering contract these are resolved into every frame, and its
    // own note on why `group` must always be masked (`& 0x0Fu`) before use
    // as a shift amount.
    std::uint8_t group = 0;
    std::uint16_t collisionMask = 0xFFFF;
```

### 3.3 — `src/Physics/ModelColliderDetection.cpp`: populate the new fields

In `DetectModelColliders()`, immediately after the existing
`def.shapeSize = body.shapeSize;` line, add:

```cpp
        def.group = body.group;
        def.collisionMask = body.collisionGroupMask;
```

(`body` is already in scope — the same `const RigidBody& body` this
function already reads `shape`/`shapeSize`/`translate`/`rotateRadians` from.)
No other change needed in this file.

### 3.4 — `src/Physics/DynamicChainDefinition.h`: add trailing `group`/`collisionMask` fields to `DynamicJointSettings`

```cpp
struct DynamicJointSettings {
    float damping = 0.4f;
    float stiffness = 0.02f;
    float mass = 1.0f;

    // task_manager/verlet-integration-10, PHASE1 - this JOINT's own PMX
    // collision-group membership/mask, copied from whichever Dynamic/
    // DynamicAndBoneMerge RigidBody this joint's own bone matched during
    // detection (DynamicChainDetection.cpp, Step G) - the joint-side half of
    // the same Bullet-style group/mask filter Physics/Collider.h's own
    // `group`/`collisionMask` fields implement for the STATIC collider side.
    // Same "collides with everything by default" rationale as Collider's own
    // fields - every hand-built DynamicJointSettings{...} in the existing
    // test suite (e.g. DynamicChainSolverTests.cpp's
    // `DynamicJointSettings{ damping, stiffness, 1.0f }`) leaves these two
    // trailing fields at these exact defaults, reproducing pre-PHASE1
    // behavior byte-for-byte. `group` is NEVER range-validated anywhere in
    // this engine (a raw, unchecked byte from the source .pmx file - see
    // PHASE0_MASTER_STRATEGY.md's Step 2 point 13) - always mask it
    // (`group & 0x0Fu`) before using it as a shift amount.
    std::uint8_t group = 0;
    std::uint16_t collisionMask = 0xFFFF;
};
```

### 3.5 — `src/Physics/DynamicChainDetection.cpp`, Step G: seed the new fields

Locate the existing loop (Step G, already shown in PHASE0 Step 2 point 5,
confirmed at the real file's lines 444-451):

```cpp
        for (std::size_t j = 0; j < chain.jointBoneIndices.size(); ++j) {
            const auto rbIt = boneIndexToRigidBodyIndex.find(chain.jointBoneIndices[j]);
            if (rbIt != boneIndexToRigidBodyIndex.end()) {
                const RigidBody& body = physics->rigidBodies[static_cast<std::size_t>(rbIt->second)];
                chain.jointSettings[j].mass = body.mass;
                chain.jointSettings[j].damping = body.linearDamping;
            }
        }
```

Add two lines inside the existing `if` body, immediately after the
`damping` line:

```cpp
                chain.jointSettings[j].group = body.group;
                chain.jointSettings[j].collisionMask = body.collisionGroupMask;
```

No other change to this file in this phase (PHASE2 revisits this exact same
loop again, adding one more line — read PHASE2 before editing this loop a
second time, or make both edits together if convenient; either order is
functionally identical since PHASE2's own line is independent of this one).

### 3.6 — `src/Game/Physics/PhysicsSystem.cpp`: copy group/mask into the resolved `Collider`

Locate the existing per-entity collider-resolution loop (confirmed real
lines 390-399, function `PhysicsSystem::Update()`):

```cpp
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
```

Add two lines immediately after `collider.size = colliderDef.shapeSize;`:

```cpp
                collider.group = colliderDef.group;
                collider.collisionMask = colliderDef.collisionMask;
```

### 3.7 — `src/Physics/DynamicChainSolver.cpp`: the actual filter (step 5)

This is the phase's one genuinely new piece of LOGIC (everything above is
plumbing). Locate the existing collision loop:

```cpp
    if (definition.collisionEnabled) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            for (const Collider& collider : colliders) {
                SolveCollision(state.particles[i], collider);
            }
        }
    }
```

Replace the inner body with a group/mask-filtered call. First, add two small
local free functions near the top of the anonymous namespace already present
in this file (alongside `kMinMass`/`SeedParticlesFromAnimatedPose`/
`IsFinite`):

```cpp
// task_manager/verlet-integration-10, PHASE1 (v2) - a single "which layer am
// I on" bit, SAFELY derived from a raw group value that this engine never
// range-validates anywhere in its own load pipeline (PmxLoader.cpp's own
// ConvertRigidBody(): `out.group = body.m_group;`, a straight byte copy off
// an untrusted .pmx file, with no range check - see
// PHASE0_MASTER_STRATEGY.md's Step 2 point 13). PMX authoring tools always
// emit 0-15, but this engine cannot assume the FILE itself is well-formed.
// Masking to the documented 4-bit range BEFORE shifting is what guarantees
// `1u << group` can never become undefined behavior (shifting by an amount
// >= the promoted-to unsigned int's own bit width, 32, is UB in C++ - a real
// risk for an unmasked `group` up to 255) - matching this codebase's own
// "degrade gracefully, never crash" convention (see BoxCollider.cpp's own
// degenerate half-extent handling, SphereCollider.cpp's own degenerate-
// center fallback, IsDegenerateColliderShape()'s own sibling precedent in
// Physics/ModelColliderDetection.cpp).
constexpr std::uint16_t GroupBit(std::uint8_t group) noexcept
{
    return static_cast<std::uint16_t>(1u << (group & 0x0Fu));
}

// task_manager/verlet-integration-10, PHASE1 - Bullet's own broad-phase
// collision-filter convention (confirmed against
// third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp's own
// `m_world->addRigidBody(rb, 1 << mmdRB->GetGroup(), mmdRB->GetGroupMask())`
// call, lines 149-154, and its own `GetGroup()`/`GetGroupMask()` accessors,
// lines 632-637) - a SYMMETRIC AND-test: A and B may collide only if A's own
// group bit is set in B's mask, AND B's own group bit is set in A's mask.
bool GroupsMayCollide(std::uint8_t groupA, std::uint16_t maskA, std::uint8_t groupB, std::uint16_t maskB) noexcept
{
    const std::uint16_t bitA = GroupBit(groupA);
    const std::uint16_t bitB = GroupBit(groupB);
    return (bitA & maskB) != 0 && (bitB & maskA) != 0;
}
```

Then replace the collision loop body:

```cpp
    if (definition.collisionEnabled) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            const std::uint8_t jointGroup = definition.jointSettings[i].group;
            const std::uint16_t jointMask = definition.jointSettings[i].collisionMask;
            for (const Collider& collider : colliders) {
                if (!GroupsMayCollide(jointGroup, jointMask, collider.group, collider.collisionMask)) {
                    continue; // task_manager/verlet-integration-10, PHASE1 - PMX collision-group/layer rule.
                }
                SolveCollision(state.particles[i], collider);
            }
        }
    }
```

Add `#include <cstdint>` to this `.cpp` file if not already transitively
available (it already includes headers that pull in `<cstdint>`
transitively via `DynamicChainDefinition.h`, but an explicit include is
cheap insurance and matches this codebase's own preference for explicit
includes).

**Why the filter check lives here, and not inside `SolveCollision()`
itself:** `SolveCollision()` (`Physics/Collider.h`) is documented as a pure,
engine-data-free dispatcher with zero knowledge of "chains"/"joints" — adding
a joint-side group/mask parameter to it would break that tiering (see
`PHASE0_MASTER_STRATEGY.md`'s "Architectural tiering" precedent, inherited
from verlet-integration-9). `DynamicChainSolver.cpp` already has both pieces
of information in scope (`definition.jointSettings[i]` for the joint,
`collider` for the Static body) at exactly the point the loop already
iterates both — the natural, minimal-diff location.

### 3.8 — Doc-comment updates (no code change)

- `src/Physics/DynamicChainSolver.h`'s own step-5 doc comment (already
  describing `if (definition.collisionEnabled) ... SolveCollision()`) gets one
  added sentence noting the new group/mask filter runs first, safely masking
  `group` via `GroupBit()` before ever using it as a shift amount.
- `src/Physics/Collider.h`'s own `SolveCollision()` doc comment gets one
  added sentence explicitly stating group/mask filtering is the CALLER's
  responsibility, never performed by `SolveCollision()` itself.

### 3.9 — New tests

**`tests/Physics/DynamicChainSolverCollisionGroupFilterTests.cpp`** (new file)
— exercises `StepDynamicChain()` directly (mirrors
`DynamicChainSolverTests.cpp`'s own `BuildThreeJointChainDefinition()`
helper-based style):

- `JointAndColliderInSameGroupDoCollide` — a 1-joint chain whose
  `jointSettings[0].group = 3`, `collisionMask = 0xFFFF`; a Sphere `Collider`
  whose `group = 3`, `collisionMask = 0xFFFF`, placed directly in the joint's
  falling path. After stepping, the joint must be pushed outside the sphere
  (mirrors the existing "EnabledCollisionKeepsJointsOffEveryColliderSurface..."
  test's own assertion style).
- `JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide` — same
  fixture, but the Sphere's `collisionMask = 0x0000` (or any mask with bit 3
  clear) — the joint must be found to have penetrated the sphere after
  stepping (a genuine regression guard proving the filter, not merely the
  ordinary "collision disabled" no-op — set `definition.collisionEnabled =
  true` explicitly here so this failure mode is provably about the GROUP
  filter, not the chain-level opt-in).
- `FilterIsSymmetricBothSidesMustAllow` — three sub-cases in one test
  (A allows B but B doesn't allow A; B allows A but A doesn't allow B;
  both allow each other) proving the AND is genuinely symmetric, not
  one-directional — reuse the same truth table `GroupsMayCollide()` itself
  documents by re-deriving it independently in the test (never call the
  production `GroupsMayCollide()` directly — it is `static`/anonymous-
  namespace, not exported — write a small local, independently-reasoned
  re-implementation in the test file itself, exactly like
  `PhysicsSystemMultiShapeColliderTests.cpp`'s own
  `IsOutsideSphere()`/`IsOutsideBox()`/`IsOutsideCapsule()` precedent of
  never calling the production math directly when independently verifying
  it).
- `DefaultGroupAndMaskReproduceOldUnconditionalCollisionBehavior` — a
  regression guard: a chain and collider BOTH left at their default-
  constructed `group = 0`/`collisionMask = 0xFFFF` still collide exactly like
  every pre-PHASE1 test already proved (this is effectively re-running
  `DynamicChainSolverTests.cpp`'s own existing
  `EnabledCollisionKeepsJointsOffEveryColliderSurfaceWhenChainFallsIntoIt`
  scenario, but written fresh in this new file to make the "defaults are
  still 100% backward compatible" guarantee an explicit, standalone,
  campaign-owned assertion rather than an implicit inference from an
  unrelated file continuing to pass).
- **`OutOfRangeGroupValueNeverCrashesAndStillMasksToTheCorrectLowFourBits`
  (NEW in v2, see Revision Notes finding #2)** — a direct, explicit
  regression guard for the shift-safety fix: build a joint/collider pair
  whose `group` fields are deliberately set to out-of-documented-range
  values that nonetheless share the SAME low 4 bits (e.g. `group = 3` for
  the joint, `group = 3 + 16 = 19` for the collider — `19 & 0x0F == 3`, the
  same bit as a "legitimate" group-3 value) with masks that allow that
  shared bit; assert they DO collide (proving the masking is applied
  consistently, not merely "does not crash") — and a second sub-case with
  `group = 255` on one side, asserting the call completes without triggering
  any sanitizer/assert failure (this sub-case's own primary purpose is
  running cleanly under UBSan/ASan in CI, if enabled, not a specific
  collision-outcome assertion, since `255 & 0x0F == 15`, a legitimately
  in-range bit, is a well-defined, sensible outcome anyway).

**`tests/Physics/ModelColliderDetectionGroupMaskTests.cpp`** (new file) —
exercises `DetectModelColliders()` directly:

- `StaticRigidBodyGroupAndMaskAreCopiedVerbatimIntoTheDetectedCollider` — one
  Static `RigidBody` with a deliberately non-default `group` (e.g. `7`) and
  `collisionGroupMask` (e.g. `0b0000000010100000`), confirm the resulting
  `ModelColliderDefinition::group`/`collisionMask` match exactly.
- `DefaultZeroGroupAndDefaultZeroMaskAreStillCopiedVerbatimNotSilentlyReplaced`
  — a Static `RigidBody` whose `group`/`collisionGroupMask` were never
  explicitly set by the test (both `0`, `Assets/PhysicsData.h`'s own actual
  default) — confirms the detected collider ends up with `group == 0`,
  `collisionMask == 0` (i.e. **verbatim copy, not the Collider-side "default
  to 0xFFFF" convenience** — that convenience lives ONLY on `Collider`/
  `DynamicJointSettings` for hand-built test fixtures that skip PHASE1's
  own 3.6 copy step entirely; `ModelColliderDefinition`/the real detection
  pipeline always copies whatever the PMX file actually said, even if that
  happens to be a literal `collisionGroupMask == 0`, which in real PMX
  authoring data would mean "collides with nothing" — a legitimate, if
  unusual, authored choice this engine must faithfully preserve, never
  silently override). **This test's existence is important: it is what
  proves PHASE1 did not accidentally change `ModelColliderDefinition::
  collisionMask`'s own default member-initializer's semantics away from
  "verbatim copy" into "always 0xFFFF regardless of the source RigidBody."**

### 3.10 — Register the new test files

Add both new file paths to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list,
grouped alongside their existing siblings
(`Physics/DynamicChainSolverTests.cpp` and
`Physics/ModelColliderDetectionTests.cpp` respectively), and add one entry
each to that file's own top-of-file taxonomy comment block, matching the
style/detail level of every neighboring entry.

## Step 4 — Confirmed zero regression (re-verify before moving to PHASE2)

- Every existing `Collider{...}`/`DynamicJointSettings{...}` positional
  aggregate-init call site across the entire test suite (confirmed by
  `search_in_dir` sweep in this campaign's own investigation — 27 matches for
  `Collider{` across 5 files, all exactly 4 positional args; 13 matches for
  `DynamicJointSettings{` across 3 files, all exactly 0 or 3 positional args)
  continues to compile and continues to produce IDENTICAL runtime behavior,
  because every new field is a TRAILING member with a default that
  reproduces "collides with everything" (the old, only-ever-existing
  behavior).
- `DynamicChainSolverTests.cpp`'s own existing collision tests
  ((e)/(f)/(f2) — `EnabledCollisionKeepsJointsOffEveryColliderSurface...`,
  `CollidersAreIgnoredWhenCollisionEnabledIsFalse`,
  `MultipleCollidersOfDifferentShapesAreAllRespectedSimultaneously`) all
  construct their own `Collider`s via the old 4-arg form — every one of them
  ends up with `group = 0`, `collisionMask = 0xFFFF`, and their
  `DynamicChainDefinition`'s own `jointSettings` (built via
  `BuildThreeJointChainDefinition()`'s 3-arg `DynamicJointSettings{...}`)
  likewise end up with `group = 0`, `collisionMask = 0xFFFF` — `GroupBit(0) &
  0xFFFF == nonzero` both directions, so `GroupsMayCollide()` returns `true`
  unconditionally for every one of these pre-existing tests, exactly
  preserving their already-passing behavior.
- `GroupBit(group)` for every already-in-range value (0-15) is textually and
  behaviorally identical to the v1 design's unmasked `1u << group` — masking
  only ever changes behavior for a value that was ALREADY undefined behavior
  before this fix, so there is no observable behavior change for any
  legitimately-authored PMX file, ever.
