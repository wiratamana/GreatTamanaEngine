# PHASE2 — Bone-Chain Physics Bridge (v2, consumer note added in v3, generic naming applied in v4)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprits A and E). Depends on: Phase 1
(`src/Physics/VerletParticle.h`, `VerletIntegration.h`,
`ChainConstraints.h`, `WindField.h`, all compiling and tested). Adds
to: `src/Physics/` (chain-level orchestration) and `src/Animation/` (one
small, additive extraction). Produces: a pure function that steps a whole
dynamic bone chain for one fixed timestep, and a second pure function that
turns the result back into `BoneLocalOffset` values the existing FK pipeline
already understands.

**v3 consumer note (no functional change to this phase):** Phase 3 was
rewritten to call every function this phase produces
(`ComputeBoneWorldMatrix()`, `StepDynamicChain()`, `ApplyDynamicChainPhysicsToPose()`)
from a brand-new, standalone `PhysicsSystem::Update()`
(`src/Game/Physics/PhysicsSystem.h/.cpp`) instead of from inside
`AnimationSystem::Update()`. Nothing in THIS phase changes as a result —
every deliverable below is already pure, ECS-free, engine-data-only, exactly
as required — only the identity of the future caller is different. Do not
add any ECS/`AnimationSystem`/`PhysicsSystem` dependency to any file in
this phase; that would violate this phase's own Step 4 scope and Phase 3's
own "zero cross-system reach-through" rule.

**v4 note (naming only, no functional change):** every type/function this
phase introduces is renamed to a generic, hair-free equivalent, per
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v4)": `HairChainDefinition`
→ `DynamicChainDefinition`, `HairJointSettings` → `DynamicJointSettings`,
`HairChainRuntimeState` → `DynamicChainRuntimeState`, `HairChainSolver` →
`DynamicChainSolver`, `StepHairChain()` → `StepDynamicChain()`,
`ApplyHairChainPhysicsToPose()` → `ApplyDynamicChainPhysicsToPose()`. This
phase's own math/algorithms are entirely unaffected.

## Step 1: The Goal

Bridge two worlds that currently know nothing about each other: Phase 1's
generic `VerletParticle` chain (plain 3D points, no concept of "bone") and
`src/Animation/`'s `SkeletonData`/`BoneLocalOffset` pipeline (bones,
hierarchy, rotations, no concept of "physics"). At the end of this phase,
given (a) a description of which bones form a chain and (b) that chain's
already-simulated particle positions, we can produce a `BoneLocalOffset`
array patch that — merged into the pose the existing pipeline already
computes — makes `ComputeSkinningMatrices()` bend those bones toward the
simulated positions, with **zero changes** to `SkeletonPose.cpp` itself.

## Step 2: The Situation / The Problem

- A `VerletParticle` (Phase 1) only has a position — no orientation. A bone's
  skinning matrix, however, is built from a **rotation** (`BoneLocalOffset::
  rotation`, see `Animation/BonePoseMath.h`'s `ComputeBoneLocalMatrix()`).
  Simply writing a particle's world position into a bone's translation would
  be wrong twice over: PMX bones don't carry a translatable bind offset the
  way the engine's local-offset formula expects (see `SkeletonPose.h`'s own
  file comment: *"every bone is implicitly unrotated in its bind pose"* —
  translation-only bind poses are exactly why `ComputeSkinningMatrices()`
  never needs to touch a bone's translation channel for a purely FK-driven
  bone), and it would silently orphan every VERTEX skinned to that bone,
  since skinning follows the bone's matrix, not a floating point in space.
  **The correct approach — the same one Cyclic-Coordinate-Descent IK already
  uses in this codebase — is to derive a ROTATION that aims the bone from
  its (already-resolved) parent world position toward the simulated child
  position**, and write that rotation into `BoneLocalOffset::rotation`.
- Doing that derivation needs "this bone's current world matrix, as of the
  pose so far" — and, per Culprit E (`PHASE0`), the one function that already
  does exactly this, `ComputeBoneWorldMatrix()`, is `static`-scoped inside
  `IkSolver.cpp`'s anonymous namespace, invisible outside that translation
  unit.

