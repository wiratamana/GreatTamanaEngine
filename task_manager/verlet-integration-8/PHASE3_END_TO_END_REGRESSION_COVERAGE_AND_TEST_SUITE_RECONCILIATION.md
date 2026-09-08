# PHASE3 — End-To-End Regression Coverage & Test Suite Reconciliation

Depends on Phase 1 (the code exists to test against) and, for comment
accuracy, Phase 2. This phase has three parts: (A) add ONE new, full-pipeline
regression test FILE (a complete, compilable translation unit — not a bare
code fragment) that reproduces the user's own exact bug report, end-to-end,
through the REAL `PhysicsSystem::Update()` (not just the isolated resolver
function Phase 1 already covers); (B) REGISTER that new file with the build
(`tests/CMakeLists.txt` is a fully explicit, hand-maintained list — see Step
3.1a — this step is mandatory, not optional, or the new tests silently never
run); (C) audit every existing test file the original investigation named by
number, fixing any comment or numeric threshold that is now stale, and
confirming (by actually building and running each suite) that no other file
requires a code change.

## Step 1: The Goal (Where are we going?)

1. A permanent, automated, full-pipeline test must exist that fails if this
   exact bug (or an equivalent regression) is ever reintroduced: build a
   synthetic model whose chain anchor is also a real, non-participating body
   bone (a stand-in for `下半身`/legs), with several accessory joints sharing
   it as their direct tree-parent (a stand-in for the reported ~23-25
   `(root child)` entries); run it through `PhysicsSystem::Update()` for many
   frames AND across a `Transform.position` drag (the user's own literal
   reproduction steps); assert the "leg" bone never moves.
2. That new test file must actually be compiled and run by CI/`ctest` — this
   project's test binary is built from an explicit, hand-maintained file
   list (`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`), never a glob, so a new
   `.cpp` file that is never added to that list silently never runs, no
   matter how correct its own contents are.
3. Every other test file the original investigation explicitly named as
   "must be preserved" must still compile and still pass, OR have its stale
   comment/threshold corrected as a deliberate, explained code edit — never
   silently deleted or weakened to force a pass.

## Step 2: The Situation (what already exists, and what each file actually asserts)

Confirmed by direct inspection during this campaign's own investigation
(grep for `rootBoneIndex`/`ApplyDynamicChainPhysicsToPose` across `tests/`):

- `tests/Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp` uses a
  4-bone synthetic chain (`root(0) -> chainRoot(1, STATIC anchor) ->
  joint1(2, Dynamic) -> joint2(3, Dynamic)`) where `joint1` is the chain's
  ONLY direct anchor-child. Every assertion in this file is expressed in
  terms of reconstructed WORLD POSITIONS (`ReconstructTipWorldPosition()`/
  `ReconstructRootRelativeTipOffset()`), never a direct read of
  `pose[1].rotation`/`.translation`. Because Phase 1's fix reproduces the
  exact same final world positions for a single-anchor-child chain (see
  Phase 1, Step 4.3's worked proof), these tests are expected to keep
  passing UNCHANGED numerically. Two comments, however, describe the OLD
  mental model and must be corrected (they are not assertions, but they are
  actively misleading after Phase 1):
  - Lines 148-150: *"the root bone itself always moves perfectly rigidly
    with the entity's own Transform (its own pose entry only ever has its
    ROTATION corrected by physics, never its translation..."* — after Phase
    1, the anchor's pose entry is never touched AT ALL (neither channel);
    rewrite to say so, and note the immediate anchor-child now carries the
    translation correction instead.
  - Lines 283-292: the comment explaining why a huge drag delta "would swing
    the corrected direction almost entirely toward the drag itself" describes
    the OLD direction-only, fixed-bind-length rotation behavior. After Phase
    1, the immediate anchor-child's translation reproduces the simulated
    position exactly (not a fixed-length approximation) — rewrite this
    comment to describe the new mechanism; the test's own numeric threshold
    (`0.1f` drag, `0.5f * dragDelta` tolerance) is expected to still hold
    (see Step 3 below for what to do if a real test run disagrees).
  - Line 304-309's own inline comment (immediately before the
    `rootWorldPosAfterDrag - rootWorldPosBeforeDrag` assertion) repeats the
    same "its own pose entry only ever has its rotation corrected... never
    its translation" claim a third time in this same file — fix it alongside
    the two occurrences above rather than leaving one copy stale.
