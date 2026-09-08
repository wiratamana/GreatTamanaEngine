# PHASE2 — Contract & Documentation Alignment

Depends on Phase 1 (cites its new code). Purely doc-comment edits in
production headers — zero behavior change, zero test changes required. Small,
low-risk, but explicitly required: the original investigation flagged the
current doc/implementation mismatch as a real, separate contributing factor,
and stale comments in a codebase that clearly relies on comments as the
primary design record (as this one does — see the extremely detailed header
comments throughout `src/Physics/`) are a genuine, ongoing maintenance risk if
left uncorrected.

## Step 1: The Goal (Where are we going?)

Every doc comment that describes `rootBoneIndex`'s simulated-vs-anchor
contract, or the hub/last-write-wins limitation, must accurately describe
Phase 1's new, fixed behavior — not the old, buggy behavior the investigation
diagnosed. A future implementer reading only the header comments (not the
`.cpp` bodies) must come away with the CORRECT mental model.

## Step 2: The Situation (exact current text to fix)

1. `src/Physics/DynamicChainDefinition.h`, the doc comment directly above
   `struct DynamicChainDefinition` (current lines 57-68), states:

   > `rootBoneIndex` is NOT simulated (it is the pinned anchor, always taken
   > directly from the animated FK pose every step)

   Before Phase 1, this was **factually false** for any `parentJointIndex[i]
   == -1` joint (`BoneChainPhysicsResolver.cpp` rewrote
   `pose[rootBoneIndex].rotation`). After Phase 1, it is **true** — but the
   comment itself gives no hint that this was ever in question, nor does it
   explain WHY it matters (a real, shared, load-bearing body bone). Needs
   strengthening, not just leaving alone.

2. `src/Physics/DynamicChainDetection.h`, the "Known Limitation" paragraph at
   the end of the file (current lines 108-118), describes the hub/last-write-
   wins issue as a blanket, still-accepted limitation with no distinction
   between a root-level hub (now fixed by Phase 1) and an interior hub (still
   accepted, unaffected by Phase 1).

3. `src/Physics/BoneChainPhysicsResolver.h`'s own big header comment was
   already rewritten as part of Phase 1 itself (see that phase's Step 3.3) —
   nothing further to do here in Phase 2 for that file; it is listed here
   only so this phase's own file-by-file checklist is complete and you do not
   accidentally re-edit it a second time.

## Step 3: The Plan (exact new text)

### 3.1 — `src/Physics/DynamicChainDefinition.h`

Replace the current single-sentence parenthetical
(`` `rootBoneIndex` is NOT simulated (it is the pinned anchor, always taken
directly from the animated FK pose every step) ``) with an expanded version
that states the guarantee explicitly and names the enforcing code:

```
// `rootBoneIndex` is NEVER written by physics - not its rotation, not its
// translation, for any joint, ever (see
// Physics/BoneChainPhysicsResolver.cpp's ApplyDynamicChainPhysicsToPose(),
// task_manager/verlet-integration-8, Phase 1). It is the pinned anchor,
// always taken directly from the animated FK pose every step. This matters
// because `rootBoneIndex` is frequently a REAL, SHARED, load-bearing
// skeleton bone (e.g. an MMD model's own 下半身/"lower body," which is also
// the ordinary FK ancestor of the character's legs) - NOT a dedicated,
// physics-only "hair root" bone. Before verlet-integration-8, this
// guarantee was violated for any joint whose tree-parent was the anchor
// directly (see that campaign's own investigation,
// task_manager/verlet-integration-7/INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md,
// for the full reported symptom and root cause) - this comment now
// describes the CORRECTED, enforced behavior.
```

Keep the rest of that comment block (the explanation of `jointBoneIndices`
being the simulated set, and the Phase-1-of-`verlet-integration-6` tree
rationale) unchanged.

### 3.2 — `src/Physics/DynamicChainDetection.h`

Replace the "Known Limitation" paragraph (current lines 108-118) with a
version that distinguishes the two cases:

```
// Known Limitation (NARROWED by task_manager/verlet-integration-8, Phase 1
// - read carefully, this note used to describe a strictly broader problem):
// when two or more joints in the SAME chain share the exact same
// parentJointIndex-resolved parent bone (a genuine "hub"), and that shared
// parent is itself ANOTHER CHAIN JOINT (parentJointIndex of the shared
// parent's own entry is >= 0, i.e. an ordinary accessory bone, never the
// chain's own rootBoneIndex) - BoneChainPhysicsResolver.cpp's
// ApplyDynamicChainPhysicsToPose() still processes joints strictly in
// ascending order and, for that shared NON-ANCHOR parent, each subsequent
// child's own corrective rotation still OVERWRITES the previous child's -
// only the LAST child processed at that interior hub each frame "wins" that
// one accessory bone's own final FK rotation. This remaining case never
// touches a real, shared, load-bearing body bone (it is confined entirely
// to the chain's own accessory bones), so it cannot cause the "whole body
// looks ragdoll-simulated" symptom - it is a narrower, purely cosmetic,
// still-accepted, still-not-fixed limitation (e.g. a spider-web skirt's own
// internal strand-root sharing).
//
// The BROADER case this note used to describe - two or more joints sharing
// the chain's own ANCHOR bone (rootBoneIndex) as their direct tree-parent,
// exactly the shape of the model in
// task_manager/verlet-integration-7/INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md
// (dozens of accessory strands hanging directly off a real, shared body
// bone like 下半身) - is FIXED as of task_manager/verlet-integration-8,
// Phase 1: each such joint now corrects its OWN local translation instead of
// rotating the shared anchor, so every one of them lands at its own
// independent target in the same call, and the anchor (and every real body
// bone descending from it) is never perturbed at all. See
// Physics/BoneChainPhysicsResolver.h's own header comment for the full
// derivation.
```

## Step 4: What We Will NOT Do (Focus, this phase)

- We will not touch `DynamicChainDetection.cpp`'s Step A-H algorithm
  comment block (current lines 15-59 of the `.cpp`) — chain assembly itself
  is unaffected by Phase 1; only the ONE "Known Limitation" paragraph in the
  header changes.
- We will not touch any test file in this phase — no assertion anywhere
  depends on comment text.
