# PHASE1 — Anchor-Bone Immutability Fix In `BoneChainPhysicsResolver.cpp`

Implements the ONE code change that fixes the reported bug. Depends on
nothing outside today's `src/` tree. Must compile and pass its own rewritten
test file before Phase 2/3 begin.

## Step 1: The Goal (Where are we going?)

`ApplyDynamicChainPhysicsToPose()` (`src/Physics/BoneChainPhysicsResolver.cpp`)
must land every joint of every chain at its own simulated world position
**without ever writing to `pose[definition.rootBoneIndex]`** — not the
rotation, not the translation, not for any joint, not for any chain, ever.
This makes `DynamicChainDefinition.h`'s own documented `rootBoneIndex`
contract ("NOT simulated... always taken directly from the animated FK pose
every step") literally true in code for the first time. As a direct
byproduct, any number of joints that share the anchor as their direct
tree-parent (a "root-level hub," exactly the reported model's shape — two
dozen or so accessory strands hanging off `下半身`) must each independently
land at their own distinct target position in the SAME call, with no
"last-processed child wins" interference between them.

## Step 2: The Situation (exact current code)

`src/Physics/BoneChainPhysicsResolver.cpp`, the per-joint loop (today's lines
41-119), does this for **every** joint `i`, with no distinction between "my
parent is another chain bone" and "my parent is the anchor itself":

```cpp
for (std::size_t i = 0; i < jointCount; ++i) {
    const std::int32_t boneIndex = definition.jointBoneIndices[i];
    if (boneIndex < 0 || out of range) continue;

    const std::int32_t parentJoint = definition.parentJointIndex[i];
    const std::int32_t parentBoneIndex = (parentJoint < 0)
        ? definition.rootBoneIndex                                   // <-- THE BUG: the anchor itself
        : definition.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
    if (parentBoneIndex < 0 || out of range) continue;

    // ... compute a corrective rotation so `boneIndex` aims at
    // simulatedJointWorldPositions[i] ...
    pose[static_cast<std::size_t>(parentBoneIndex)].rotation = /* ... */;
}
```

When `parentJoint < 0` (this joint's tree-parent is the anchor directly —
the Bone Viewer's `(root child)` label), `parentBoneIndex` becomes
`definition.rootBoneIndex` and the corrective rotation lands on
**`pose[rootBoneIndex]`** — a real, shared, load-bearing skeleton bone for
any model whose PMX rig reuses an ordinary FK bone (`下半身`) as a chain
anchor. Every other `(root child)` joint in the same chain does the exact
same thing, to the exact same `pose[rootBoneIndex]` entry, later overwriting
whatever the previous one just wrote.

## Step 3: The Plan (exact new code)

### 3.1 — The math (derive once, use directly in code)

Shared formula already used throughout this codebase
(`Animation/BonePoseMath.h::ComputeBoneLocalMatrix()`):

```
localBindOffset(bone) = bone.position - parent.position
localMatrix(bone)     = Translate(localBindOffset + offset.translation) * Rotate(offset.rotation)
```

A bone's **translation** channel shifts only that bone's own origin, inside
its (unmodified) parent's local frame — it never affects the parent, and
never affects any sibling. This is exactly the tool needed: for a joint whose
tree-parent is the anchor, we do not need to rotate the anchor at all — we
can place the joint bone exactly at its simulated target by writing ONLY
that joint bone's own `translation` channel.

Derivation, for a joint bone `boneIndex` whose real parent is `rootBoneIndex`:

```
anchorWorld            = ComputeBoneWorldMatrix(skeleton, pose, rootBoneIndex)   // NEVER mutated by this function anymore
desiredLocalPoint       = anchorWorld.Inverse().TransformPoint(target)           // target = simulatedJointWorldPositions[i]
localBindOffset         = skeleton.bones[boneIndex].position - skeleton.bones[rootBoneIndex].position
pose[boneIndex].translation = desiredLocalPoint - localBindOffset
```

Proof this lands `boneIndex` exactly at `target`: by construction,
`anchorWorld.TransformPoint(localBindOffset + pose[boneIndex].translation)`
`== anchorWorld.TransformPoint(desiredLocalPoint) == target` (applying
`anchorWorld` to its own inverse-transformed point returns the original
point). `pose[boneIndex].rotation` is **never read or written** by this
branch — it stays at whatever value it already had (bind pose, or a value a
LATER iteration in this same call writes — see 3.3).

No angle/direction math, no `acos`, no cross product, no degenerate-direction
guard is needed for this branch at all (unlike the existing rotate-parent
branch) — it is pure, always-well-defined point algebra. The only guard
needed is the same defensive "matrix must be invertible" pattern already used
elsewhere in this codebase (`Game/Physics/PhysicsSystem.cpp`'s own
`entityWorldMatrixInverse` computation): `anchorWorld` is always a pure
TRS with unit scale (rotation + translation only, per
`ComputeBoneLocalMatrix()`), so it is algebraically never singular — use
`Mat4::TryInverse()` and simply skip this joint (leave its pose untouched)
in the never-expected-to-happen failure case, matching this file's own
existing "degrade gracefully, never crash" philosophy.

### 3.2 — Exact new function body

Replace `src/Physics/BoneChainPhysicsResolver.cpp`'s per-joint loop (current
lines 41-119) with:

```cpp
for (std::size_t i = 0; i < jointCount; ++i) {
    const std::int32_t boneIndex = definition.jointBoneIndices[i];
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton.bones.size()) {
        continue; // Malformed chain data - skip rather than crash.
    }

    const std::int32_t parentJoint = definition.parentJointIndex[i];

    if (parentJoint < 0) {
        // This joint's tree-parent is the chain's own ANCHOR bone
        // (definition.rootBoneIndex) directly - the Bone Viewer's "(root
        // child)" case. The anchor is frequently a REAL, SHARED, load-
        // bearing skeleton bone (e.g. MMD's own 下半身), which may be the
        // tree-parent of many dozens of unrelated accessory joints AND the
        // real FK ancestor of non-participating body bones (legs). Per
        // DynamicChainDefinition.h's own documented contract, the anchor's
        // pose entry must NEVER be written by physics - so instead of
        // rotating it (which would move every other child sharing it, and
        // every real body bone descending from it), this branch corrects
        // ONLY this joint's OWN local TRANSLATION, which moves nothing
        // except this one bone's own origin within the anchor's (always
        // untouched) frame. See this file's own header comment for the
        // full derivation. task_manager/verlet-integration-8, Phase 1.
        const std::int32_t rootBoneIndex = definition.rootBoneIndex;
        if (rootBoneIndex < 0 || static_cast<std::size_t>(rootBoneIndex) >= skeleton.bones.size()) {
            continue; // No real anchor bone (e.g. a world-anchored chain) - nothing to translate relative to.
        }

        const Mat4 anchorWorld = ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::size_t>(rootBoneIndex));
        Mat4 anchorWorldInverse;
        if (!anchorWorld.TryInverse(anchorWorldInverse)) {
            continue; // Algebraically should never happen (anchorWorld is a unit-scale TRS) - defensive only.
        }

        const Vec3 desiredLocalPoint = anchorWorldInverse.TransformPoint(simulatedJointWorldPositions[i]);
        const Vec3 localBindOffset = skeleton.bones[static_cast<std::size_t>(boneIndex)].position
            - skeleton.bones[static_cast<std::size_t>(rootBoneIndex)].position;
        pose[static_cast<std::size_t>(boneIndex)].translation = desiredLocalPoint - localBindOffset;
        // pose[boneIndex].rotation is intentionally left untouched here - if
        // this SAME bone is itself some LATER joint's own tree-parent, the
        // `parentJoint >= 0` branch below (a later iteration, per this
        // array's own root-to-tip ordering invariant) will still correctly
        // rewrite its rotation to aim that descendant - translation and
        // rotation are independent BoneLocalOffset channels
        // (Animation/BonePoseMath.h's ComputeBoneLocalMatrix()), so writing
        // both across two different iterations composes correctly.
        continue;
    }

    // UNCHANGED below this point: parentJoint >= 0, i.e. this joint's
    // tree-parent is ANOTHER chain joint (never the anchor) - rotate that
    // joint's own bone to aim this joint's descendant at its target, exactly
    // as before. See this file's own header comment for why this must be
    // the PARENT's rotation, never boneIndex's own.
    const std::int32_t parentBoneIndex = definition.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
    if (parentBoneIndex < 0 || static_cast<std::size_t>(parentBoneIndex) >= skeleton.bones.size()) {
        continue;
    }

    const Mat4 parentWorld = ComputeBoneWorldMatrix(skeleton, pose, parentBoneIndex);
    const Vec3 parentWorldPos = parentWorld.TransformPoint(Vec3::Zero());

    const Mat4 currentChildWorld = ComputeBoneWorldMatrix(skeleton, pose, boneIndex);
    const Vec3 currentChildPos = currentChildWorld.TransformPoint(Vec3::Zero());
    const Vec3 rawCurrentDelta = currentChildPos - parentWorldPos;

    const Vec3 rawTargetDelta = simulatedJointWorldPositions[i] - parentWorldPos;

    if (LengthSquared(rawCurrentDelta) < kMinDirectionLengthSq || LengthSquared(rawTargetDelta) < kMinDirectionLengthSq) {
        continue;
    }

    const Vec3 currentDir = Normalize(rawCurrentDelta);
    const Vec3 targetDir = Normalize(rawTargetDelta);

    const float dot = Clamp(Dot(currentDir, targetDir), -1.0f, 1.0f);
    const float angle = std::acos(dot);
    if (angle < kMinAngleRadians) {
        continue;
    }

    Vec3 axis = Cross(currentDir, targetDir);
    if (LengthSquared(axis) < kMinDirectionLengthSq) {
        continue;
    }
    axis = Normalize(axis);

    const Quat delta = Quat::FromAxisAngle(axis, angle);
    const Quat newParentWorldRotation = delta * Quat::FromMat4(parentWorld);

    const std::int32_t grandparentBoneIndex = skeleton.bones[static_cast<std::size_t>(parentBoneIndex)].parentBoneIndex;
    const Mat4 grandparentWorld = ComputeBoneWorldMatrix(skeleton, pose, grandparentBoneIndex);
    const Quat grandparentWorldRotationInverse = Quat::FromMat4(grandparentWorld).Inverse();
    pose[static_cast<std::size_t>(parentBoneIndex)].rotation =
        Normalize(grandparentWorldRotationInverse * newParentWorldRotation);
}
```

Notes for the implementer:
- `ComputeBoneWorldMatrix()`'s signature takes `std::int32_t boneIndex`
  (see `Animation/BoneWorldMatrixQuery.h`) — the `static_cast<std::size_t>`
  calls above are only for indexing into `skeleton.bones`/`pose`, matching
  this file's own existing style; pass the plain `std::int32_t` value
  straight through to `ComputeBoneWorldMatrix()` itself, exactly like the
  pre-existing code already does.
- `kMinDirectionLengthSq`/`kMinAngleRadians` (anonymous-namespace constants,
  current lines 21-22) are used ONLY by the unchanged `parentJoint >= 0`
  branch now — leave them exactly as they are.
- The top-of-function guards (current lines 29-39: size mismatch early
  return, `pose` growth to `skeleton.bones.size()`) are entirely unchanged.

### 3.3 — Update `BoneChainPhysicsResolver.h`'s header comment

The current header comment (lines 17-83) documents ONLY the rotate-parent
algorithm as if it were the sole code path. Rewrite it to describe both
branches:

- Keep the existing "IMPORTANT DESIGN NOTE" explanation of why a bone's own
  rotation can never move its own position (still true, still the reason the
  `parentJoint >= 0` branch rotates the PARENT).
- Add a new paragraph, placed before the numbered per-joint steps, explaining
  the `parentJoint < 0` case is handled differently ON PURPOSE: rather than
  rotating `definition.rootBoneIndex` (which the old text incorrectly implied
  was always safe to do), this case corrects the joint bone's OWN
  translation, specifically so a chain's anchor — frequently a real, shared,
  load-bearing skeleton bone — is NEVER written by physics, matching
  `DynamicChainDefinition.h`'s own `rootBoneIndex` doc comment. Cross-
  reference `task_manager/verlet-integration-8` and the investigation this
  campaign fixes
  (`task_manager/verlet-integration-7/INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md`).
- Keep the existing numbered steps 1-8 as the documentation for the
  `parentJoint >= 0` branch specifically (retitle the numbered list "Per
  joint whose tree-parent is ANOTHER chain joint" so it's unambiguous which
  branch it documents), and add an equivalent short numbered list for the
  new `parentJoint < 0` branch, mirroring section 3.1's derivation above.

## Step 4: Test Rewrite Plan — `tests/Physics/BoneChainPhysicsResolverTests.cpp`

Every existing test in this file uses fixtures where the joint under test is
a DIRECT child of `rootBoneIndex` (`parentJointIndex[i] == -1`) except one
(`ThreeJointChainAppliesRootToTipInDependencyOrder`, which also has a second,
interior joint). All must be rewritten to match the new contract. Exact
worked numbers below — use them verbatim as the new expected values.

### 4.1 — `ProducedRotationLandsBoneAtRequestedSimulatedPosition` → rename `ProducedTranslationLandsRootChildBoneAtRequestedSimulatedPositionWithoutTouchingTheAnchor`

Fixture unchanged (`BuildTwoBoneSkeleton()`: root@`(0,0,0)` → joint@`(0,1,0)`;
`BuildSingleJointDefinition()`: `rootBoneIndex=0`, `jointBoneIndices={1}`,
`parentJointIndex={-1}`). Target unchanged: `simulatedPositions = {(1,0,0)}`.

New expected results (anchorWorld = Identity since pose[0] starts and stays
untouched; `localBindOffset = (0,1,0) - (0,0,0) = (0,1,0)`;
`desiredLocalPoint = (1,0,0)`; `translation = (1,0,0) - (0,1,0) = (1,-1,0)`):

```cpp
ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
    << "Anchor bone's rotation must NEVER be touched by physics.";
EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()))
    << "Anchor bone's translation must NEVER be touched by physics either.";
EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
    << "A leaf joint bone's own rotation is never written (nothing needs to swing ITS descendants).";
EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3(1.0f, -1.0f, 0.0f), 1e-4f))
    << "Joint bone's own translation must carry the full correction.";

const Mat4 jointWorld = ComputeBoneWorldMatrix(skeleton, pose, 1);
const Vec3 landedPosition = jointWorld.TransformPoint(Vec3::Zero());
EXPECT_TRUE(ApproximatelyEqual(landedPosition, simulatedPositions[0], 1e-4f))
    << "Bone did not land at the requested simulated position after ApplyDynamicChainPhysicsToPose().";
```

### 4.2 — `TargetAlreadyAlignedLeavesRotationUntouched` → rename `TargetAtBindPositionLeavesTranslationNearZeroAndAnchorFullyUntouched`

Change the simulated target to the joint's EXACT bind position
(`Vec3(0.0f, 1.0f, 0.0f)`, not the old `(0,5,0)` — the old test only
required "same direction," which was meaningful for a direction-only
rotation but is no longer the right thing to assert for a translation-based
correction; asserting "no correction needed when the target already equals
bind position" is the correct equivalent):

```cpp
const std::vector<Vec3> simulatedPositions = { Vec3(0.0f, 1.0f, 0.0f) };

ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero(), 1e-4f))
    << "A target already at the bind position should require no translation correction.";
```

### 4.3 — `ThreeJointChainAppliesRootToTipInDependencyOrder` (keep name, rewrite body/assertions)

Fixture unchanged (root@0`(0,0,0)`, jointA@1`(0,1,0)` child of 0, jointB@2
`(0,2,0)` child of 1; `parentJointIndex = {-1, 0}`). Targets unchanged:
jointA → `(1,0,0)`, jointB → `(2,0,0)`.

Worked numbers with the NEW algorithm:
- `i=0` (jointA, `parentJoint=-1`): `localBindOffset=(0,1,0)`,
  `desiredLocalPoint=(1,0,0)` (anchor/root is Identity), `pose[1].translation
  = (1,0,0) - (0,1,0) = (1,-1,0)`. JointA's world position is now exactly
  `(1,0,0)` — matches target. `pose[0]` (root) is untouched.
- `i=1` (jointB, `parentJoint=0` ≥ 0, unchanged rotate-parent branch):
  jointA's world matrix is `Translate((1,0,0)) * Rotate(Identity)` (its
  rotation has not been touched yet this call). jointB's CURRENT world
  position (before correction) is therefore `(1,0,0) + (0,1,0) = (1,1,0)`
  (jointB's own bind offset from jointA is `(0,2,0)-(0,1,0) = (0,1,0)`).
  `rawCurrentDelta = (1,1,0) - (1,0,0) = (0,1,0)`;
  `rawTargetDelta = (2,0,0) - (1,0,0) = (1,0,0)`. A genuine ~90° rotation is
  computed and written to `pose[1].rotation` (jointA's OWN rotation — jointA
  is jointB's tree-parent). After this write, jointB's world position is
  exactly `(2,0,0)` — matches target.
- Net result: jointA ends up with a NONZERO translation AND (from the second
  iteration) a NONZERO rotation of its own; the root/anchor (`pose[0]`) stays
  fully Identity throughout. Both final world positions are numerically
  identical to what the OLD (buggy) algorithm also produced for this same
  fixture — only WHERE the correction is stored differs.

```cpp
ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

const Vec3 jointAWorld = ComputeBoneWorldMatrix(skeleton, pose, 1).TransformPoint(Vec3::Zero());
const Vec3 jointBWorld = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());
EXPECT_TRUE(ApproximatelyEqual(jointAWorld, simulatedPositions[0], 1e-4f));
EXPECT_TRUE(ApproximatelyEqual(jointBWorld, simulatedPositions[1], 1e-4f));

EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
    << "Root/anchor bone must NEVER be rotated, even when it indirectly carries a whole sub-chain.";
EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
EXPECT_FALSE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()))
    << "JointA (a direct anchor-child) must have received its own translation correction.";
EXPECT_FALSE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
    << "JointA must ALSO have been rotated, to aim its own descendant (jointB) - translation and "
       "rotation on the same bone are independent and must both apply.";
```

### 4.4 — `OutOfRangeJointBoneIndexIsSkippedGracefully`, `OutOfRangeRootBoneIndexIsSkippedGracefully`, `MismatchedParentJointIndexSizeIsIgnoredGracefully`

No behavior change reaches these paths (they all bail out before either
branch runs: out-of-range `boneIndex` is checked first; a mismatched
`parentJointIndex` size trips the function's own top-of-function early
return; `rootBoneIndex = -1` is caught by the new branch's own
`rootBoneIndex < 0` guard, mirroring the old code's `parentBoneIndex < 0`
guard exactly). Keep these three tests as-is; optionally add an
`EXPECT_TRUE(ApproximatelyEqual(pose[...].translation, Vec3::Zero()))`
assertion alongside each existing rotation assertion, for completeness.

### 4.5 — `BranchingTreeRotatesTheSharedParentTowardBothChildrenIndependently` → rename `RootLevelHubTranslatesEachChildIndependentlyWithoutOverwritingSiblingsOrTouchingTheSharedAnchor`

This is the single most important test in the file — it is the isolated,
minimal reproduction of the exact reported bug (many joints sharing one real
anchor bone as their direct tree-parent). Fixture unchanged (root@0
`(0,0,0)`; jointA@1 `(0,1,0)` child of 0; jointB@2 `(0,1,0)` ALSO child of 0;
`parentJointIndex = {-1,-1}`). Targets unchanged: jointA → `(1,0,0)`,
jointB → `(0,-1,0)`.

Worked numbers: both joints resolve `localBindOffset = (0,1,0)` (identical
bind position in this fixture). jointA: `translation = (1,0,0)-(0,1,0) =
(1,-1,0)`, world pos `(1,0,0)`. jointB: `translation = (0,-1,0)-(0,1,0) =
(0,-2,0)`, world pos `(0,-1,0)`. **Both land exactly at their own distinct
targets, in the same call, because each writes to its own distinct bone
index — there is no shared mutable state left to overwrite.**

```cpp
ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

const Vec3 jointAWorld = ComputeBoneWorldMatrix(skeleton, pose, 1).TransformPoint(Vec3::Zero());
const Vec3 jointBWorld = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());
EXPECT_TRUE(ApproximatelyEqual(jointAWorld, simulatedPositions[0], 1e-4f))
    << "jointA must land at its OWN target, undisturbed by jointB being processed afterward.";
EXPECT_TRUE(ApproximatelyEqual(jointBWorld, simulatedPositions[1], 1e-4f))
    << "jointB must land at its OWN (different) target.";
EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
    << "The shared anchor's rotation must NEVER be written, regardless of how many children hub off it.";
EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
```

Update the comment block above this test (current lines 197-210) to state
this is no longer a "known, accepted limitation" — it is the exact scenario
this campaign fixes; reference `task_manager/verlet-integration-8`.

### 4.6 — NEW test: `AnchorThatIsAlsoARealBodyAncestorNeverMovesRegardlessOfHubSize`

Add a brand-new test that is the most direct possible encoding of the
reported bug: an anchor bone with a REAL, non-participating descendant (a
stand-in for "leg"), plus several (not just two) hub children sharing that
same anchor, mirroring the reported model's actual shape (~23-25 children).
A handful (e.g. 5) is enough to prove the pattern scales; you do not need to
literally build 23.

```cpp
TEST(BoneChainPhysicsResolverTests, AnchorThatIsAlsoARealBodyAncestorNeverMovesRegardlessOfHubSize)
{
    // Skeleton: root(0) -> anchor(1, e.g. "lower_body") -> leg(2, a REAL,
    // NON-PARTICIPATING body bone descending from the anchor - never listed
    // in any DynamicChainDefinition::jointBoneIndices) ; anchor(1) is ALSO
    // the direct parent of five independent accessory joints (3..7).
    SkeletonData skeleton;
    Bone root; root.position = Vec3(0,0,0); root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // 0

    Bone anchor; anchor.position = Vec3(0,1,0); anchor.parentBoneIndex = 0;
    skeleton.bones.push_back(anchor); // 1

    Bone leg; leg.position = Vec3(0,-1,0); leg.parentBoneIndex = 1; // hangs BELOW the anchor, like a real leg would.
    skeleton.bones.push_back(leg); // 2

    std::vector<std::int32_t> jointBoneIndices;
    std::vector<Vec3> simulatedPositions;
    for (int k = 0; k < 5; ++k) {
        Bone accessory;
        accessory.position = Vec3(0,1,0); // bind-identical to the anchor's own position, like a real hair/skirt root.
        accessory.parentBoneIndex = 1;
        skeleton.bones.push_back(accessory); // 3, 4, 5, 6, 7
        jointBoneIndices.push_back(3 + k);
        // Each accessory swings to its OWN distinct, arbitrary target.
        simulatedPositions.push_back(Vec3(0.1f * static_cast<float>(k), -0.1f * static_cast<float>(k), 1.0f));
    }

    DynamicChainDefinition definition;
    definition.rootBoneIndex = 1;
    definition.jointBoneIndices = jointBoneIndices;
    definition.jointSettings.assign(jointBoneIndices.size(), DynamicJointSettings{});
    definition.parentJointIndex.assign(jointBoneIndices.size(), -1); // every accessory is a direct anchor-child.
    definition.restLengths.assign(jointBoneIndices.size(), 1.0f);

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const Vec3 legWorldBefore = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
        << "The shared anchor must never be rotated, no matter how many accessories hub off it.";
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()));

    const Vec3 legWorldAfter = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(legWorldAfter, legWorldBefore, 1e-5f))
        << "A real, non-participating body bone descending from the anchor must not move AT ALL - "
           "this is the exact reported 'whole body looks ragdoll-simulated' bug.";

    for (std::size_t k = 0; k < jointBoneIndices.size(); ++k) {
        const Vec3 landed = ComputeBoneWorldMatrix(skeleton, pose, jointBoneIndices[k]).TransformPoint(Vec3::Zero());
        EXPECT_TRUE(ApproximatelyEqual(landed, simulatedPositions[k], 1e-4f))
            << "Accessory joint " << k << " did not land at its own independent target.";
    }
}
```

## Step 4.7 — Three additional, pre-existing comment blocks in this SAME test file that must also be rewritten (do not skip)

Sections 4.1-4.6 above already give the exact new BODY for every `TEST()` in
this file. Direct re-inspection of the real, current
`tests/Physics/BoneChainPhysicsResolverTests.cpp` during this document's own
v2 audit found three more pieces of prose IN THE SAME FILE — all written to
describe the OLD (buggy) "always rotate the parent" model — that sit OUTSIDE
any individual test body and are easy to miss if you only search for
`TEST(`. Leaving them unrewritten would recreate, inside this very file, the
exact kind of documentation/implementation mismatch this whole campaign
exists to fix, so treat all three as mandatory, not optional polish.

1. **The file's own top-of-file header comment (current lines 0-13).**
   States as an unconditional fact: *"landing joint `i` at its simulated
   target actually rewrites joint i's PARENT bone's rotation (rootBoneIndex
   for the first joint, or the previous joint for every one after it)"* —
   true only for the `parentJoint >= 0` branch after this phase. Rewrite it
   to describe BOTH branches, e.g.:

   ```
   // Unit tests for ApplyDynamicChainPhysicsToPose
   // (src/Physics/BoneChainPhysicsResolver.h) - the position -> rotation/
   // translation bridge that turns a chain of already-simulated Verlet
   // particle positions back into BoneLocalOffset corrections the existing
   // FK pipeline already understands (verlet-integration-1 campaign,
   // PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md; anchor-rigidity fix,
   // task_manager/verlet-integration-8, Phase 1).
   //
   // IMPORTANT: per this file's own header comment ("IMPORTANT DESIGN
   // NOTE"), a bone's own rotation can never move its own world position -
   // only its DESCENDANTS'. For a joint whose tree-parent is ANOTHER chain
   // joint, landing it at its target rewrites that PARENT joint's own
   // rotation - these tests assert against the PARENT's pose entry for that
   // case. For a joint whose tree-parent is the chain's own rootBoneIndex
   // (the ANCHOR) directly, the anchor's pose entry is NEVER written at all
   // (see DynamicChainDefinition.h's own "NOT simulated" contract) - instead
   // the joint bone's OWN local TRANSLATION is corrected, so these tests
   // assert against the JOINT's own pose entry for that case instead.
   ```

2. **The comment directly above the test renamed in 4.1 (current lines
   60-64).** States *"confirm the produced rotation (written into the ROOT
   bone - the joint's PARENT...)"* — rewrite it to describe the translation-
   based correction instead, consistent with the new test name
   (`ProducedTranslationLandsRootChildBoneAtRequestedSimulatedPositionWithoutTouchingTheAnchor`),
   e.g.: *"Genuine round-trip correctness test: move the joint's simulated
   target SIDEWAYS by the SAME distance as its own bind length, confirm the
   produced TRANSLATION (written into the JOINT bone itself, NEVER the
   anchor - see this file's own header comment) actually lands the joint
   bone there when fed back through ComputeBoneWorldMatrix()."*

3. **The inline setup comment inside the test renamed/rewritten in 4.3
   (current lines 135-141).** States *"Achieving this requires rewriting the
   ROOT bone's rotation (to swing jointA into place) - jointB then falls out
   'for free' from the same rotation, since jointA's own rotation was never
   touched..."* — this is exactly backwards after Phase 1: jointA's own
   TRANSLATION lands it at its target first (root/anchor untouched), and
   jointB then requires jointA's own ROTATION to be written (the
   `parentJoint >= 0` branch) to swing IT into place. Rewrite this comment to
   match the corrected mechanism this document's own Step 4.3 worked
   derivation above describes, e.g.: *"Bend the whole chain 90 degrees
   sideways - jointA lands at (1,0,0) via its OWN translation correction
   (root/anchor untouched), then jointB (one further rest-length past it)
   requires jointA's OWN rotation to be written next, to swing jointB into
   its own target at (2,0,0) - two independent BoneLocalOffset channels on
   the SAME bone (jointA), written by two different loop iterations."*

## Step 5: What We Will NOT Do (Focus, this phase)

- We will not touch the `parentJoint >= 0` branch's math at all (no
  behavior change for interior chain bones aiming their own descendants).
- We will not add any new public function/parameter to
  `ApplyDynamicChainPhysicsToPose()` — its signature is unchanged; this is a
  pure internal-behavior fix.
- We will not attempt to also fix the interior (non-anchor) hub "last write
  wins" limitation in this phase — out of scope, see `PHASE0_MASTER_STRATEGY.md`.