## Step 3: The Plan

### 3.1 Promote the shared bone-world-matrix query

Extract `IkSolver.cpp`'s anonymous-namespace `ComputeBoneWorldMatrix()`
verbatim into a new, public header:

`src/Animation/BoneWorldMatrixQuery.h` (new file):
```cpp
#pragma once
#include "BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Mat4.h"

#include <vector>

namespace gte {

// A single bone's CURRENT world matrix, re-derived fresh from `pose` every
// call by walking only that bone's own ancestor chain
// (BoneChainResolver.h's ResolveSingleBoneChain()) - deliberately NOT
// memoized across calls, since `pose` may be mutated between successive
// queries by a caller mid-solve (IkSolver.cpp's CCD loop; DynamicChainSolver.h's
// per-joint aim-solve below - see this file's own Step 3.2). Shares the
// exact same bind-relative local-transform formula SkeletonPose.cpp uses
// (BonePoseMath.h's ComputeBoneLocalMatrix()), so every caller of this
// function can never silently drift out of sync with the FK pass itself.
//
// Promoted out of IkSolver.cpp (where it originated) into this shared
// header specifically so DynamicChainSolver.h (src/Physics/) can reuse it
// rather than hand-rolling a second, independent copy of this exact
// cycle-guarded ancestor walk - see AGENTS.md, "Skeletal Animation Pose
// Resolution": "Never hand-roll a new cycle-guarded bone-ancestor-chain
// walk - use Animation/BoneChainResolver.h's ... instead."
Mat4 ComputeBoneWorldMatrix(const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex);

} // namespace gte
```

Move the *implementation* into a new `src/Animation/BoneWorldMatrixQuery.cpp`
(pure `#include`s: `BoneChainResolver.h`, `BonePoseMath.h`), byte-identical
body to the version being removed from `IkSolver.cpp`. Update
`IkSolver.cpp`: delete the anonymous-namespace copy, `#include
"BoneWorldMatrixQuery.h"`, and confirm every existing call site inside
`SolveIkChains()` still compiles unchanged (it will — same function name/
signature, just no longer `static`). Add both new files to `CMakeLists.txt`'s
`gte_core` source list, directly alongside the existing
`src/Animation/IkSolver.*` entries. **No test behavior changes** — this is a
pure refactor; `tests/Animation/IkSolverTests.cpp` must still pass
unmodified, byte-for-byte, proving the extraction didn't alter behavior.

### 3.2 `src/Physics/DynamicChainDefinition.h` (new)

