# PHASE1 — Tree-Based Chain Data Model + Solver/Resolver Generalization (v2)

Part of the `verlet-integration-6` campaign — read `PHASE0_MASTER_STRATEGY.md`
first. This phase touches ZERO detection-algorithm code
(`DynamicChainDetection.h/.cpp` is not modified here at all) — it only
generalizes the DATA SHAPE a chain is expressed in, and the two runtime
consumers that currently hard-code the old flat-list shape. Land this,
compile, and get its own tests green BEFORE starting Phase 2.

**v2 note:** this phase's own underlying design/plan needed no correction —
the only change from v1 is in section 3.2 below, where the illustrative code
excerpt was self-contradictory (it showed one thing, then told the
implementer in prose not to actually write it that way). It now shows only
the correct, final form. See `PHASE0_MASTER_STRATEGY.md`'s own "Revision
Notes (v2)", finding #7, for the full rationale.

## Step 1: The Goal (Where are we going?)

`DynamicChainDefinition` currently represents a chain as an implicit flat
linked list: `jointBoneIndices[i]`'s "chain parent" (used both for the
rest-length distance constraint AND for which bone's rotation actually gets
rewritten to visually move it) is ALWAYS `jointBoneIndices[i - 1]` (or
`rootBoneIndex` for `i == 0`). After this phase, a chain is an explicit TREE:
every joint stores its OWN parent's position within the chain
(`parentJointIndex[i]`, `-1` meaning "my parent is `rootBoneIndex` itself"),
so a single `DynamicChainDefinition` can represent genuine branching (a
skirt's spider-web hub with 4+ children) instead of being forced into one
object per branch. Additionally, every chain gains a new,
independent list of `ExtraStructuralConstraint` entries — plain
particle-to-particle distance constraints between two joints that are NOT a
tree parent/child pair (the skirt's horizontal "ring brace" joints) — so the
simulation stays fully stabilized by every authored PMX Joint even though
only real skeleton parent/child edges can ever drive visible bone rotation
(see `PHASE0_MASTER_STRATEGY.md`, Culprit A, for exactly why that hard limit
exists and cannot be routed around without a much bigger change to how bones
are posed).

## Step 2: The Situation / The Problem (Where are we now?)

Three files hard-code "parent is `i - 1`" today, each independently, each
needing its own targeted fix:

1. **`src/Physics/DynamicChainDefinition.h`** — the struct itself has no
   concept of a tree at all. `restLengths[i]`'s own doc comment (lines 25-30)
   explicitly says "the distance from `jointBoneIndices[i-1]` to
   `jointBoneIndices[i]`" — this document is the field's own contract, and it
   is exactly the assumption that must change.
2. **`src/Physics/BoneChainPhysicsResolver.cpp`**, line 52:
   `const std::int32_t parentBoneIndex = (i == 0) ? definition.rootBoneIndex
   : definition.jointBoneIndices[i - 1];` — this is the literal line that
   decides which bone's rotation gets rewritten to make joint `i` land at its
   simulated position. `BoneChainPhysicsResolver.h`'s own big header comment
   (the "IMPORTANT DESIGN NOTE" section, lines 17-78) documents the FULL
   8-step algorithm around this line in prose; every one of those 8 steps
   still applies unchanged EXCEPT step 1's own definition of `parentBoneIndex`.
3. **`src/Physics/DynamicChainSolver.cpp`**, lines 94-97 (inside
   `StepDynamicChain()`'s "3. Constrain-structural" block):
   ```cpp
   SolveDistanceConstraint(anchor, state.particles[0], definition.restLengths[0]);
   for (std::size_t i = 1; i < jointCount; ++i) {
       SolveDistanceConstraint(state.particles[i - 1], state.particles[i], definition.restLengths[i]);
   }
   ```
   This is the physics-side mirror of the same assumption — which particle
   pair gets distance-constrained together, per structural iteration.

Two existing test files assert the OLD flat-list shape directly and will
need conservative additions (not a rewrite — this phase must NOT change
existing linear-chain behavior, only ADD tree/branch support):
`tests/Physics/BoneChainPhysicsResolverTests.cpp` and
`tests/Physics/DynamicChainSolverTests.cpp`. Two more files build
`DynamicChainDefinition` fixtures that will need one new field populated to
keep compiling once `parentJointIndex` becomes a real member with no default
that magically reconstructs "linear": `tests/Physics/
DynamicChainDefinitionTests.cpp`, `tests/Game/Physics/
DynamicChainRigCacheTests.cpp`, and `tests/Game/Physics/
PhysicsSystemParallelTests.cpp` — this phase touches all of these directly;
Phase 4 later re-confirms nothing was missed once Phase 3's detection
rewrite starts actually emitting `parentJointIndex` values other than
"linear."

## Step 3: The Plan (A very detailed strategy about how will we get there?)

### 3.1 `src/Physics/DynamicChainDefinition.h` — the new fields

Add two new members to `DynamicChainDefinition`, plus one new struct, plus
one small static helper:

```cpp
// A single non-hierarchy stabilizing constraint between two joints that are
// BOTH already members of this same chain's jointBoneIndices, but are NOT a
// parentJointIndex tree edge (see parentJointIndex's own doc comment below).
// Indices are POSITIONS within jointBoneIndices (0..jointBoneIndices.size()-1),
// never raw skeleton bone indices - mirrors jointBoneIndices' own "index into
// itself" convention used by parentJointIndex. Purely a Verlet
// SolveDistanceConstraint() pair (ChainConstraints.h) - never drives bone
// rotation (see BoneChainPhysicsResolver.h's own "IMPORTANT DESIGN NOTE" for
// exactly why only a REAL skeleton parent/child pair can ever do that).
// task_manager/verlet-integration-6, Phase 1/3 - this is how an MMD skirt's
// authored horizontal "ring brace" Joints (connecting two SIBLING bones, or
// two bones unrelated in the skeleton) still meaningfully stabilize the
// simulation even though they can never move a bone by themselves.
struct ExtraStructuralConstraint {
    std::int32_t jointIndexA = -1; // position within jointBoneIndices.
    std::int32_t jointIndexB = -1; // position within jointBoneIndices.
    float restLength = 0.0f;       // bind-pose distance between the two bones.
};

struct DynamicChainDefinition {
    std::int32_t rootBoneIndex = -1;
    std::vector<std::int32_t> jointBoneIndices;
    std::vector<DynamicJointSettings> jointSettings;

    // task_manager/verlet-integration-6, Phase 1 - EXPLICIT tree-parent per
    // joint, index-aligned 1:1 with jointBoneIndices. parentJointIndex[i] is
    // a POSITION within jointBoneIndices (never a raw bone index) of joint
    // i's own parent joint; -1 means "my parent is rootBoneIndex directly."
    // INVARIANT (relied on by BoneChainPhysicsResolver.cpp/DynamicChainSolver.cpp,
    // and re-verified by both files' own tests): parentJointIndex[i], if not
    // -1, MUST be < i (every joint's parent already has an earlier position
    // in this same array) - this guarantees a single top-to-bottom pass over
    // jointBoneIndices always processes a parent strictly before any of its
    // children, exactly like the OLD implicit "i-1" order already guaranteed
    // by construction. A chain builder (Phase 3) that violates this ordering
    // produces a definition that will silently fail to pose/simulate
    // correctly for the affected joint and every one of its descendants -
    // this is the single most important invariant in this whole campaign.
    // ALSO INVARIANT: skeleton.bones[jointBoneIndices[i]].parentBoneIndex
    // must equal (parentJointIndex[i] < 0 ? rootBoneIndex :
    // jointBoneIndices[parentJointIndex[i]]) - i.e. parentJointIndex must
    // always describe a REAL skeleton parent/child pair, never an arbitrary
    // graph edge (see this file's own header comment above
    // ExtraStructuralConstraint, and PHASE0's Culprit A). Chain builders
    // (Phase 3) are the ONLY code that may construct this array; hand-built
    // test fixtures must respect it too.
    //
    // A pre-Phase1 "flat list" chain is just the special case
    // parentJointIndex[i] == static_cast<std::int32_t>(i) - 1 for every i -
    // use DynamicChainDefinition::MakeLinearParentIndices() below to build
    // exactly that shape without repeating this logic at every call site.
    std::vector<std::int32_t> parentJointIndex;

    // task_manager/verlet-integration-6, Phase 1/3 - see
    // ExtraStructuralConstraint's own doc comment above. May be empty (the
    // overwhelmingly common case for a plain single-strand hair/tail chain
    // with no cross-bracing Joints at all).
    std::vector<ExtraStructuralConstraint> extraConstraints;

    // Bind-pose segment lengths, index-aligned with jointBoneIndices:
    // restLengths[i] is the bind-pose distance between jointBoneIndices[i]
    // and ITS OWN parentJointIndex-resolved parent (rootBoneIndex if -1) -
    // see parentJointIndex's own doc comment above for exactly which bone
    // that is. Precomputed once, never recomputed per frame.
    std::vector<float> restLengths;

    // ... gravityScale/windScale/constraintIterations/hasHeadCollider/
    // headColliderBoneIndex/headColliderRadius/maxPlausibleRootDelta
    // UNCHANGED, not reproduced here.

    // task_manager/verlet-integration-6, Phase 1 - convenience helper for
    // both hand-built test fixtures AND any future single-strand-only
    // caller: returns { -1, 0, 1, ..., jointCount - 2 }, the exact "flat
    // list" shape every chain implicitly had before this phase. Pass the
    // RESULT to a freshly-built DynamicChainDefinition's own
    // parentJointIndex field directly.
    static std::vector<std::int32_t> MakeLinearParentIndices(std::size_t jointCount);
};
```

Implement `MakeLinearParentIndices()` in `DynamicChainDefinition.cpp` (a
trivial loop: index `0` gets `-1`, index `i>0` gets `static_cast<std::int32_t>(i) - 1`).

Update `restLengths`' doc comment (quoted above) and delete the now-inaccurate
old wording ("restLengths[0] is the distance from rootBoneIndex to
jointBoneIndices[0]; restLengths[i] (i>0) is the distance from
jointBoneIndices[i-1] to jointBoneIndices[i]").

`FindDynamicChainJointByBoneIndex()` (`DynamicChainDefinition.cpp`) needs NO
change — it only ever searches `jointBoneIndices` by value, which is
unaffected by how joints relate to each other.

### 3.2 `src/Physics/BoneChainPhysicsResolver.cpp` — resolve the real parent via the tree

Replace line 52 exactly, AND extend the function's existing top-of-function
malformed-data early return (the `simulatedJointWorldPositions.size() !=
jointCount` check) so a size-mismatched `parentJointIndex` degrades
gracefully instead of ever being indexed out of bounds. This is the ONLY
correct final form — do not write a per-iteration bounds `continue` inside
the loop instead (an earlier draft of this document did exactly that, and it
was subtly wrong: it only ever guarded indices `i < parentJointIndex.size()`,
so any `i` at or beyond `parentJointIndex.size()` would still fall through to
an unguarded, out-of-bounds `definition.parentJointIndex[i]` read a few lines
below — see `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", finding
#7):

```cpp
// Top-of-function guard (existing line, extended):
if (simulatedJointWorldPositions.size() != jointCount || definition.parentJointIndex.size() != jointCount) {
    return; // Malformed/stale input - never read/write out of bounds.
}

// ... (unchanged: the pose-growing block, and the top of the `for` loop) ...

// OLD (line 52, deleted):
// const std::int32_t parentBoneIndex = (i == 0) ? definition.rootBoneIndex : definition.jointBoneIndices[i - 1];

// NEW (replaces line 52 - safe because the size check above already
// guarantees definition.parentJointIndex.size() == jointCount, so every
// `i` in this loop's own [0, jointCount) range is always a valid index):
const std::int32_t parentJoint = definition.parentJointIndex[i];
const std::int32_t parentBoneIndex = (parentJoint < 0)
    ? definition.rootBoneIndex
    : definition.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
```

No other line in this function changes — steps 2 through 8 of the algorithm
(`parentWorld`, `currentChildWorld`, the axis/angle computation, the
`grandparentBoneIndex` lookup, the final `pose[parentBoneIndex].rotation`
write) all operate purely in terms of `parentBoneIndex`, which is now
correctly resolved from the tree instead of assumed to be `i - 1`. Update
`BoneChainPhysicsResolver.h`'s own big header comment (step 1's prose,
lines 44-47) to describe the same `parentJointIndex`-based resolution instead
of the old `i-1` wording, so the header stays byte-accurate documentation of
the actual code (this file's own established convention — every other
campaign's Revision Notes call out exactly this kind of doc/code drift as a
real defect class to avoid).

### 3.3 `src/Physics/DynamicChainSolver.cpp` — structural constraints via the tree + extra constraints

Replace lines 83-98 (the whole "3. Constrain-structural" block):

```cpp
// NEW:
const int iterations = static_cast<int>(definition.constraintIterations);
for (int iter = 0; iter < iterations; ++iter) {
    VerletParticle anchor;
    anchor.position = rootWorldPosition;
    anchor.previousPosition = rootWorldPosition;
    anchor.inverseMass = 0.0f;
    anchor.pinned = true;

    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t parentJoint = definition.parentJointIndex[i];
        if (parentJoint < 0) {
            SolveDistanceConstraint(anchor, state.particles[i], definition.restLengths[i]);
        } else {
            SolveDistanceConstraint(state.particles[static_cast<std::size_t>(parentJoint)], state.particles[i],
                definition.restLengths[i]);
        }
    }
    for (const ExtraStructuralConstraint& extra : definition.extraConstraints) {
        if (extra.jointIndexA < 0 || extra.jointIndexB < 0
            || static_cast<std::size_t>(extra.jointIndexA) >= jointCount
            || static_cast<std::size_t>(extra.jointIndexB) >= jointCount) {
            continue; // Malformed/stale - never read/write out of bounds.
        }
        SolveDistanceConstraint(state.particles[static_cast<std::size_t>(extra.jointIndexA)],
            state.particles[static_cast<std::size_t>(extra.jointIndexB)], extra.restLength);
    }
}
```

IMPORTANT ordering note to add as a code comment at this exact spot: the tree
edges are solved in ascending `i` order (parent-before-child, guaranteed by
`parentJointIndex`'s own invariant from 3.1), then `extraConstraints` are
solved AFTER every tree edge, every iteration — mirrors standard PBD practice
of resolving the "primary" structure before "secondary" bracing constraints
within the same relaxation pass, and keeps `extraConstraints` fully order-
independent among themselves (each is a simple, symmetric pairwise
correction with no ordering dependency on any other extra constraint).

Also update the top-of-function malformed-data guard (line 46-49) to add
`|| definition.parentJointIndex.size() != jointCount` to the existing
`||`-chained early-return condition, exactly like 3.2's own resolver fix, so
a malformed/stale definition degrades gracefully instead of reading out of
bounds. Update `DynamicChainSolver.h`'s own header-comment prose for step 3
(lines 49-54) to describe the tree-based resolution + the new
`extraConstraints` pass, replacing the old "anchor for the first joint, then
i-1→i pairs" wording.

`ChainConstraints.h`/`.cpp` (`SolveDistanceConstraint`/`SolveGoalConstraint`)
need **NO changes at all** — both already operate on two arbitrary
`VerletParticle&` references with no assumption about which array positions
they came from; this phase only changes WHICH pairs get passed to the
existing, unmodified function.

### 3.4 Update existing tests (additive, never remove existing coverage)

- **`tests/Physics/DynamicChainDefinitionTests.cpp`** — every hand-built
  `DynamicChainDefinition` fixture (`chain0`, `chain1`, lines 18/22) must now
  also set `.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(chain0.jointBoneIndices.size())`
  (or the literal equivalent) so the object remains well-formed per 3.1's new
  invariant. Add one new test,
  `MakeLinearParentIndicesProducesExpectedShapeForVariousJointCounts`,
  covering `jointCount` = 0, 1, and 3+ (expect `{}`, `{-1}`, and
  `{-1, 0, 1}` respectively).
- **`tests/Physics/BoneChainPhysicsResolverTests.cpp`** — every existing
  fixture (lines 51, 128, 162, 180) gains the same
  `.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(...)`
  call, keeping every existing assertion's expected behavior byte-identical
  (a linear chain resolves to the exact same `parentBoneIndex` sequence
  either way — this is the whole point of the helper). Add ONE new test,
  `BranchingTreeRotatesTheSharedParentTowardBothChildrenIndependently`: a
  3-bone skeleton (root=0, parentA=1 child of 0, childB=2 ALSO child of 0 —
  i.e. two joints sharing the same real skeleton parent bone),
  `jointBoneIndices = {1, 2}`, `parentJointIndex = {-1, -1}` (both joints'
  parent is `rootBoneIndex` directly, matching the skeleton), and two
  DIFFERENT `simulatedJointWorldPositions` targets — assert
  `ApplyDynamicChainPhysicsToPose()` does not crash/misbehave when TWO
  different joints in the same call both resolve to the SAME
  `parentBoneIndex` (this is the exact "hub" scenario Phase 3 will construct
  for a spider-web skirt, and this test proves the resolver already handles
  it correctly once 3.2 lands — each iteration independently overwrites
  `pose[parentBoneIndex].rotation` from scratch based on its own `i`'s
  target, so processing joint 2 after joint 1 in the SAME call will
  overwrite joint 1's own corrective rotation; document this explicitly as a
  KNOWN, ACCEPTED limitation of a shared-parent hub in the test's own
  comment — Phase 3's own document addresses this directly, see its "Known
  Limitation" callout). Add a SECOND new test,
  `MismatchedParentJointIndexSizeIsIgnoredGracefully`: build an otherwise
  well-formed 2-joint chain but leave `parentJointIndex` at its
  default-constructed empty size (simulating stale/malformed data), call
  `ApplyDynamicChainPhysicsToPose()`, and assert `pose` is left completely
  untouched (the extended top-of-function guard from 3.2 fires) — this is
  the direct regression test proving the corrected guard actually prevents
  the out-of-bounds read the original (rejected) per-iteration snippet would
  not have.
- **`tests/Physics/DynamicChainSolverTests.cpp`** — every existing fixture
  (lines 22, 169) gains the same `.parentJointIndex =
  DynamicChainDefinition::MakeLinearParentIndices(...)` call. Add ONE new
  test, `ExtraStructuralConstraintPullsTwoNonAdjacentParticlesTogether`: a
  3-joint linear chain plus one `ExtraStructuralConstraint{0, 2, restLength}`
  (bracing joint 0 directly to joint 2, skipping joint 1) — seed particles
  far enough apart that the extra constraint has real work to do, call
  `StepDynamicChain()` for enough fixed steps to converge, and assert the
  final distance between `state.particles[0].position` and
  `state.particles[2].position` is within tolerance of `restLength` (proof
  the new extra-constraint loop in 3.3 actually executes and converges).

## Revision Notes (v2)

This phase's own algorithm/data-model plan was independently re-verified
against the current source tree during the v2 review and found fully
accurate (every quoted line number/excerpt in Step 2 above still matches the
real files exactly). The ONLY change made in this revision is section 3.2's
code excerpt: the v1 draft showed an incorrect intermediate per-iteration
`continue` guard, immediately followed by prose telling the implementer to
actually fold the guard into the top-of-function early return instead — a
self-contradiction that could have led an implementer to transcribe the
wrong, subtly out-of-bounds version literally. v2 shows only the correct
final form directly, with no contradictory intermediate step, and adds one
new regression test (`MismatchedParentJointIndexSizeIsIgnoredGracefully`,
3.4) proving the corrected guard actually degrades gracefully. See
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", finding #7, for the
cross-campaign summary of this same fix.