- `tests/Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp` — audit
  for the same pattern (position-based assertions vs. direct rotation/
  translation field reads); update any comment describing the old
  rotate-the-anchor mental model. Confirmed by direct inspection: every
  assertion in this file already goes through `ReconstructTipWorldPosition()`
  (world-position-based) or compares two full `pose` vectors bone-by-bone for
  equality (`IdentityOrMissingTransformProducesByteIdenticalResultsToPreWorldSpaceBehavior`,
  `EntityTransformScaleNeverAffectsTheSimulatedPoseOrTheTeleportGuardThreshold`)
  — neither pattern hard-codes an assumption about WHICH channel (rotation
  vs. translation) carries the correction, so no assertion needs to change;
  this file has no comment describing the old rotate-the-anchor mechanism
  either (unlike `PhysicsSystemFreezeAndCulpritFTests.cpp` above) — zero
  edits needed here, confirm by building.
- `tests/Physics/DynamicChainSolverIdleSettlingTests.cpp` /
  `tests/Physics/DynamicChainSolverTests.cpp` — both set `rootBoneIndex = -1`
  in their own fixtures ("unused by StepDynamicChain itself"). These test
  `StepDynamicChain()` (the Verlet integration step), never
  `ApplyDynamicChainPhysicsToPose()` — Phase 1 does not touch
  `StepDynamicChain()` at all. Expected: zero changes needed; confirm by
  building.
- `tests/Game/Physics/PhysicsSystemParallelTests.cpp` — asserts the
  disjoint-`jointBoneIndices` parallel-dispatch safety invariant. Phase 1
  does not change which bones a chain writes to relative to another chain's
  bones (it still never writes across chains; it now ALSO never writes to
  `rootBoneIndex` at all, which is strictly fewer writes than before, never
  more) — expected: zero changes needed; confirm by building. (Its own
  `BuildSymmetricModel()` fixture gives every chain exactly ONE joint, always
  a direct anchor-child, so this file's own results are numerically unchanged
  by Phase 1 too, per the same single-anchor-child proof as
  `PhysicsSystemFreezeAndCulpritFTests.cpp` above.)
- `tests/Game/Physics/DynamicChainRigCacheTests.cpp`,
  `tests/Physics/DynamicChainDefinitionTests.cpp`,
  `tests/Physics/DynamicChainDetectionTests.cpp` — all read `rootBoneIndex`
  purely as a plain data field (construction/round-trip/detection-result
  checks), never asserting resolver-internal rotation/translation semantics.
  Expected: zero changes needed; confirm by building.
- `src/Editor/BoneViewerWindow.cpp` (lines 705-706, 1421-1468) and
  `src/Editor/Panels/InspectorPanel.cpp` (lines 395-422) — NOT test files,
  but confirmed by direct inspection to read `chain.rootBoneIndex` only to
  look up a bone's name and its cached, static BIND-POSE position
  (`m_bones[...].position` / `rig->skeleton.bones[...]`) for editor-time-only
  visualization/labels — neither ever reads `ResolvedAnimationPose::pose` at
  runtime, so Phase 1's translation-vs-rotation change is invisible to the
  Editor. No edit needed; listed here only so this phase's own audit trail is
  complete.

## Step 3: The Plan

### 3.1 — New file: `tests/Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp`

Add a new, COMPLETE, compilable test file (real `PhysicsSystem`/`Registry`,
following the exact same fixture-building conventions already used by
`PhysicsSystemFreezeAndCulpritFTests.cpp`'s `BuildSyntheticChainFixture()` —
copy that pattern, do not invent a new one). Its own `#include` list should
mirror `tests/Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp`'s
LEANER set, not `PhysicsSystemFreezeAndCulpritFTests.cpp`'s fuller one — this
new file never plays a real animation clip or exercises
`AnimationSystem`/`RenderSystem`/`MeshInstantiationSystem`/`GtaFile`/
`MotionFile`/`AssetTypes`/`MeshAssetSource` the way that file's last two
tests do, so none of those headers are needed here. The full file, exactly as
it should be written to disk:

