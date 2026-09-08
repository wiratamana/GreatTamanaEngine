# PHASE0 — MASTER STRATEGY: Anchor-Bone Rigidity Fix For Hub-Shared Dynamic Chain Roots ("Whole Body Looks Ragdoll-Simulated" On Transform Drag)

Orchestrator document for the `verlet-integration-8` campaign. Every child
phase document in this folder implements one slice of this plan, in strict
order — each phase's code depends on the previous phase's deliverables
already compiling. Every phase edits real `.h`/`.cpp` files (production code
and/or test code); none of them are "just planning" or "verification only."

This campaign is the direct, code-level fix for the defect fully diagnosed
(but deliberately NOT fixed) in
`task_manager/verlet-integration-7/INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md`.
Read that investigation first if you have not already — this document only
restates the parts needed to justify the fix; it does not repeat the full
forensic trail.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause recap, the chosen fix strategy (and the two rejected alternatives), ordering, and scope boundaries. |
| `PHASE1_ANCHOR_BONE_IMMUTABILITY_FIX_IN_BONE_CHAIN_PHYSICS_RESOLVER.md` | The actual bug fix: `src/Physics/BoneChainPhysicsResolver.cpp`/`.h` changed so a chain's `rootBoneIndex` (the anchor — a real, shared, load-bearing skeleton bone such as `下半身`) is **never** written by physics, for any joint, ever. Direct anchor-children instead get their own bone's **local translation** corrected to land at their simulated position. Includes a full rewrite of `tests/Physics/BoneChainPhysicsResolverTests.cpp` plus new regression tests that directly encode the reported bug (a hub anchor that is also a real ancestor of a non-participating body bone). |
| `PHASE2_CONTRACT_AND_DOCUMENTATION_ALIGNMENT.md` | Fixes the documentation/implementation mismatch the investigation flagged as a "Secondary Contributing Factor": updates the doc comments in `src/Physics/DynamicChainDefinition.h` (`rootBoneIndex`) and `src/Physics/DynamicChainDetection.h` (the "Known Limitation" note) so they accurately describe the NEW, fixed behavior instead of the old, buggy one. |
| `PHASE3_END_TO_END_REGRESSION_COVERAGE_AND_TEST_SUITE_RECONCILIATION.md` | Adds one new, full-pipeline (`PhysicsSystem::Update()`-level, not just the isolated resolver function) regression test that reproduces the user's own exact reported scenario (drag `Transform.position`, watch the "leg" bone) and proves it no longer moves. Reconciles every existing test file the original investigation named (`PhysicsSystemFreezeAndCulpritFTests.cpp`, `PhysicsSystemWorldSpaceRootMotionTests.cpp`, `DynamicChainSolverIdleSettlingTests.cpp`, `PhysicsSystemParallelTests.cpp`, `DynamicChainRigCacheTests.cpp`, `DynamicChainDetectionTests.cpp`, `DynamicChainDefinitionTests.cpp`) against the Phase 1 change, updating stale comments/thresholds wherever the actual behavior changed. |

## Step 1: The Goal (Where are we going?)

1. Dragging an entity's `Transform` (or simply letting it idle/settle) must
   **never** visibly bend/rotate the character's own rigid FK body (hips,
   thighs, shins, torso) — only the bones that are genuinely members of a
   detected `DynamicChainDefinition::jointBoneIndices` (hair, skirt frills,
   decorations) may visibly react to physics.
2. `src/Physics/DynamicChainDefinition.h`'s own documented contract for
   `rootBoneIndex` — *"is NOT simulated (it is the pinned anchor, always
   taken directly from the animated FK pose every step)"* — must become
   **actually true in code**, not just true in a comment, for every model,
   including ones whose PMX rigid-body/joint data happens to route two dozen
   or more accessory strands through one single, real, shared, load-bearing
   skeleton bone (the exact shape of the model in the bug report).
3. The fix must be surgical: it must not require re-authoring any model's
   PMX data, must not change chain-detection results (`DetectDynamicChains()`
   keeps finding exactly the same chains, on exactly the same anchor bones,
   as before), and must not regress any of `verlet-integration-6`'s or
   `verlet-integration-7`'s own already-landed, already-tested behavior for
   the ordinary (non-hub, non-shared-anchor) case.
4. Every currently-existing automated test that exercises this code path must
   keep compiling and keep asserting something true — where the new, correct
   behavior differs numerically from the old, buggy behavior, the test itself
   must be rewritten (not deleted, not skipped) to assert the new, correct
   contract.