Plain, engine-native, no-behavior data — describes WHICH bones form one
dynamic bone chain and their LOCAL (per-joint) tuning. Deliberately modeled
after `PhysicsData.h`'s own "plain struct, produced by an importer/authoring
step, consumed read-only" shape:

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace gte {

// LOCAL (per-joint) tuning - see PHASE4's "global vs. local" split.
struct DynamicJointSettings {
    float damping = 0.08f;      // "Damping" - see VerletIntegration.h.
    float stiffness = 0.35f;    // "Stiffness" (goal constraint) - see ChainConstraints.h's SolveGoalConstraint().
    float mass = 1.0f;          // "Weight" - inverse-mass fed to VerletParticle::inverseMass (must be > 0).
};

// One dynamic bone chain (a linear run of physics-simulated bones on one
// model - any number of independent chains may coexist on the same
// skeleton, sharing no state with each other) - `rootBoneIndex` is NOT
// simulated (it is the pinned anchor, always taken directly from the
// animated FK pose every step); `jointBoneIndices` is the ordered list of
// bones that ARE simulated, root-to-tip, each one's parent in the chain
// being the previous entry (or rootBoneIndex for the first).
struct DynamicChainDefinition {
    std::int32_t rootBoneIndex = -1;
    std::vector<std::int32_t> jointBoneIndices;
    std::vector<DynamicJointSettings> jointSettings; // index-aligned 1:1 with jointBoneIndices.

    // Bind-pose segment lengths, index-aligned with jointBoneIndices:
    // restLengths[0] is the distance from rootBoneIndex to jointBoneIndices[0]
    // in the BIND pose; restLengths[i] (i>0) is the distance from
    // jointBoneIndices[i-1] to jointBoneIndices[i]. Precomputed once (see
    // PHASE4's chain-building step) directly from SkeletonData::Bone::position
    // - never recomputed per frame.
    std::vector<float> restLengths;

    float gravityScale = 1.0f; // LOCAL multiplier applied to the GLOBAL gravity vector (see PHASE4).
    float windScale = 1.0f;    // LOCAL multiplier applied to the GLOBAL WindSettings (see PHASE4).
    std::uint8_t constraintIterations = 4; // structural relaxation passes per fixed step - see ChainConstraints.h.
};

} // namespace gte
```

### 3.3 `src/Physics/DynamicChainRuntimeState.h` (new)

The first genuinely **mutable, frame-to-frame-persistent** piece of state in
the animation stack (Culprit C) — deliberately its own header, deliberately
NOT merged into `DynamicChainDefinition` (which stays plain, reusable,
never-mutated authoring data, mirroring `AGENTS.md`'s ECS rule that
authoring/definition data and runtime/mutable state are always kept in
separate structs):

```cpp
#pragma once
#include "../Physics/VerletParticle.h"

#include <vector>

namespace gte {

// Persistent, per-INSTANCE simulation memory for one DynamicChainDefinition -
// owned by whichever entity/component is simulating it (see PHASE3's new
// DynamicChainRig component), one DynamicChainRuntimeState per
// DynamicChainDefinition, never shared across two different entities animating
// the same underlying model (this mirrors AnimationSystem's own existing,
// documented "two entities sharing one *.gta currently fight over shared
// state" limitation - see AGENTS.md's Job System table - a per-entity
// runtime state is exactly what avoids that same trap here).
struct DynamicChainRuntimeState {
    // Index-aligned with DynamicChainDefinition::jointBoneIndices - resized
    // and (re-)seeded to the animated bind pose the first time
    // StepDynamicChain() (below) is ever called for this instance (see its
    // own "lazy init" step) so the chain doesn't visibly "fall" from the
    // origin on its very first frame.
    std::vector<VerletParticle> particles;
    bool initialized = false;

    // Running simulation clock, in seconds, advanced by exactly
    // fixedDeltaTime every StepDynamicChain() call - fed to
    // ComputeWindAcceleration() (Phase 1) so wind phase is continuous
    // across frames rather than resetting.
    float simulationTimeSeconds = 0.0f;
};

} // namespace gte
```

### 3.4 `src/Physics/DynamicChainSolver.h` / `.cpp` (new)

The chain-level orchestration — the direct analog of
`Animation/IkSolver.h`'s `SolveIkChains()`, but for physics instead of IK,
and operating on Phase 1's particles instead of `BoneLocalOffset` directly
(the conversion back to `BoneLocalOffset` is a SEPARATE function, 3.5 below —
keep the two concerns split, mirroring how `AnimationPoseEvaluator.h` keeps
sampling/IK/append/FK as four separately-testable steps rather than one
monolith):

```cpp
#pragma once
#include "DynamicChainDefinition.h"
#include "DynamicChainRuntimeState.h"
#include "WindField.h"
#include "../Math/Vec3.h"

