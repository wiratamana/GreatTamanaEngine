# INVESTIGATION — "Whole Body Looks Ragdoll-Simulated" When Dragging The Model

Investigation only. **No source code was modified for this report.** Written
in response to the reported symptom: dragging the entity's `Transform`
(Inspector `Position`, e.g. `-1.270` on X, as shown in the reported
screenshot) makes the ENTIRE body (legs, hips, torso) visibly bend/contort
into an unnatural, ragdoll-like pose — not just the hair/skirt/accessories
that are supposed to be the only physically-simulated parts. Expected
behavior: the body should stay rigid (pure FK/bind pose, following the
Transform rigidly) while only the mapped Verlet chains (hair, skirt frills,
decorations — the pink dots in the Bone Viewer) react with inertial lag.

## Summary Of The Finding

This is **not a single new bug introduced line-by-line in `verlet-integration-7`**.
It is a **pre-existing, already-documented, "accepted" design limitation from
`verlet-integration-6`** (never fixed, and explicitly called "Known
Limitation" / "Known Behavior" in that campaign's own header comments) that
`verlet-integration-7` — entirely as *intended*, per its own stated goals —
turned from a rare, masked, low-impact quirk into a **guaranteed, continuous,
highly-visible defect** for exactly the scenario the user is now testing
(a resting/idle model whose position is dragged). Campaign 7's own code is
working exactly as its phase documents describe; the newly-*exposed* bug
lives in **`BoneChainPhysicsResolver.cpp`** (`ApplyDynamicChainPhysicsToPose()`)
and **`DynamicChainDetection.cpp`** (chain-anchor selection), both from
`verlet-integration-6`, neither of which `verlet-integration-7` touched.

## The Mechanism, Step By Step

### 1. A chain's "root"/anchor bone is often a REAL, SHARED skeleton bone

`DynamicChainDetection.cpp`'s `DetectDynamicChains()` picks a chain's
`rootBoneIndex` (the "anchor") from whichever bone in the model's PMX
rigid-body graph is marked `RigidBodyMotionType::Static` and is the nearest
reachable ancestor of a group of hair/skirt/decoration bones. For this
model, the Bone Viewer confirms Chain 0's anchor is **`下半身`** ("lower
body" — literally the character's own hip/pelvis bone), a bone that is
also the real skeletal ancestor of the character's **legs/thighs** in the
ordinary FK body rig — it is not a dedicated "hair root" bone, it is a
shared, load-bearing body bone. `DynamicChainDefinition.h`'s own header
comment for `rootBoneIndex` explicitly documents the *intended* contract:

> `rootBoneIndex` is NOT simulated (it is the pinned anchor, always taken
> directly from the animated FK pose every step)

### 2. But `ApplyDynamicChainPhysicsToPose()` DOES write to the anchor bone

`src/Physics/BoneChainPhysicsResolver.cpp`, inside its per-joint loop:

```cpp
const std::int32_t parentJoint = definition.parentJointIndex[i];
const std::int32_t parentBoneIndex = (parentJoint < 0)
    ? definition.rootBoneIndex                                   // <-- the anchor bone itself
    : definition.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
...
pose[static_cast<std::size_t>(parentBoneIndex)].rotation =
    Normalize(grandparentWorldRotationInverse * newParentWorldRotation);
```

For every joint `i` whose tree-parent is the root directly
(`parentJointIndex[i] == -1`, i.e. every "root child" — the Bone Viewer's
own `(root child)` label), this function's own documented design (a bone's
rotation can only ever move its *descendants*, never itself, so to aim
joint `i` at its simulated position you must rewrite **its parent's**
rotation) means **`pose[rootBoneIndex].rotation` gets overwritten** — i.e.
the supposedly "pinned, never-simulated" anchor bone (`下半身`) has its
*own local rotation* rewritten by physics, directly contradicting
`DynamicChainDefinition.h`'s documented contract quoted above. This is a
real, provable inconsistency between the two files, present since
`verlet-integration-6`.

### 3. Chain 0 alone has ~23-25 different joints sharing that ONE anchor as their parent