```cpp
// Unit tests for task_manager/verlet-integration-8 - the anchor-bone
// rigidity fix (src/Physics/BoneChainPhysicsResolver.cpp, Phase 1). This is
// the full end-to-end, PhysicsSystem::Update()-level regression test for the
// exact bug reported in task_manager/verlet-integration-7/
// INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md: a chain anchor
// that is ALSO a real, non-participating FK body bone (a stand-in for MMD's
// own 下半身/legs), with several accessory joints hubbed directly off that
// same anchor (a stand-in for the reported model's ~23-25 "(root child)"
// hair/skirt strands) - the "leg" bone's own local pose must never move, no
// matter how the hub accessories simulate or how the entity's own Transform
// is dragged. All Tier 1 - no GPU/Renderer/ImGui dependency beyond a plain
// Registry, mirroring PhysicsSystemFreezeAndCulpritFTests.cpp's/
// PhysicsSystemWorldSpaceRootMotionTests.cpp's own conventions exactly.

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Math/Mat4.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte {
namespace {

// Synthetic rig: root(0, no rigid body) -> anchor(1, STATIC, e.g.
// "lower_body") -> leg(2, NO rigid body at all - an ordinary, non-
// participating FK body bone, exactly like a real character's thigh bone
// descending from 下半身) ; anchor(1) is ALSO the direct parent of FIVE
// independent Dynamic accessory bones (3..7, e.g. hair/skirt strand roots),
// each with its own Joint back to the anchor's rigid body - reproducing the
// reported model's own "(root child)" hub shape at a manageable scale.
SkinnedMeshData BuildHubAnchorWithLegFixture()
{
    SkinnedMeshData data;
    Bone root;
    root.name = "root";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root); // 0

    Bone anchor;
    anchor.name = "lower_body";
    anchor.position = Vec3(0.0f, 1.0f, 0.0f);
    anchor.parentBoneIndex = 0;
    data.skeleton.bones.push_back(anchor); // 1

    Bone leg;
    leg.name = "leg";
    leg.position = Vec3(0.0f, -0.5f, 0.0f); // hangs below the anchor.
    leg.parentBoneIndex = 1;
    data.skeleton.bones.push_back(leg); // 2

    PhysicsData physics;
    RigidBody anchorBody;
    anchorBody.boneIndex = 1;
    anchorBody.motionType = RigidBodyMotionType::Static;
    physics.rigidBodies.push_back(anchorBody); // 0

    for (int k = 0; k < 5; ++k) {
        Bone accessory;
        accessory.name = "accessory_" + std::to_string(k);
        accessory.position = Vec3(0.0f, 1.0f, 0.0f); // bind-identical to the anchor, like a real hair root.
        accessory.parentBoneIndex = 1;
        data.skeleton.bones.push_back(accessory); // 3..7

        RigidBody accessoryBody;
        accessoryBody.boneIndex = 3 + k;
        accessoryBody.motionType = RigidBodyMotionType::Dynamic;
        physics.rigidBodies.push_back(accessoryBody);

        Joint anchorToAccessory;
        anchorToAccessory.rigidBodyAIndex = 0;
        anchorToAccessory.rigidBodyBIndex = static_cast<std::int32_t>(physics.rigidBodies.size() - 1);
        physics.joints.push_back(anchorToAccessory);
    }

    data.physics = std::move(physics);
    return data;
}

} // namespace

TEST(PhysicsSystemAnchorRigidityRegressionTests, LegBoneNeverMovesWhileHubAccessoriesSimulateContinuously)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildHubAnchorWithLegFixture();
    physicsSystem.RegisterDynamicChains("HubAnchorLegModel.gta", data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, "HubAnchorLegModel.gta");
    ASSERT_TRUE(registry.HasComponent<DynamicChainRig>(entity));

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size());

    const Vec3 legWorldBindPose = ComputeBoneWorldMatrix(data.skeleton, pose.pose, 2).TransformPoint(Vec3::Zero());

    for (int i = 0; i < 300; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
        const ResolvedAnimationPose* current = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(current, nullptr);
        const Vec3 legWorldNow = ComputeBoneWorldMatrix(data.skeleton, current->pose, 2).TransformPoint(Vec3::Zero());
        EXPECT_TRUE(ApproximatelyEqual(legWorldNow, legWorldBindPose, 1e-4f))
            << "Frame " << i << ": the leg bone moved even though it is not a member of any "
               "DynamicChainDefinition - the reported 'whole body looks ragdoll-simulated' bug has regressed.";
    }
}

TEST(PhysicsSystemAnchorRigidityRegressionTests, LegBoneNeverMovesAcrossATransformDrag)
{
    // Reproduces the user's own literal reported reproduction steps:
    // dragging the entity's Transform.position must never visibly move the
    // rigid body (here: the "leg" bone) - only the hub accessories may lag.
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildHubAnchorWithLegFixture();
    physicsSystem.RegisterDynamicChains("HubAnchorLegDragModel.gta", data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, "HubAnchorLegDragModel.gta");
    Transform& transform = registry.AddComponent<Transform>(entity);

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size());

    for (int i = 0; i < 30; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    // Drag Position.x, exactly like the reported Inspector screenshot.
    transform.position.x = -1.270f;
    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* after = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(after, nullptr);

    // The leg's LOCAL (bone-space) pose must be untouched by the drag - only
    // its WORLD position should move, and only by rigidly following the
    // entity's own Transform (exactly like every other rigid FK bone would).
    EXPECT_TRUE(RepresentSameRotation(after->pose[1].rotation, Quat::Identity()))
        << "Anchor bone rotation must stay untouched even across a live Transform drag.";
    EXPECT_TRUE(ApproximatelyEqual(after->pose[1].translation, Vec3::Zero()));
    EXPECT_TRUE(RepresentSameRotation(after->pose[2].rotation, Quat::Identity()))
        << "Leg bone's own local pose must be completely unaffected by the drag.";
    EXPECT_TRUE(ApproximatelyEqual(after->pose[2].translation, Vec3::Zero()));
}

} // namespace gte
```