namespace gte {

// Advances ONE dynamic bone chain by exactly one FIXED timestep.
// `rootWorldPosition` and `animatedJointWorldPositions` (index-aligned with
// definition.jointBoneIndices) are wherever PLAIN forward-kinematics
// animation (no physics at all) puts the chain's bones THIS frame - the
// "goal"/pin targets - computed by the Phase 3 caller via
// Animation/BoneWorldMatrixQuery.h's ComputeBoneWorldMatrix() BEFORE calling
// this function. `gravity` is the GLOBAL world gravity vector; `wind` is the
// GLOBAL wind description (see PHASE4) - both scaled locally by
// definition.gravityScale/windScale.
//
// Per-call steps:
//   1. Lazy init (state.initialized == false): resize state.particles to
//      definition.jointBoneIndices.size(), set every particle's position AND
//      previousPosition to its corresponding animatedJointWorldPositions[i]
//      (zero implied velocity, so frame 1 never "snaps"/free-falls from the
//      origin), inverseMass = 1.0f / max(jointSettings[i].mass, small
//      epsilon), pinned = false. Set state.initialized = true.
//   2. Integrate: for each particle i, acceleration = gravity *
//      definition.gravityScale + ComputeWindAcceleration(wind,
//      particle.position, state.simulationTimeSeconds) * definition.windScale;
//      call IntegrateParticle(particle, fixedDeltaTime, acceleration,
//      jointSettings[i].damping).
//   3. Constrain-structural: repeat definition.constraintIterations times:
//      SolveDistanceConstraint against a temporary anchor particle pinned at
//      rootWorldPosition (inverseMass 0, pinned = true) for the first joint
//      (restLengths[0]), then between consecutive joint particles for every
//      later one (restLengths[i]). ONLY the structural/distance constraint
//      is repeated here.
//   4. Constrain-goal (v2 fix - see PHASE0's Revision Notes, finding #3 -
//      this step MUST run exactly ONCE per call, OUTSIDE/AFTER the
//      constraintIterations loop above, never inside it): for every joint i,
//      SolveGoalConstraint(particles[i], animatedJointWorldPositions[i],
//      jointSettings[i].stiffness). Applying this once, after structural
//      relaxation has already converged the rod lengths for this step, is
//      what keeps `stiffness` (a per-joint "how much to keep the animated
//      shape" knob) and `constraintIterations` (a chain-level rod-rigidity/
//      performance knob) fully independent: repeating a Lerp-based blend N
//      times geometrically compounds it (`stiffness` applied N times inside
//      a loop pulls `1-(1-stiffness)^N` of the way to the target, not plain
//      `stiffness`), which would silently make the chain feel "stiffer"
//      purely because a tuner raised `constraintIterations` for an unrelated
//      (perf/rod-rigidity) reason - exactly the coupling bug v1 of this
//      document had.
//   5. state.simulationTimeSeconds += fixedDeltaTime.
//
// Degrades gracefully (does nothing) if any of the three index-aligned
// arrays (jointBoneIndices/jointSettings/restLengths,
// animatedJointWorldPositions) disagree in size - a malformed/stale
// definition must never read or write out of bounds.
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind);

} // namespace gte
```

Implement exactly per the doc comment, calling straight into Phase 1's
`IntegrateParticle`/`SolveDistanceConstraint`/`SolveGoalConstraint`/
`ComputeWindAcceleration` — this function contains **no** new physics math of
its own, only orchestration/looping. Use a local, stack-allocated
`VerletParticle` for the temporary root anchor each call (never stored in
`state` — the root is never itself simulated, only referenced).

### 3.5 `src/Physics/BoneChainPhysicsResolver.h` / `.cpp` (new)

The position→rotation conversion — reuses `Animation/
BoneWorldMatrixQuery.h`'s newly-shared `ComputeBoneWorldMatrix()` (3.1) and
`Animation/BonePoseMath.h`'s bind-offset convention:

```cpp
#pragma once
#include "DynamicChainDefinition.h"
#include "../Animation/BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Vec3.h"

#include <vector>