The reported Bone Viewer screenshot lists roughly two dozen `(root child)`
entries directly under `下半身` for Chain 0 (`HB1_0_0`…`HB1_0_8`, `HB2_0_1`,
`HB3_0_1`, `HB4_0_1`, `Q_0_0`…`Q_0_12`, etc.) — closely matching the
Inspector's own "23 duplicate rigid-body assignment(s)" diagnostic count
shown in the same screenshot, itself a `DynamicChainDetection.cpp` Step-C
diagnostic confirming this model's PMX rigid-body/anchor setup is unusually
"hub-shaped" (many simulated strands radiating from one shared anchor bone).
`DynamicChainDetection.h`'s own header comment documents exactly this
scenario as a **known, accepted, unfixed limitation**:

> Known Limitation: when two or more joints in the same chain share the
> exact same parentJointIndex-resolved parent bone (a genuine "hub" ...),
> `BoneChainPhysicsResolver.cpp`'s `ApplyDynamicChainPhysicsToPose()`
> processes joints strictly in ascending order and, for a shared parent,
> each subsequent child's own corrective rotation OVERWRITES the previous
> child's — only the LAST child processed at a given hub each frame "wins"
> the parent bone's final FK rotation ... Accepted, documented, not fixed
> by this campaign.

So every single frame, `下半身`'s final rotation is **whichever one of the
~23-25 hair/skirt/decoration strands happens to be last in bone-index
order** — an arbitrary, unrelated-to-the-body accessory's own current physics
swing direction — **not** a value that has anything to do with keeping the
body itself rigid. Because `下半身` is a real ancestor of the legs in the
FK hierarchy, whatever rotation "wins" this lottery every frame is inherited
by the entire lower body (thighs, shins, feet) when the skinning matrices are
computed — this is the direct, mechanical explanation for legs/hips
appearing to "physically simulate" even though they were never added to
any `DynamicChainDefinition::jointBoneIndices` at all.

## Why This Was Never Visible Before `verlet-integration-7` (and is now)

This exact mechanism already existed, unchanged, before this campaign. Three
things `verlet-integration-7` deliberately, correctly changed are what turned
a previously rare/masked defect into a constant, obvious one:

1. **Phase 1** (`PHASE1_...md` / `AnimationSystem::EvaluatePoses()`) made
   physics run **every frame, forever**, even for a model that is never
   animated — writing a flat, all-identity bind pose as the baseline every
   frame before physics corrects it. Previously, physics only ever ran while
   an MMD dance clip was actively playing, so `下半身`'s "winning" hub
   correction each frame was a small perturbation riding on top of large,
   constantly-changing, intentional dance motion — visually swamped/masked.
   Now, for a resting/idle model, that same hub correction **is** the entire
   visible rotation of the hip bone every frame, with nothing to hide behind.
2. **Phase 5** (`PHASE5_...md`) deliberately weakened `stiffness` from
   `0.35` to `0.02` (and raised `damping`) specifically so a resting T-pose
   chain would sag *visibly* instead of looking "dead". A side effect: every
   simulated joint now drifts much farther from its small, near-bind-pose FK
   target than before it could ever be tuned to. The corrective angle
   `ApplyDynamicChainPhysicsToPose()` must apply to make the "winning" hub
   child land on its simulated position is now correspondingly **much
   larger** than under the old, stiffer defaults — directly amplifying how
   dramatic the resulting mis-rotation of `下半身` (and the legs riding on
   it) looks.
3. **Phase 3** (`PHASE3_...md`) made physics simulate in true world space,
   composed with the entity's own `Transform`, specifically so
   dragging/rotating the model produces inertial lag. This is exactly what
   is now visible in the report's screenshot: dragging `Position.x` to
   `-1.270` momentarily desyncs every accessory joint's simulated (lagging)
   position from its instantaneous animated/bind target across all ~23-25
   hub children at once — the "winning" hub child's required corrective
   angle spikes, and `下半身`'s (and therefore the legs') visible rotation
   spikes with it, in the same single frame the drag happens. Before Phase 3
   this coupling didn't exist (dragging the Transform never perturbed the
   physics-space math at all), so this particular failure mode had no
   trigger.