## Step 2: The Situation / The Problem (Where are we now?)

Full mechanism (see the investigation doc for the complete forensic
derivation) — condensed to exactly what matters for choosing a fix:

- `src/Physics/BoneChainPhysicsResolver.cpp`'s `ApplyDynamicChainPhysicsToPose()`
  (lines 41-119) loops over every joint `i` in a chain and, to land joint `i`
  at its simulated world position, rewrites the **rotation** of joint `i`'s
  tree-**parent** bone (a bone's own rotation can never move its own world
  position — only its descendants', per this file's own "IMPORTANT DESIGN
  NOTE," `BoneChainPhysicsResolver.h` lines 17-37).
- For every joint whose tree-parent is the chain's own `rootBoneIndex`
  directly (`definition.parentJointIndex[i] == -1` — the Bone Viewer's own
  `(root child)` label), that "parent" is the **anchor bone itself**
  (line 56-58 of the `.cpp`: `parentBoneIndex = definition.rootBoneIndex`).
  For this model, the anchor is `下半身` ("lower body") — a real, shared
  skeleton bone that is also the ordinary FK ancestor of the character's own
  legs.
- Every one of the ~23-25 accessory strands that are direct children of that
  one anchor independently overwrites `pose[下半身].rotation` from scratch,
  every single frame (`DynamicChainDetection.h`'s own documented "Known
  Limitation": last-processed child wins). Whichever rotation "wins" is
  inherited by the real legs during skinning, because `下半身` really is
  their FK ancestor — this is the entire, exact mechanism behind "the whole
  body looks ragdoll-simulated."
- `verlet-integration-7`'s Phases 1/3/5 (all working exactly as designed)
  made this pre-existing `verlet-integration-6` defect run continuously
  (Phase 1), react to Transform drags (Phase 3), and swing by a much larger
  angle than before (Phase 5's weaker `stiffness`) — turning a rare, masked
  quirk into the reported, constant, obvious defect.

### Two candidate fixes considered, and why one is chosen

1. **Rejected — change `DynamicChainDetection.cpp`'s anchor-selection rule**
   (e.g. never choose a bone as an anchor if it is also a real ancestor of a
   non-participating body bone). Rejected because: it changes chain-detection
   RESULTS (some models would suddenly detect zero chains, or route them
   through a different, possibly nonexistent anchor), touches an algorithm
   `verlet-integration-6`/`7` both deliberately left alone, and does not
   actually fix the underlying mathematical problem (a single shared bone's
   rotation genuinely cannot simultaneously satisfy two dozen different
   children's independent targets) — it would just relocate which bone the
   symptom appears on.
2. **Rejected — average/blend multiple children's corrective rotations at a
   shared parent.** Rejected because: it still writes a corrective rotation
   onto `rootBoneIndex` itself, which **still** violates
   `DynamicChainDefinition.h`'s own "anchor is NOT simulated" contract and
   **still** visibly rotates the real legs (just by a smaller, blended,
   equally-unrelated-to-the-body angle, instead of one arbitrary child's full
   angle) — it reduces the symptom's amplitude without eliminating its cause.
3. **Chosen — make the anchor bone's own pose entry (`pose[rootBoneIndex]`)
   permanently, unconditionally untouched by physics, and instead correct
   each direct anchor-child's OWN local TRANSLATION to land it at its
   simulated target.** This works because `Animation/BonePoseMath.h`'s own
   shared formula —
   `localMatrix(bone) = Translate(localBindOffset + offset.translation) * Rotate(offset.rotation)`
   — means a bone's **translation** channel moves ONLY that bone's own
   origin, relative to its (untouched) parent, with zero effect on any
   sibling sharing the same parent, and zero effect on the parent itself.
   Each of the ~23-25 hub children gets its own independent translation
   write to its OWN distinct bone index — there is no shared mutable state
   left to fight over, so the "last write wins" defect disappears as a
   byproduct, and the anchor's rotation (and hence the real legs' FK pose)
   is never perturbed, for real, matching the documented contract exactly.
   See Phase 1 for the full derivation and exact code.

This is a **strictly local, single-function fix**: it touches exactly the
one branch of `ApplyDynamicChainPhysicsToPose()` that currently misbehaves
(`parentJointIndex[i] == -1`) and leaves the other branch (`parentJointIndex[i] >= 0`
— rotating an ordinary interior chain bone to aim one of its own descendants,
which is never a real, shared, load-bearing body bone) completely untouched.
The interior-hub "last write wins" limitation (two accessory bones deep
inside the SAME chain sharing one non-anchor parent) remains exactly as
documented and accepted by `verlet-integration-6` — it does not cause "whole
body distortion" (it is confined to the chain's own accessory bones) and is
explicitly out of scope here; see Phase 1's "What We Will NOT Do."

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 3, strictly in order:

1. **Phase 1** rewrites `ApplyDynamicChainPhysicsToPose()`'s per-joint loop so
   the `parentJointIndex[i] == -1` case computes and writes a corrective
   **local translation onto the joint bone itself**, instead of a corrective
   rotation onto `definition.rootBoneIndex`. The `parentJointIndex[i] >= 0`
   case is left byte-for-byte unchanged. `tests/Physics/BoneChainPhysicsResolverTests.cpp`
   is rewritten test-by-test to assert the new, correct contract, plus new
   tests that directly encode the reported bug (anchor bone with a real,
   non-participating descendant that must never move). This is the ONLY
   phase that changes production simulation behavior — it must land first
   and must compile/pass entirely on its own before Phase 2/3 begin.
2. **Phase 2** updates the two doc comments the investigation flagged as
   factually wrong relative to the (buggy) implementation
   (`DynamicChainDefinition.h`'s `rootBoneIndex`,
   `DynamicChainDetection.h`'s "Known Limitation") so they describe Phase 1's
   new, TRUE behavior. Purely comment edits; zero behavior change; depends on
   Phase 1 only so the new comments can accurately cite the new code.
3. **Phase 3** adds a full-pipeline regression test that reproduces the
   user's own exact bug report end-to-end (`PhysicsSystem::Update()`, a
   `Transform` drag, a synthetic "hub anchor with real leg descendant"
   fixture) and audits every other test file the original investigation named
   for staleness against Phase 1's new behavior, fixing any comment or
   threshold that no longer matches reality.

## Step 4: What We Will NOT Do (Focus)

- We will **not** touch `src/Physics/DynamicChainDetection.cpp`'s actual
  anchor-selection/chain-assembly algorithm — chain detection results (which
  bones are members, which chain they land in, which bone is the anchor) are
  completely unchanged by this campaign. Only `DynamicChainDetection.h`'s doc
  comment changes (Phase 2), not its logic.
- We will **not** change `src/Physics/DynamicChainSolver.cpp`,
  `ChainConstraints.cpp`, `VerletIntegration.cpp`, or `WindField.cpp` — the
  Verlet simulation itself (what world position each joint particle settles
  at) is entirely unaffected; this campaign only changes HOW an
  already-simulated position gets written back into the bone pose.
- We will **not** fix the interior (non-anchor) "last write wins" hub
  limitation `DynamicChainDetection.h` already documents as accepted — that
  scenario never touches a real, shared body bone and is not the reported
  symptom. It remains exactly as accepted by `verlet-integration-6`.
- We will **not** change `src/Game/Physics/PhysicsSystem.cpp`'s own call
  sites into `ApplyDynamicChainPhysicsToPose()`/`StepDynamicChain()` — this
  fix is entirely internal to `BoneChainPhysicsResolver.cpp`'s own function
  body; every existing caller keeps calling it with the exact same signature
  and the exact same inputs.
- We will **not** introduce a synthetic/virtual "hub" bone into the skeleton,
  and will **not** touch `SkeletonData`, PMX loading, or skinning-matrix
  computation — the chosen fix needs none of that; it only ever writes into
  the ALREADY-EXISTING `BoneLocalOffset::translation` channel of an
  already-existing chain-member bone.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact function bodies (with real, worked
numeric examples) to write, and the exact test files to rewrite or extend.
Do not reorder the phases — Phase 2's comments only make sense once Phase 1's
code exists to describe; Phase 3's regression test only proves anything once
Phase 1's fix is actually in place. Whenever this document's own line-number
citations and a later phase's own re-check of the real source tree disagree,
trust the child phase's own re-verification — this document is the roadmap,
each child phase is the up-to-date work order.

## Revision Notes (v2 — Second Iteration Audit)

Performed as a dedicated re-audit pass, mirroring `verlet-integration-7`'s own
`PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)" precedent: every
line-number, function-signature, and struct-field citation in this
campaign's three child phase documents was independently re-verified against
the real, current source tree — `src/Physics/BoneChainPhysicsResolver.cpp/.h`,
`src/Physics/DynamicChainDefinition.h`, `src/Physics/DynamicChainDetection.h/.cpp`,
`src/Physics/DynamicChainSolver.cpp`, `src/Animation/BonePoseMath.h`,
`src/Animation/BoneWorldMatrixQuery.h`, `src/Animation/BoneLocalOffset.h`,
`src/Math/Mat4.h`, `src/Game/Physics/PhysicsSystem.cpp/.h`,
`tests/Physics/BoneChainPhysicsResolverTests.cpp`,
`tests/Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp`,
`tests/Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp`,
`tests/Game/Physics/PhysicsSystemParallelTests.cpp`, `tests/CMakeLists.txt`,
`src/Editor/BoneViewerWindow.cpp`, and `src/Editor/Panels/InspectorPanel.cpp`.

- **Phase 1 and Phase 2: confirmed accurate, no changes made to their core
  plan.** Every quoted code excerpt, exact line range (e.g. the per-joint
  loop at `BoneChainPhysicsResolver.cpp` lines 41-119), struct field,
  function signature (`Mat4::TryInverse(Mat4&) const noexcept`,
  `ComputeBoneWorldMatrix(const SkeletonData&, const std::vector<BoneLocalOffset>&, std::int32_t)`,
  `ComputeBoneLocalMatrix()`'s exact
  `Translate(localBindOffset + offset.translation) * Rotate(offset.rotation)`
  formula), and every worked numeric example (Steps 4.1-4.6's hand-computed
  translations/rotations) in these two documents matches the current source
  tree exactly. Their core plans remain sound. Phase 1 gained one small,
  additive Step 4.7 (see below) — everything else in both documents is left
  byte-for-byte as originally written, per this task's own explicit "skip v2
  if it's already good enough" allowance.
- **Finding #1 (Phase 3 — the CMake question was left open; it is now
  answered and made mandatory):** the original Phase 3 document told its own
  implementer to "confirm whether [`tests/CMakeLists.txt`] is an explicit
  list or a glob before assuming no edit is needed." Direct inspection during
  this audit confirms it: `GTE_TEST_SOURCES` (`tests/CMakeLists.txt`, line
  1265) is a fully explicit, hand-maintained list —
  `Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp` is its own literal
  entry at line 1330 — with NO glob anywhere in this project's build. Phase 3
  (v2) now gives the exact line to add and the exact new prose paragraph
  required for this same file's own giant per-file descriptive header
  comment (every one of this list's ~80 entries has a matching paragraph up
  there; leaving the new file undocumented would itself be a fresh instance
  of the very "implementation says one thing, the written record says
  another" problem this whole campaign exists to fix).
- **Finding #2 (Phase 3 — the new test file was a bare code fragment, not a
  compilable unit):** the original Phase 3's own Step 3.1 showed only a
  helper function and two `TEST()` bodies, with no `#include` list and no
  `namespace gte { namespace { ... } }` wrapper — unlike Phase 1's own,
  already-precise `BoneChainPhysicsResolverTests.cpp` edits. Phase 3 (v2) now
  gives the exact, minimal include list this new file actually needs
  (mirroring `PhysicsSystemWorldSpaceRootMotionTests.cpp`'s own leaner set —
  this new file never touches `AnimationSystem`/`RenderSystem`/
  `MeshInstantiationSystem`/`GtaFile`/`MotionFile`/`AssetTypes`, unlike
  `PhysicsSystemFreezeAndCulpritFTests.cpp`, which was the wrong template to
  fully mirror here) plus the required namespace wrapper, so the file
  compiles on the very first attempt exactly as given.
- **Finding #3 (confirmed by direct inspection, not just assumed: the Editor
  is genuinely unaffected):** `src/Editor/BoneViewerWindow.cpp` (lines
  705-706, 1421-1468) and `src/Editor/Panels/InspectorPanel.cpp` (lines
  395-422) both read `chain.rootBoneIndex` only to look up a bone's NAME and
  its cached, static BIND-POSE position (`m_bones[...].position` /
  `rig->skeleton.bones[...]`) for editor-time-only visualization/labels —
  neither ever reads `ResolvedAnimationPose::pose` at runtime, so Phase 1's
  translation-vs-rotation change is provably invisible to the Bone
  Viewer/Inspector. This document already implicitly assumed as much; this
  v2 pass confirms it by direct inspection instead of by assumption, and no
  phase document requires any change as a result.