### 3.1a — Register the new file in `tests/CMakeLists.txt` (REQUIRED — confirmed explicit list, not a glob)

Direct inspection during this document's own v2 audit confirms
`tests/CMakeLists.txt` builds `GreatTamanaEngineTests` from a fully explicit,
hand-maintained `GTE_TEST_SOURCES` list (declared at line 1265) — there is NO
glob anywhere in this project's build, so a new `.cpp` file that is never
added to this list is simply never compiled, never linked, and never run by
`ctest`/`gtest_discover_tests()`, no matter how correct its own contents are.
Two edits are required, both inside `tests/CMakeLists.txt`:

1. **Add the new source file to `GTE_TEST_SOURCES`.** The existing entry
   `Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp` sits at line 1330,
   immediately after `Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp`
   (line 1329) and immediately before the closing `)` of the `set(...)` call
   (line 1352). Insert the new file as its own line directly after line 1330:

   ```cmake
       Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp
       Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp
   ```

2. **Add a matching prose paragraph to this same file's own giant per-file
   descriptive header comment.** Every single one of `GTE_TEST_SOURCES`'
   roughly eighty entries has a matching multi-line `#`-prefixed paragraph
   describing what it covers, further up in this same file — the
   `PhysicsSystemFreezeAndCulpritFTests.cpp` entry's own paragraph runs from
   line 1179 to line 1214. Leaving the new file without a matching paragraph
   would itself be a fresh, avoidable instance of the exact "the written
   record doesn't match reality" problem this whole campaign exists to fix.
   Insert a new paragraph immediately after line 1214 (before the blank
   `#` line at 1215-1216), matching the existing indentation/alignment
   convention (file path starts at column 4, continuation lines align under
   the description text), e.g.:

   ```
   #   Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp -
   #                                            task_manager/verlet-integration-8
   #                                            campaign (src/Physics/
   #                                            BoneChainPhysicsResolver.cpp,
   #                                            Phase 1): the full end-to-end
   #                                            regression test for "whole body
   #                                            looks ragdoll-simulated" (see
   #                                            task_manager/verlet-integration-7/
   #                                            INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md) -
   #                                            a synthetic rig whose chain
   #                                            anchor is ALSO a real,
   #                                            non-participating FK body bone
   #                                            (a stand-in for MMD's own
   #                                            下半身/legs) with FIVE
   #                                            independent Dynamic accessory
   #                                            joints hubbed directly off that
   #                                            same anchor (a stand-in for the
   #                                            reported model's ~23-25
   #                                            "(root child)" hair/skirt
   #                                            strands), driven through the
   #                                            REAL PhysicsSystem::Update()
   #                                            for 300 continuous frames and
   #                                            across a live
   #                                            Transform.position drag -
   #                                            proves the "leg" bone's own
   #                                            local pose (both rotation AND
   #                                            translation) never moves at
   #                                            all, regardless of how many
   #                                            accessories hub off its parent
   #                                            anchor or how the entity's
   #                                            Transform is dragged. No
   #                                            GPU/Renderer/ImGui involved
   #                                            beyond a plain Registry.
   ```

   (Re-check the exact current line numbers immediately before editing —
   this document's own citations were correct at the time of this v2 audit,
   but Phase 1/Phase 2's own edits to production headers do not touch this
   test-build file at all, so these numbers should still be exact; if they
   have drifted, search for the literal string
   `PhysicsSystemFreezeAndCulpritFTests.cpp` in `tests/CMakeLists.txt` and
   insert relative to that instead of trusting the line number blindly.)