None of Phase 1/3/5's own code is incorrect relative to what its own phase
document asked for — each independently did exactly what was specified and
was tested against its own explicit goals. The bug is that **their combined
effect operates on top of an existing, documented, `verlet-integration-6`
data/algorithm limitation** (`DynamicChainDetection.cpp`'s hub-anchor
selection + `BoneChainPhysicsResolver.cpp`'s "last hub child wins" behavior)
that was previously inert/invisible in practice and is now the dominant,
constantly-active visual behavior for this specific model's rig shape
(a hub with two dozen+ children sharing one shared, load-bearing body bone
as anchor).

## Secondary Contributing Factor Worth Flagging

`src/Physics/DynamicChainDefinition.h`'s own doc comment for `rootBoneIndex`
("NOT simulated ... always taken directly from the animated FK pose every
step") is **factually wrong** given `BoneChainPhysicsResolver.cpp`'s actual,
by-design behavior for any `parentJointIndex[i] == -1` joint. This
documentation/implementation mismatch appears to predate `verlet-integration-7`
as well, but is directly relevant here: it suggests whoever designed the
anchor-bone contract did **not** intend for the anchor's own rotation to ever
be physics-writable — i.e. the current `BoneChainPhysicsResolver.cpp`
behavior (rewriting `pose[rootBoneIndex]` for root-children) may itself be
an unintentional divergence from the anchor's originally-intended
"always-rigid" contract, not merely an accepted quirk of the hub-sharing
scenario. Both readings point at the same two files as the place to keep
investigating/fixing, whichever is judged the "correct" original intent.

## Files Implicated (none modified by this investigation)

| File | Role in this bug |
|---|---|
| `src/Physics/BoneChainPhysicsResolver.cpp` (`ApplyDynamicChainPhysicsToPose()`) | Writes a corrective rotation onto `rootBoneIndex` itself whenever a joint's tree-parent is the root directly; last-processed hub child wins, overwriting all earlier siblings' corrections this same frame. `verlet-integration-6`, untouched by campaign 7. |
| `src/Physics/DynamicChainDetection.cpp` (`DetectDynamicChains()`) | Chooses a real, shared, load-bearing skeleton bone (`下半身`) as a chain anchor purely from PMX rigid-body `Static` motion type, with no check for how many children will hub off it, nor whether that bone is also depended on by non-chain body bones (legs). `verlet-integration-6`, untouched by campaign 7. Its own header comment already documents the resulting hub/last-write-wins behavior as an accepted limitation. |
| `src/Physics/DynamicChainDefinition.h` | Documents `rootBoneIndex` as "NOT simulated" — contradicted by the actual behavior above; a documentation/implementation mismatch worth resolving alongside the behavior itself. |
| `src/Game/Physics/PhysicsSystem.cpp` (Phase 1/3/4/5, `verlet-integration-7`) | Did not introduce the bug, but is what now makes it run **continuously, every frame, even at rest** (Phase 1), **react to Transform drags** (Phase 3), and produce **much larger** corrective swings than before (Phase 5's weaker `stiffness`). All three changes are exactly what their own phase documents specified and are working as designed. |

## Suggested Direction For A Future Fix (not implemented here, per instructions)

Two independent, non-mutually-exclusive angles a future fix could take
(purely for the next investigator/implementer's benefit — no code changed
in this report):

1. Change `DynamicChainDetection.cpp` so that a bone which is also a real
   ancestor of any *non-participating* body bone (i.e. shared with the main
   FK skeleton, not exclusively an accessory sub-tree) is never chosen — or
   is specially flagged — as a chain anchor, since rotating it always
   affects more than the intended accessory.
2. Change `BoneChainPhysicsResolver.cpp`'s hub handling so multiple
   root-children sharing one anchor bone combine their corrections (e.g. an
   averaged/blended rotation) instead of a strict last-write-wins overwrite,
   and/or make the anchor bone's rotation genuinely immutable (matching
   `DynamicChainDefinition.h`'s documented "NOT simulated" contract) by
   introducing a dedicated, non-body "hub" bone per chain instead of reusing
   a load-bearing skeleton bone directly.

Either change would need to preserve `verlet-integration-6`'s own existing
test suite (`DynamicChainDetectionTests`, `BoneChainPhysicsResolverTests`)
and `verlet-integration-7`'s regression tests (`PhysicsSystemWorldSpaceRootMotionTests`,
`PhysicsSystemFreezeAndCulpritFTests`, `DynamicChainSolverIdleSettlingTests`),
none of which currently cover the "hub anchor is also a real shared body
bone" scenario at all — this appears to be a genuine, previously-untested
gap in both campaigns' own test coverage.
