# PHASE3 — Skeleton-Aligned Chain Detection Rewrite (the heart of the campaign) (v2)

Part of the `verlet-integration-6` campaign — read `PHASE0_MASTER_STRATEGY.md`,
`PHASE1_TREE_BASED_CHAIN_DATA_MODEL_AND_SOLVER.md`, and
`PHASE2_RIGIDBODY_JOINT_GRAPH_ANALYSIS.md` first; both must already compile
and pass their own tests before starting this phase. This phase deletes and
fully rewrites `DetectDynamicChains()` (`src/Physics/DynamicChainDetection.h/
.cpp`) and its entire test file. Nothing from the old
`Bone::deformAfterPhysics`-based algorithm survives this phase except the
`DynamicChainDetectionDefaults` struct (unchanged — still the per-model
fallback damping/stiffness/mass/gravity/wind/iterations seed) and the public
function name `DetectDynamicChains()` (kept, new body, new behavior, new
doc comment).

**v2 note:** this is the most significantly revised document in the
campaign. A second-pass review found a genuine correctness bug (Culprit F —
see `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)") in the original
Step C/D interaction: true graph-orphans (Phase 2's own
`orphanedDynamicRigidBodyIndices`) were silently dropped instead of ever
reaching `diagnostics.orphanedDynamicBoneIndices`, directly contradicting
this very document's own `DynamicBodyWithNoReachableStaticAnchorIsOrphanedNotSimulated`
test. This v2 also adds a skeleton-ancestry cycle guard, an explicit
out-of-range-parent guard, excludes unattached (`boneIndex == -1`) rigid
bodies from the eligibility map up front, and adds a new
`duplicateBoneRigidBodyAssignmentsDropped` diagnostic. Four new regression
tests are added (3.3). Everything else in this document (the overall
shape of Steps A/B/E/F/G/H, the public signature philosophy, the Known
Behavior Change / Known Limitation callouts) was re-verified against the
current source tree and found still accurate — only Step C/D and the
diagnostics struct/tests changed.

## Step 1: The Goal (Where are we going?)

`DetectDynamicChains(skeleton, physics, defaults)` becomes a genuine
implementation of the user's own directive, quoted in full in
`PHASE0_MASTER_STRATEGY.md`'s Step 1: pick up the RigidBody/Joint graph,
walk outward from every reachable Dynamic rigid body until a dead-end or a
Static anchor is found, and turn the result into a tree-shaped
`DynamicChainDefinition` (Phase 1's new shape) — while STILL guaranteeing
every tree edge is a real skeleton parent/child pair (the hard constraint
from `PHASE0_MASTER_STRATEGY.md`'s Culprit A), folding every other authored
Joint into an `ExtraStructuralConstraint`, splitting only at genuinely
separate anchors (never at an internal branch — the user's explicit
"spider web = one chain" answer), and flagging any Dynamic rigid body that
can never reach an anchor as an orphan — **including one that Phase 2's own
graph analysis already proved has no anchor reachable via ANY path at all,
not only one that fails the stricter real-skeleton-ancestry test (v2 fix for
Culprit F — see this document's own Revision Notes (v2) below)**.

## Step 2: The Situation / The Problem (Where are we now?)

`src/Physics/DynamicChainDetection.cpp`'s current ~170 lines
(`IsPhysicsBone`, `CountPhysicsChildren`, `WalkChainRun`, and
`DetectDynamicChains()`'s own body) are ENTIRELY driven by
`Bone::deformAfterPhysics` and the skeleton's own `childrenByParent` map,
built purely from `Bone::parentBoneIndex` — `PhysicsData` is read only at
the very end (lines 152-162) to override `mass`/`damping` on joints ALREADY
found by the bone flag. None of this can be reused as-is; Phase 2's new
`RigidBodyJointGraph`/`ComputeReachabilityFromStaticAnchors()` must become
the actual source of "which bones participate," and the skeleton's REAL
`parentBoneIndex` chain (not the raw joint graph) must become the actual
source of "who is whose tree-parent," per Culprit A. This phase is where
those two independent pieces of information get combined correctly for the
first time.

## Step 3: The Plan

### 3.1 The core algorithm (plain-language spec, to be transcribed into
`DetectDynamicChains()`'s new body)

Given `skeleton` (`SkeletonData`) and `physics` (`PhysicsData*`, may be
`nullptr`):

**Step A — no physics data, no chains.** If `physics == nullptr`, return an
empty result immediately (see this phase's own "Known Behavior Change"
callout below — this is an intentional consequence of the user's "fully
replace" answer, not an oversight).

**Step B — build the graph and compute raw reachability (Phase 2).**
`const RigidBodyJointGraph graph = RigidBodyJointGraph::Build(*physics);`
`const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(*physics, graph);`

**Step C — map rigid bodies to bones, validate, and seed the TRUE-orphan
diagnostic list first (v2).** Build a small
`std::unordered_map<std::int32_t, std::int32_t> boneIndexToRigidBodyIndex`
(bone index → rigid body index) in THREE passes, in this exact order:

  1. **(v2, new — fixes Culprit F) Seed the orphan list from Phase 2's own
     output, before anything else.** For every rigid body index in
     `reach.orphanedDynamicRigidBodyIndices`, look up its
     `RigidBody::boneIndex`; if `boneIndex >= 0`, append it to a new,
     function-local `std::vector<std::int32_t> orphanedBoneIndices` (this is
     the seed for the final `DynamicChainDetectionDiagnostics::orphanedDynamicBoneIndices`
     — see 3.2 below). Skip (do not append anything for) any orphaned body
     whose `boneIndex < 0` (unattached) — there is no bone to flag/render
     for a rigid body that was never attached to one at all; that body is
     still correctly "not simulated" (it was never eligible for the map
     below either), it is just not representable as a bone-indexed
     diagnostic. **This is the fix for Culprit F**
     (`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", finding #1):
     Phase 2 already proved these bodies have NO Static anchor reachable
     anywhere in their own connected component — this line is the ONLY place
     in the whole algorithm that ever records that fact anywhere, so
     skipping it (as the v1 draft did, implicitly, by simply never visiting
     these bodies in Step D) silently loses the information end-to-end. Do
     not defer this to "Step D will catch it eventually" — Step D, by
     design, only ever walks bones that made it into the eligibility map
     built in pass 2 below, and these bodies are deliberately EXCLUDED from
     that map (see pass 2's own reasoning) — the ONLY way this information
     ever reaches the final result is this explicit seed step.
  2. **Build the eligibility map** by iterating `physics->rigidBodies` ONCE,
     inserting an entry ONLY for bodies that are (a) `Static`, OR (b)
     `Dynamic`/`DynamicAndBoneMerge` AND present in
     `reach.reachableDynamicRigidBodyIndices` — AND, in both cases, **(v2)
     only if `body.boneIndex >= 0`** (an unattached rigid body, static or
     dynamic, has no bone to become an anchor or a chain member for, and
     must never be inserted into this map at all — see the rationale in
     `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", finding #4, for
     why this guard must come BEFORE the duplicate tie-break in step 3
     below, not after: without it, two entirely unrelated unattached bodies
     could spuriously "collide" on the same sentinel key). A body already
     excluded here because it's in `reach.orphanedDynamicRigidBodyIndices`
     was already handled in pass 1 above and must NOT be inserted into this
     map either (so Step D can never accidentally treat it as a valid chain
     member).
  3. **Apply the duplicate-assignment tie-break, and diagnose it (v2).** If
     two DIFFERENT eligible rigid bodies (per pass 2's own filter) claim the
     SAME `boneIndex`, keep the LOWEST rigid-body-index one deterministically
     for the map, and append every OTHER (dropped) rigid-body index for that
     same `boneIndex` to a new, function-local
     `std::vector<std::int32_t> duplicateAssignmentsDropped` list (v2 — see
     3.2's new `duplicateBoneRigidBodyAssignmentsDropped` diagnostic field:
     this case is genuinely rare, but when it involves a `Static` body losing
     the tie-break to a `Dynamic` one (or vice versa) it silently flips
     whether that bone becomes an ANCHOR or a MEMBER, which is far more
     structurally significant than two same-type colliders overlapping, and
     was completely invisible before v2). Add exactly one test for the
     basic case (3.3, `DuplicateBoneRigidBodyAssignmentPicksLowestIndexDeterministically`)
     and one for the new diagnostic (3.3,
     `DuplicateBoneRigidBodyAssignmentDropRecordedInDiagnostics`).

**Step D — for every reachable Dynamic bone, walk its REAL skeleton ancestor
chain to find (1) which anchor it ultimately belongs to, and (2) its own
tree-parent.** For each `boneIndex` that is a key of the map from Step C
(pass 2) with a Dynamic/DynamicAndBoneMerge rigid body (i.e. every
"participating" bone), walk `skeleton.bones[boneIndex].parentBoneIndex`
upward, bone-by-bone, **tracking every bone index visited during THIS one
walk in a local visited-set (v2, new — see below)**, until one of four
things happens:

  1. The walk reaches a bone that IS a `Static` rigid body's own `boneIndex`
     (per Step C's map) → this bone is the **anchor** for `boneIndex`'s
     eventual chain, and the FIRST participating bone encountered along the
     way (possibly `boneIndex` itself, if its own real parent already is
     the anchor) gets `parentJointIndex = -1` (parent is `rootBoneIndex`
     directly); every OTHER (non-participating, "pass-through") bone
     encountered along the way is simply skipped over, exactly like the OLD
     algorithm's own `IsPhysicsBone`-skipping behavior in `WalkChainRun` —
     just driven by "is this bone a key of Step C's map" instead of
     "`Bone::deformAfterPhysics`."
  2. The walk reaches ANOTHER bone that is ALSO a key of Step C's map with a
     Dynamic/DynamicAndBoneMerge rigid body, and THAT bone has ALREADY been
     assigned a chain by an earlier iteration of this same Step D (memoize
     every processed bone's own `(chainIndex, jointIndexInChain)` result the
     first time it's computed, so later iterations reuse it in O(1) instead
     of re-walking) → `boneIndex` joins that SAME chain, with
     `parentJointIndex` pointing at that already-processed bone's own
     position.
  3. The walk reaches a bone with **no valid further ancestor** — defined as
     `parentBoneIndex < 0` **OR** `parentBoneIndex >= skeleton.bones.size()`
     **(v2: both conditions are explicitly, identically terminal — an
     out-of-range parent index is already corrupted/malformed data exactly
     like a missing one, and must never be used to index `skeleton.bones[...]`
     even once)** — WITHOUT ever hitting case 1 or 2 → `boneIndex` cannot
     reach any anchor via its OWN real skeleton ancestry (even though Phase 2's
     raw joint graph said it WAS graph-connected to a Static body via some
     non-hierarchy-aligned path — e.g. a purely "ring brace" joint with no
     ancestor/descendant relationship at all). Append `boneIndex` to the SAME
     `orphanedBoneIndices` list Step C (pass 1, v2) already started seeding
     (this is the union point: Step C seeds it with TRUE graph-orphans, Step
     D adds every graph-reachable-but-hierarchy-unreachable bone on top —
     never two separate lists, never a second field) — this is the "safe to
     make safe" case from the user's own answer — flagged, never simulated,
     never crashes.
  4. **(v2, new — cycle guard, robustness only, no behavior change for any
     well-formed skeleton)** The walk's own current bone index is ALREADY in
     this walk's own visited-set (a `parentBoneIndex` cycle — malformed/
     corrupted skeleton data that should never happen in practice, but is
     not a schema-enforced impossibility) → treat exactly like case 3
     (append to `orphanedBoneIndices`, stop walking) instead of looping
     forever. In a debug build, also fire an `assert(false && "...")` so a
     genuinely corrupted rig fails loudly during development rather than
     merely producing an unexpectedly-orphaned bone silently in a release
     build.

  Process bones in ASCENDING `boneIndex` order at the top level (not
  `physics->rigidBodies`' own storage order) so the overall result — which
  bone ends up in which chain, and in what internal order — is fully
  order-independent with respect to `PhysicsData`'s own storage order
  (satisfies the user's explicit "order-independent but fully deterministic"
  requirement). Memoization (case 2) makes the walk itself cheap (each bone
  is walked at most once end-to-end, amortized); the cycle guard (case 4)
  only ever activates for genuinely malformed data and does not change this
  complexity characteristic for any real rig.

**Step E — assemble each chain's `jointBoneIndices`/`parentJointIndex`/
`restLengths` in a valid topological order.** Because Step D's memoized walk
can, in principle, discover a bone's chain membership via ANY of its
descendants first (a bone with two children might get its own membership
recorded via child A's walk before child B's walk runs), do NOT rely on
"processing order equals array position" directly. Instead, once every
participating bone's `(chainIndex, parentBoneRealIndex-or-anchor)` is known
(Step D's memo table), build each chain's own `jointBoneIndices` array with a
second, explicit pass: start from that chain's anchor, and do a
breadth-first (or depth-first — either is fine, see determinism note below)
walk OUTWARD through the memo table's own parent→children adjacency (which
is now just an in-memory tree, trivially invertible from "each bone's own
recorded parent" into "each bone's own recorded children list"), **visiting
children at every node in ASCENDING bone-index order** for full determinism
regardless of Step D's own discovery order. Append each bone to
`jointBoneIndices` in the exact order visited; `parentJointIndex[i]` is then
simply "the position within jointBoneIndices where this bone's own recorded
parent bone landed" (or `-1` if the parent was the anchor itself) — by
construction (BFS/DFS parent-before-children), this is ALWAYS an earlier
index than `i`, satisfying Phase 1's own invariant exactly.
`restLengths[i] = Length(skeleton.bones[jointBoneIndices[i]].position -
(parentJointIndex[i] < 0 ? skeleton.bones[rootBoneIndex].position :
skeleton.bones[jointBoneIndices[parentJointIndex[i]]].position))` — the
exact same bind-pose-distance formula the old algorithm used, just resolved
against the tree parent instead of `i-1`.

**Step F — fold every remaining original Joint into `extraConstraints` (or
drop it as cross-chain, with a diagnostic).** After every chain's
`jointBoneIndices` is finalized, iterate `physics->joints` ONCE MORE (in
ascending `jointIndex` order, for determinism), and for each joint, resolve
both `rigidBodyAIndex`/`rigidBodyBIndex` to their own bone index (via
`RigidBody::boneIndex`), then look each bone up in a
`boneIndex → (chainIndex, jointIndexInChain)` reverse map built from every
chain's own `jointBoneIndices` in Step E. Four cases:

  1. Either endpoint doesn't resolve to any chain member at all (e.g. one
     side is an orphan, a Static anchor bone itself, or a rigid body with no
     `boneIndex`) → skip this joint entirely, nothing to add (a joint
     touching the anchor/root itself is implicitly already "structural" via
     `restLengths[0]`-equivalent tree edges; a joint touching an orphan is
     already covered by that bone's own orphan flag).
  2. Both endpoints resolve to the SAME chain, and together they ALREADY
     form that chain's own tree parent/child pair (i.e. one endpoint's
     `jointIndexInChain` equals the OTHER endpoint's own
     `parentJointIndex` — checked by BONE-PAIR relationship, never by
     "happens to be the same PMX `Joint` object Step D/E originally used,"
     since a chain's tree edges are derived from skeleton ancestry, not from
     any specific `Joint` at all) → skip (already represented as the
     implicit tree edge / `restLengths[i]`; do not double-add it as ALSO an
     `ExtraStructuralConstraint`, which would make that segment twice as
     stiff for no reason).
  3. Both endpoints resolve to the SAME chain, but are NOT a tree edge (a
     genuine "ring brace"/cross joint) → append
     `ExtraStructuralConstraint{jointIndexInChainA, jointIndexInChainB,
     Length(skeleton.bones[boneA].position - skeleton.bones[boneB].position)}`
     to that chain's own `extraConstraints`. De-duplicate identical
     `(jointIndexA, jointIndexB)` unordered pairs (two different PMX Joints
     that happen to connect the exact same two bones) by keeping only the
     FIRST one encountered in `jointIndex` order.
  4. Both endpoints resolve to chain members, but of TWO DIFFERENT chains (a
     genuine cross-chain brace, e.g. two independently-anchored skirt panels
     laced together) → skip, but record it in a new, purely-diagnostic
     output (see 3.2's `crossChainJointsDropped` field) so this is never
     silently invisible — Phase 5 can surface it later if desired, but this
     phase's own job is only to make sure it never crashes/corrupts a chain
     and is never lost information (logged in the return value, even if
     nothing currently reads it — matches this codebase's own general
     "degrade gracefully, never silently" convention).

**Step G — apply `defaults.minimumChainLength` and seed
`DynamicJointSettings`/head-collider defaults.** Identical in SPIRIT to the
old algorithm's own corresponding logic (`DynamicChainDetection.cpp` lines
110-120, 132-150) — a finished chain shorter than
`defaults.minimumChainLength` joints is discarded entirely (never
partially); every joint's `DynamicJointSettings` starts from
`defaults.defaultJointSettings`, then is overridden per-joint by that exact
bone's own matched `RigidBody::mass`/`linearDamping` (this part of the OLD
algorithm, lines 152-162, is REUSED almost verbatim — the only difference is
the matched `RigidBody` is now looked up via Step C's already-built
`boneIndexToRigidBodyIndex` map instead of a fresh linear scan);
`maxPlausibleRootDelta`/`hasHeadCollider`/`headColliderBoneIndex`/
`headColliderRadius` are seeded exactly as before, using each chain's own
final `restLengths` sum. **(v2 note: a chain discarded here for being too
short does NOT retroactively move its own bones into `orphanedBoneIndices`
— it simply detects zero chains for that region, same as the original
algorithm's own equivalent case; "too short to bother simulating" and
"provably unreachable from any anchor" remain two distinct, un-conflated
concepts, exactly as in v1.)**

**Step H — sort the final chain list for determinism.** Sort the returned
`std::vector<DynamicChainDefinition>` by ascending `rootBoneIndex` (ties
broken by ascending `jointBoneIndices[0]`, which cannot collide given the
disjointness invariant) before returning. Also sort and deduplicate the three
diagnostic vectors (`orphanedBoneIndices`, `crossChainJointsDropped`,
`duplicateAssignmentsDropped`) ascending before assigning them into the
returned `DynamicChainDetectionDiagnostics` (v2: `orphanedBoneIndices` in
particular is now populated from TWO sources — Step C pass 1 and Step D case
3/4 — so an explicit final sort+dedup is required to keep the "fully
deterministic" guarantee regardless of which source found a given bone
first) — so two calls against logically-identical-but-differently-ordered
`PhysicsData` always return chains AND diagnostics in the same relative
order too (not just the same CONTENTS) — completes the "fully deterministic"
requirement end-to-end.

### 3.2 New/changed public signature (`src/Physics/DynamicChainDetection.h`)

```cpp
// task_manager/verlet-integration-6, Phase 3 - purely diagnostic output,
// alongside the real chains - never affects simulation, but must never be
// silently lost (see this phase's own Step F, case 4, and Step C/D, v2).
struct DynamicChainDetectionDiagnostics {
    // Dynamic/DynamicAndBoneMerge rigid-body bones that could not reach ANY
    // Static anchor - populated from TWO sources, unioned and deduplicated
    // (Step H): (a) Step C, pass 1 (v2) - bones whose own rigid body Phase 2
    // already proved is not graph-reachable from any Static body at all via
    // ANY path, and (b) Step D, case 3/4 - bones that ARE graph-reachable
    // but whose own real skeleton ancestry never crosses a Static anchor
    // bone (or hits a cycle - case 4). NOT simulated; a later Editor phase
    // may render these distinctly (see PHASE5).
    std::vector<std::int32_t> orphanedDynamicBoneIndices;
    // Original PhysicsData::joints indices whose two endpoints landed in two
    // DIFFERENT final chains - dropped, never applied, never crashes - see
    // this phase's own Step F, case 4.
    std::vector<std::int32_t> crossChainJointsDropped;
    // task_manager/verlet-integration-6, Phase 3 (v2) - RigidBody indices
    // dropped by Step C's "same boneIndex, keep lowest index" tie-break
    // (pass 3) - see PHASE0_MASTER_STRATEGY.md's own "Revision Notes (v2)",
    // finding #5, for why this is worth surfacing even though it is a rare,
    // arguably-authoring-error case: a Static-vs-Dynamic collision on one
    // bone silently decides whether that bone becomes an ANCHOR or a
    // MEMBER, a materially significant, previously-invisible outcome.
    std::vector<std::int32_t> duplicateBoneRigidBodyAssignmentsDropped;
};

struct DynamicChainDetectionResult {
    std::vector<DynamicChainDefinition> chains;
    DynamicChainDetectionDiagnostics diagnostics;
};

// Detects dynamic bone chains by walking PhysicsData's own RigidBody/Joint
// graph (task_manager/verlet-integration-6 REPLACES the previous
// Bone::deformAfterPhysics-based algorithm entirely - see this campaign's
// PHASE0_MASTER_STRATEGY.md for the full rationale). Returns an EMPTY result
// if `physics` is nullptr (no PMX rigid body/joint data at all) - a model
// with no physics data authored has nothing to detect chains FROM under
// this algorithm; this is an intentional behavior change from the old
// bone-flag-only algorithm (see this phase's own "Known Behavior Change").
// See DynamicChainDetection.cpp's own top-of-file comment for the full
// algorithm write-up (Steps A-H).
DynamicChainDetectionResult DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults);
```

Note the return type changes from `std::vector<DynamicChainDefinition>` to
`DynamicChainDetectionResult` — Phase 4 updates the one real call site
(`PhysicsSystem::RegisterDynamicChains()`) accordingly. **(v2: `ModelEntry`
copies this whole struct by value in Phase 4 — the new
`duplicateBoneRigidBodyAssignmentsDropped` field needs zero additional code
anywhere in Phase 4 to round-trip through the cache.)**

### 3.3 Rewritten test file `tests/Physics/DynamicChainDetectionTests.cpp`

Delete every existing test in this file (they assert the OLD
`Bone::deformAfterPhysics`-driven behavior, which no longer exists) and
replace with, at minimum:

- **`SimpleLinearRigWithOneStaticAnchorAndThreeDynamicBodiesDetectsOneLinearChain`**
  — a 4-bone skeleton (0=root/static-anchor-bone, 1/2/3 a straight FK chain),
  `PhysicsData` with `RigidBody`s: 0=Static(bone 0), 1/2/3=Dynamic(bones
  1/2/3), `Joint`s connecting 0↔1, 1↔2, 2↔3. Assert exactly one chain,
  `rootBoneIndex == 0`, `jointBoneIndices == {1, 2, 3}`,
  `parentJointIndex == {-1, 0, 1}`, `extraConstraints` empty, `restLengths`
  match bind-pose distances (mirrors the OLD file's own
  `SimpleThreeJointChainDetectedWithCorrectRootAndOrderAndRestLengths`, now
  driven by RigidBody/Joint data instead of `deformAfterPhysics`).
- **`SpiderWebSkirtWithFourBranchesAndCrossBraceProducesOneSingleTreeChain`**
  — one Static hip bone (0), four Dynamic strand-root bones (1,2,3,4, each a
  direct skeleton child of bone 0), each with one further Dynamic tip bone
  (5,6,7,8, each a skeleton child of its own strand root), Joints for every
  real parent/child pair PLUS one extra "ring brace" Joint directly between
  rigid bodies on bones 1 and 2 (siblings, NOT parent/child). Assert exactly
  ONE chain (never four — this is the direct regression test for the user's
  own "single chain with convoluted joints" requirement), `jointBoneIndices`
  contains all 8 bones, `parentJointIndex` correctly encodes the 4-branch
  tree shape, and `extraConstraints` contains exactly one entry referencing
  bones 1 and 2's own positions within `jointBoneIndices`, with the correct
  bind-pose `restLength`.
- **`NonHierarchyAlignedJointDoesNotBecomeATreeEdgeButAppearsAsExtraConstraint`**
  — two Dynamic bones that ARE Joint-connected to each other but are NOT
  skeleton parent/child (e.g. both are direct children of the SAME Static
  anchor bone, so they're skeleton siblings) plus a direct Joint between
  them: assert BOTH still end up in the chain (each via its own real
  parent-anchor edge), and the sibling-to-sibling Joint becomes an
  `ExtraStructuralConstraint`, never a `parentJointIndex` entry (this is the
  direct regression test for `PHASE0_MASTER_STRATEGY.md`'s Culprit A/the
  user's "make anything work perfect" answer — proof the hierarchy
  constraint is enforced, not silently violated).
- **`MultipleIndependentAnchorsProduceSeparateDisjointChains`** — two
  entirely separate Static-rooted linear rigs sharing one `SkeletonData`/
  `PhysicsData` (no Joint between them at all): assert exactly two chains,
  each with the expected `rootBoneIndex`/`jointBoneIndices`, and assert (by
  explicit `std::set` intersection check) their `jointBoneIndices` sets are
  fully disjoint (direct regression test for
  `PHASE0_MASTER_STRATEGY.md`'s Culprit E).
- **`GraphUnreachableDynamicBodyBoneIndexAppearsInOrphanedDynamicBoneIndices`
  (v2, new — the direct regression test for Culprit F)** — a Dynamic-only
  island (two Dynamic bodies jointed only to each other, no Static body
  anywhere in the whole `PhysicsData`, mirroring Phase 2's own
  `OrphanedIslandWithNoStaticAnchorAnywhereIsFlagged` fixture exactly):
  assert `chains` is empty AND `diagnostics.orphanedDynamicBoneIndices`
  contains BOTH bones' own bone indices. This is the exact scenario Culprit F
  describes; before the v2 fix to Step C/D, this test would have failed
  (both bones would have vanished from the result entirely — not in any
  chain, not in the orphan list either). This test's name is deliberately
  distinct from the pre-existing
  `DynamicBodyWithNoReachableStaticAnchorIsOrphanedNotSimulated` test below
  (which exercises Step D's OWN case 3 for a graph-reachable-but-hierarchy-
  unreachable bone) — the two tests exercise the two DIFFERENT sources that
  now both feed the same, unioned `orphanedDynamicBoneIndices` list.
- **`DynamicBodyWithNoReachableStaticAnchorIsOrphanedNotSimulated`** — a
  Dynamic-only island (two Dynamic bodies jointed to each other, no Static
  body anywhere): assert `chains` is empty and
  `diagnostics.orphanedDynamicBoneIndices` contains both bones' indices
  (direct regression test for the user's own explicit orphan-flagging
  answer). **(v2 note: kept verbatim from v1; this test alone already
  exercises the SAME fixture shape as the new
  `GraphUnreachableDynamicBodyBoneIndexAppearsInOrphanedDynamicBoneIndices`
  test above — both are retained, deliberately, as two independently-named,
  independently-reasoned-about regression tests for what is fundamentally
  one algorithmic guarantee, since this exact guarantee is precisely what
  Culprit F violated; redundancy here is intentional insurance, not an
  oversight.)**
- **`CrossChainJointBetweenTwoDifferentAnchorsIsDroppedAndDiagnosed`** — two
  separately-anchored chains (as in the multi-anchor test above) PLUS one
  extra Joint directly bracing a bone from chain A to a bone from chain B:
  assert the two chains remain unaffected/disjoint, and
  `diagnostics.crossChainJointsDropped` contains that joint's own index.
- **`DuplicateBoneRigidBodyAssignmentPicksLowestIndexDeterministically`** —
  two different `RigidBody` entries both referencing the SAME `boneIndex`:
  assert the result is well-formed and depends only on which rigid body has
  the lower ARRAY index, never which one happens to be scanned first by
  incidental container iteration order (this codebase's `std::vector`
  iteration is already index-ordered, so this mostly documents/locks the
  intended tie-break rule rather than testing genuine nondeterminism).
- **`DuplicateBoneRigidBodyAssignmentDropRecordedInDiagnostics` (v2, new)**
  — same fixture as the test directly above, but additionally asserting
  `diagnostics.duplicateBoneRigidBodyAssignmentsDropped` contains the
  HIGHER-index (dropped) rigid body's own index — the direct regression
  test for `PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)", finding #5.
- **`UnattachedRigidBodiesAreExcludedFromEligibilityAndNeverCauseSpuriousDuplicateCollisions`
  (v2, new)** — two entirely unrelated rigid bodies (one Static, one
  Dynamic-and-orphaned, or any other combination) both with `boneIndex ==
  -1`: assert the result is identical to a fixture with those two bodies
  removed entirely, and specifically assert
  `diagnostics.duplicateBoneRigidBodyAssignmentsDropped` is EMPTY (proof the
  `boneIndex < 0` exclusion in Step C, pass 2, runs BEFORE the tie-break in
  pass 3, so these two unrelated bodies never spuriously "collide").
- **`CyclicSkeletonAncestryIsTreatedAsOrphanNotInfiniteLoop` (v2, new)** — a
  deliberately malformed 2-bone skeleton where bone 0's `parentBoneIndex ==
  1` and bone 1's `parentBoneIndex == 0` (a cycle), with a Dynamic rigid
  body graph-reachable from a Static anchor attached to one of them: assert
  `DetectDynamicChains()` returns (does not hang) within the test's own
  timeout, `chains` is empty, and the cyclic bone appears in
  `diagnostics.orphanedDynamicBoneIndices` — the direct regression test for
  Step D's new case 4 cycle guard.
- **`OutOfRangeParentBoneIndexIsTreatedLikeNoParent` (v2, new)** — a bone
  whose own `parentBoneIndex` is set to an out-of-range value (e.g.
  `skeleton.bones.size() + 5`) rather than `-1`: assert this behaves
  identically to the same fixture with `parentBoneIndex == -1` instead (both
  produce the same orphan classification, neither crashes/reads out of
  bounds) — the direct regression test for Step D case 3's explicit
  out-of-range handling.
- **`RunShorterThanMinimumChainLengthDetectsNothing`** — ported from the old
  file, same intent, new RigidBody/Joint-driven fixture.
- **`NullPhysicsDataDetectsNothing`** — replaces the old file's
  `EmptySkeletonDetectsNothing` test; asserts `physics == nullptr` returns an
  entirely empty result (Step A) — this is the new file's explicit
  regression test for this phase's own "Known Behavior Change" below.
- **`ResultIsIdenticalRegardlessOfRigidBodyAndJointStorageOrder`** — build
  the spider-web fixture twice, with `physics.rigidBodies`/`physics.joints`
  renumbered/reordered consistently between the two builds, and assert the
  two `DetectDynamicChains()` results are equal chain-for-chain (same
  `rootBoneIndex`, same `jointBoneIndices` VALUES in the same relative
  positions, same `parentJointIndex`, same `extraConstraints` — compared
  structurally, not by raw memory) **and that all three diagnostics vectors
  (v2) are equal as SETS too** — the end-to-end regression test proving
  Step H's sort plus every earlier step's own ascending-order processing
  together deliver the user's "order-independent but fully deterministic"
  requirement for the WHOLE algorithm, not just Phase 2's graph module in
  isolation.

### 3.4 Known Behavior Change (call this out explicitly wherever this
function is referenced elsewhere in the codebase, e.g. `PhysicsSystem.cpp`'s
own comment at line ~131-137)

A model with `RigidBody`/`Joint` PMX data entirely absent (`physics ==
nullptr`), or present but with zero `Joint`s at all (e.g. only collision
rigid bodies, no dynamics chain authored), now detects **zero** chains,
regardless of how many bones have `Bone::deformAfterPhysics == true`. Under
the OLD algorithm this same model could still detect chains purely from the
bone flag. This is an intentional, accepted consequence of the user's own
explicit "fully replace" answer (`PHASE0_MASTER_STRATEGY.md`, Step 1) — MMD
models that rely purely on `deformAfterPhysics` with no authored RigidBody/
Joint physics data at all will simply no longer animate via this system
after this campaign lands. Document this prominently in this phase's own
code comments so it is never mistaken for a regression bug later.

### 3.5 Known Limitation (carried over from Phase 1's own test, 3.4)

When two OR MORE joints in the same chain share the exact same
`parentJointIndex`-resolved parent bone (a genuine "hub," e.g. the spider-web
skirt's own Static-anchor-adjacent strand roots, or any bone with multiple
Dynamic children), `BoneChainPhysicsResolver.cpp`'s `ApplyDynamicChainPhysicsToPose()`
processes joints strictly in ascending `i` order and, for a shared parent,
each subsequent child's own corrective rotation OVERWRITES the previous
child's — meaning only the LAST child processed at a given hub each frame
"wins" the parent bone's final rotation, and earlier siblings at that exact
hub will not visually reach their own simulated target as precisely (they
still get a physically-simulated Verlet position — this only affects how
faithfully the FK POSE reproduces it for extra siblings sharing one rotating
parent). This is an accepted, documented limitation of the current
single-corrective-rotation-per-bone-per-frame design (see
`BoneChainPhysicsResolver.h`'s own header comment) — NOT something this
campaign attempts to fix (mirrors `PHASE0_MASTER_STRATEGY.md`'s own "What We
Will NOT Do" scope boundary: fully generalizing the FK application to
average/blend multiple children's own corrective rotations per hub bone
would be a legitimate, separate future campaign). Call this out explicitly
in `DetectDynamicChains()`'s own top-of-file doc comment so a future reader
understands why a spider-web hub's individual strands may look very slightly
less precise than a single-strand chain, without mistaking it for something
Phase 3 got wrong. **(v2: re-confirmed unchanged and still accepted during
this revision's own review — see `PHASE0_MASTER_STRATEGY.md`'s Step 4, last
bullet.)**

## Revision Notes (v2)

Summary of every change in this revision (full rationale for each lives in
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)" section; this list
exists so a reader of THIS file alone still gets the complete picture):

1. **Culprit F fix (Critical/Correctness)** — Step C now has three explicit
   passes instead of one implicit one; pass 1 seeds
   `orphanedBoneIndices` directly from `reach.orphanedDynamicRigidBodyIndices`
   BEFORE the eligibility map is even built, so a true graph-orphan is
   guaranteed to reach the final diagnostics regardless of what Step D does
   or doesn't walk. New test:
   `GraphUnreachableDynamicBodyBoneIndexAppearsInOrphanedDynamicBoneIndices`.
2. **Cycle guard (Robustness)** — Step D case 4 (new): a per-walk visited-set
   turns a malformed cyclic skeleton into a graceful orphan classification
   plus a debug-only assertion, instead of an infinite loop. New test:
   `CyclicSkeletonAncestryIsTreatedAsOrphanNotInfiniteLoop`.
3. **Out-of-range parent guard (Robustness)** — Step D case 3 now explicitly
   treats `parentBoneIndex >= skeleton.bones.size()` identically to
   `parentBoneIndex < 0`, preventing a possible out-of-bounds
   `skeleton.bones[...]` read on corrupted data. New test:
   `OutOfRangeParentBoneIndexIsTreatedLikeNoParent`.
4. **Unattached-body exclusion ordering (Robustness/clarity)** — Step C pass
   2 now explicitly excludes `boneIndex < 0` rigid bodies BEFORE pass 3's
   duplicate tie-break runs, so two unrelated unattached bodies can never
   spuriously collide on the sentinel key. New test:
   `UnattachedRigidBodiesAreExcludedFromEligibilityAndNeverCauseSpuriousDuplicateCollisions`.
5. **New diagnostic field (QoL/parity)** —
   `DynamicChainDetectionDiagnostics::duplicateBoneRigidBodyAssignmentsDropped`
   records every rigid body index dropped by Step C pass 3's tie-break,
   mirroring the already-existing `crossChainJointsDropped` field/convention
   exactly. New test: `DuplicateBoneRigidBodyAssignmentDropRecordedInDiagnostics`.
6. **Step F case 2 wording clarified** — explicitly states the tree-edge
   check is by bone-pair/tree-relationship, not by "the literal `Joint`
   object Step D happened to use," removing an ambiguity in the v1 wording
   (no behavior change, the intended algorithm was already this — only the
   prose was underspecified).
7. **Step H extended** — the three diagnostic vectors are now explicitly
   sorted+deduplicated alongside the chain list, since
   `orphanedDynamicBoneIndices` is now populated from two separate sources
   that must be merged deterministically.

No change was made to Steps A, B, E, G, or the overall public-signature
philosophy (`DynamicChainDetectionResult` wrapping `chains` +
`diagnostics`) — all independently re-verified accurate against the current
source tree during this v2 review.