namespace gte {

// Rewrites `pose[boneIndex]` for every bone in `definition.jointBoneIndices`
// so that bone's WORLD direction (parent -> bone) points at
// `simulatedJointWorldPositions[i]` instead of wherever plain FK left it -
// the physics payoff of this whole campaign. Must be called AFTER
// Animation/AppendBoneSolver.h's ApplyAppendInheritance() and BEFORE
// Animation/SkeletonPose.h's ComputeSkinningMatrices() - see PHASE3's own
// pipeline-ordering rule.
//
// Per joint i, in root-to-tip order (each iteration depends on the
// PREVIOUS joint's own already-rewritten `pose` entry, exactly like
// IkSolver's own CCD chain - never process joints out of order or in
// parallel against the SAME chain):
//   1. parentWorld = ComputeBoneWorldMatrix(skeleton, pose, parentBoneIndex)
//      where parentBoneIndex is definition.rootBoneIndex for i==0, else
//      definition.jointBoneIndices[i-1].
//   2. parentWorldPos = parentWorld.TransformPoint(Vec3::Zero()).
//   3. currentWorld = ComputeBoneWorldMatrix(skeleton, pose, jointBoneIndices[i]).
//      currentDir = Normalize(currentWorld.TransformPoint(Vec3::Zero()) - parentWorldPos)
//      - the bone's direction BEFORE this physics pass (i.e. wherever plain
//      FK/IK/append left it).
//   4. targetDir = Normalize(simulatedJointWorldPositions[i] - parentWorldPos).
//   5. Skip (leave pose[boneIndex] untouched) if either direction is
//      degenerate (near-zero length) or already ~parallel (dot > 1 -
//      kMinAngleRadians-equivalent, mirroring IkSolver.cpp's own
//      kMinDirectionLengthSq/kMinAngleRadians guards - reuse the same
//      literal tolerances for consistency).
//   6. axis = Normalize(Cross(currentDir, targetDir)); angle = acos(Clamp(Dot(currentDir, targetDir), -1, 1)).
//   7. delta = Quat::FromAxisAngle(axis, angle) - expressed in WORLD space
//      here (unlike IkSolver's link rotation, which is expressed in the
//      LINK's own local space because it accumulates onto an existing
//      partial rotation across MANY iterations) - because this function
//      only ever applies ONE corrective rotation per bone per frame, it is
//      simplest and correct to rotate the bone's EXISTING world rotation by
//      `delta` directly: newWorldRotation = delta * Quat::FromMat4(currentWorld).
//   8. Convert newWorldRotation back to this bone's LOCAL offset rotation by
//      removing the parent's world rotation:
//      pose[boneIndex].rotation = Normalize(Quat::FromMat4(parentWorld).Inverse() * newWorldRotation).
//      (pose[boneIndex].translation is left UNCHANGED - PMX bones never
//      need a translation channel for a purely-rotated FK bend, matching
//      SkeletonPose.h's own bind-pose convention.)
void ApplyDynamicChainPhysicsToPose(const SkeletonData& skeleton, const DynamicChainDefinition& definition,
    const std::vector<Vec3>& simulatedJointWorldPositions, std::vector<BoneLocalOffset>& pose);

} // namespace gte
```

Implement exactly per the numbered steps — this mirrors `IkSolver.cpp`'s own
axis/angle derivation almost line-for-line (reuse its literal tolerance
constants, `kMinDirectionLengthSq`/`kMinAngleRadians`-equivalent values, by
either sharing them via a small shared constants header or simply
redeclaring the same numeric literals locally with a comment cross-
referencing `IkSolver.cpp`).

### 3.6 CMake + tests

Add `src/Animation/BoneWorldMatrixQuery.h/.cpp`,
`src/Physics/DynamicChainDefinition.h`, `DynamicChainRuntimeState.h`,
`DynamicChainSolver.h/.cpp`, `BoneChainPhysicsResolver.h/.cpp` to
`CMakeLists.txt`. Add:

- `tests/Animation/BoneWorldMatrixQueryTests.cpp` — a small skeleton (3–4
  bones), confirm the extracted function produces identical results to what
  `IkSolverTests.cpp` already implicitly exercises (e.g. re-derive one known
  IK test fixture's expected world position through the new public
  function).
- `tests/Physics/DynamicChainSolverTests.cpp` — a 3-joint chain: (a) with
  `stiffness = 1.0` on every joint and zero gravity/wind, the chain stays
  exactly glued to the animated target every frame (proves the goal
  constraint dominates correctly); (b) with `stiffness = 0.0` and gravity
  pointing down, the chain visibly sags below its animated target over
  several steps but never stretches past `sum(restLengths) * 1.01` (proves
  the distance constraint bounds it); (c) calling `StepDynamicChain` twice in
  a row with an unchanged animated target and zero gravity/wind converges
  toward a stable rest position rather than oscillating forever (numerical
  sanity); (d) (v2 addition — PHASE0 Revision Notes finding #3) a
  regression test proving `stiffness`/`constraintIterations` are properly
  DECOUPLED — running one `StepDynamicChain` call with
  `constraintIterations == 1` vs. an otherwise-identical call with
  `constraintIterations == 8` (same `stiffness`, same single fixed timestep,
  same starting particle positions away from the animated target) must pull
  the goal-constrained particle(s) toward `animatedJointWorldPositions` by
  the SAME fraction of the remaining distance in both cases (within
  floating-point tolerance) — i.e. changing `constraintIterations` alone
  must never change how strongly `stiffness` pulls toward the animated
  target; only the rod-length convergence may differ between the two runs.
- `tests/Physics/BoneChainPhysicsResolverTests.cpp` — a straight 2-bone bind
  pose, simulate one joint's target sideways by a known offset, confirm
  `ApplyDynamicChainPhysicsToPose()` produces a `BoneLocalOffset::rotation`
  that, when fed back through `ComputeBoneWorldMatrix()`, actually lands the
  bone at (approximately) the requested simulated position — a genuine
  round-trip correctness test, not just "it compiles."
- (v2 addition — PHASE0 Revision Notes finding #6) Add a matching
  descriptive paragraph for each new test file above to
  `tests/CMakeLists.txt`'s own header "Test taxonomy" comment block, in the
  same style/level of detail as every existing entry there.

## Step 4: What We Will NOT Do

- We will **not** solve for TWIST/roll around each bone's own forward axis —
  a chain of position-only particles has no roll information to recover;
  each bone keeps whatever roll its bind pose/parent implies. This is the
  same accepted simplification `IkSolver.cpp` already documents for its own
  axis/angle CCD step (*"not a bit-perfect reimplementation"*).
- We will **not** attempt to make `ApplyDynamicChainPhysicsToPose()` handle
  branching chains (a bone with more than one physics child) — a
  `DynamicChainDefinition` is a single, linear list; a model with a
  branching physics-driven bone structure needs two or more separate
  `DynamicChainDefinition`s sharing a root, not one chain that branches
  internally.
- We will **not** re-derive `IkSolver.cpp`'s exact CCD accumulation style
  (local-space delta composed onto an existing rotation across many
  iterations) — this function only ever applies one corrective rotation per
  bone per frame, so the simpler world-space formulation in 3.5, step 7, is
  correct and sufficient; do not "upgrade" it to match IK's own style later
  without a concrete reason.

## Step 5: Their Role

1. Do the `IkSolver.cpp` extraction (3.1) FIRST, in complete isolation, and
   re-run `tests/Animation/IkSolverTests.cpp` before writing anything else in
   this phase — a failure here means the extraction itself broke something,
   and nothing downstream should be trusted until it's green.
2. Implement `DynamicChainDefinition.h`/`DynamicChainRuntimeState.h` (plain
   data, no logic — should compile trivially).
3. Implement `DynamicChainSolver.h/.cpp` (3.4), calling only Phase 1 functions.
4. Implement `BoneChainPhysicsResolver.h/.cpp` (3.5), calling only
   `BoneWorldMatrixQuery.h` (3.1) and `Math/`.
5. Add every new file to `CMakeLists.txt`, write the four test files in 3.6,
   register them in `tests/CMakeLists.txt`, and get a fully green
   `GreatTamanaEngineTests` run before starting Phase 3 — Phase 3 wires
   these two functions into a live, standalone `PhysicsSystem` (v3/v4), and
   a bug here would otherwise be far harder to isolate once it's running
   inside that system's own per-frame `Update()`.