### 3.2 — Comment reconciliation pass

Apply the three comment rewrites identified in Step 2 to
`PhysicsSystemFreezeAndCulpritFTests.cpp` (lines ~148-150, ~283-292, and
~304-309), and perform the same "does this comment still describe the real
mechanism" read-through on `PhysicsSystemWorldSpaceRootMotionTests.cpp`
(Step 2 above already confirms, by direct inspection, that this second file
needs no edits — its assertions are all position/whole-pose-vector based and
it carries no comment describing the old rotate-the-anchor mechanism). Do
not change any assertion in either file unless Step 3.3's actual build+test
run proves one is now numerically wrong.

### 3.3 — Build and run the full suite; fix, don't suppress, any real failure

Build the test target and run every suite named in Step 2 plus the new file
from Step 3.1/3.1a. Two possible outcomes per pre-existing file:

- **Passes unchanged** (expected for every file listed in Step 2, per the
  worked proof in Phase 1 that final world positions are numerically
  unchanged for the single-anchor-child case): nothing further to do for
  that file besides the comment pass above.
- **A specific numeric assertion now fails** (possible only if a real model's
  particular fixture relies on the OLD fixed-bind-length, direction-only
  approximation in a way this document's analysis did not fully anticipate —
  e.g. a fixture with an unusually large drag/gravity-induced stretch beyond
  bind length): fix that ONE assertion's tolerance/expected value in place,
  with a comment explaining exactly why the new (more exact) translation-
  based positioning produces a different number, citing
  `task_manager/verlet-integration-8`, Phase 1. Never loosen a tolerance
  without first confirming the new value is still visually/physically
  correct (i.e. still lands the bone at its actual simulated target).

## Step 4: What We Will NOT Do (Focus, this phase)

- We will not add coverage for the interior (non-anchor) hub limitation —
  explicitly out of scope for this whole campaign (see
  `PHASE0_MASTER_STRATEGY.md`).
- We will not restructure any existing test file's fixtures beyond what is
  strictly needed to add the new assertions/comment fixes described above.
- We will not weaken, delete, or `DISABLED_`-prefix any existing test to make
  a build pass — every fix in this phase is a genuine, explained code edit.
- We will not touch `src/Editor/BoneViewerWindow.cpp` or
  `src/Editor/Panels/InspectorPanel.cpp` — Step 2 above confirms, by direct
  inspection, that neither file's own `rootBoneIndex` usage is affected by
  Phase 1's change at all (both read only cached, static bind-pose data for
  editor-time visualization, never the runtime `ResolvedAnimationPose`).
