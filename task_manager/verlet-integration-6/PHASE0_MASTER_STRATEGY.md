# PHASE0 — MASTER STRATEGY: Physics-Graph-Driven Dynamic Chain Detection (v2)

Orchestrator document for the `verlet-integration-6` campaign. Every child
phase document in this folder implements one slice of this plan. Read this
file first, then execute `PHASE1_...md` → `PHASE5_...md` strictly in order —
each phase's code depends on the previous phase's deliverables already
compiling. Every phase below is a real, compilable, testable increment that
adds or edits real `.h`/`.cpp` files; none of them are "just planning."

**This is the v2 revision of this document.** A full second-pass review of
every child phase document against the actual current source tree (see
`## Revision Notes (v2)` at the very end of this file) found one genuine
correctness bug in the originally-drafted algorithm (Culprit F, below — the
kind that would have made Phase 3's own test suite fail against its own
spec), plus several robustness/clarity/QoL gaps. All of them are now folded
directly into the phase documents that own the affected code (Phase 1, Phase
3, Phase 5) — this is not a purely additive changelog, the child documents'
own Step 3 plans were edited in place so an implementer reading only the
child phase, without ever reading this Revision Notes section, still gets
the corrected algorithm.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis, user-alignment decisions, ordering. |
| `PHASE1_TREE_BASED_CHAIN_DATA_MODEL_AND_SOLVER.md` | Generalizes `DynamicChainDefinition` from an implicit flat linked-list (`jointBoneIndices[i]`'s parent is ALWAYS `jointBoneIndices[i-1]`) to an explicit **tree** (`parentJointIndex[i]`) plus a new **extra structural constraint** list (`extraConstraints`) for non-hierarchy "web/brace" joints. Updates the three runtime consumers that hard-code the old flat assumption: `BoneChainPhysicsResolver.cpp`, `DynamicChainSolver.cpp`, and their existing tests. **(v2: the resolver code-change section was corrected — see this file's own "found nothing to fix in the underlying design, only in the illustrative snippet" note below.)** |
| `PHASE2_RIGIDBODY_JOINT_GRAPH_ANALYSIS.md` | A new, pure, Tier-1-tested module (`src/Physics/RigidBodyJointGraph.h/.cpp`) that turns `PhysicsData::rigidBodies`/`joints` into an adjacency graph and answers "is this Dynamic rigid body graph-reachable from any Static rigid body?" — the raw material Phase 3 consumes. Zero skeleton/ECS/Editor dependency. Unchanged in v2 — this module's own contract is exactly what Phase 3 v2 needs, no gap found here. |
| `PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md` | The heart of the campaign: a full, from-scratch rewrite of `DetectDynamicChains()` (`src/Physics/DynamicChainDetection.h/.cpp`) that walks the REAL skeleton bone-ancestor hierarchy (never the raw joint graph) to build each chain's tree, using Phase 2's graph purely to decide chain **membership**/**anchors**, and folds every remaining original PMX Joint into either a tree edge or an `ExtraStructuralConstraint`. Also introduces the new orphan-diagnostic output. Old `Bone::deformAfterPhysics`-based algorithm and its whole test file are deleted and replaced. **(v2: fixes Culprit F — a real orphan-reporting bug — plus adds a skeleton-ancestry cycle guard, an out-of-range-parent guard, and a new `duplicateBoneRigidBodyAssignmentsDropped` diagnostic; three new regression tests added.)** |
| `PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md` | Rewires `PhysicsSystem::RegisterDynamicChains()`, `DynamicChainRigCache::ModelEntry`, and every existing fixture/test that hand-builds a `DynamicChainDefinition` (Phase 1 fallout) so the whole engine compiles and the parallel-dispatch disjoint-chain invariant is re-verified defensively. Unchanged in v2 — this phase copies the whole `DynamicChainDetectionDiagnostics` struct by value, so Phase 3 v2's new field round-trips through the cache automatically with zero additional code here (confirmed explicitly, see this phase's own short v2 note). |
| `PHASE5_EDITOR_VISUALIZATION_TREE_WEB_AND_ORPHAN_GIZMO.md` | Updates `BoneViewerWindow.cpp`'s Verlet-mode gizmo (built by `verlet-integration-5`) to draw the new tree/branch shape correctly instead of a false straight line, draws `extraConstraints` as a visually distinct "web brace" line style, and renders orphaned (non-simulated) rigid bodies as a red marker — directly answering the user's explicit ask for visual feedback. **(v2: adds a hover tooltip explaining WHY a bone is orphaned, and surfaces the previously-totally-invisible `crossChainJointsDropped`/`duplicateBoneRigidBodyAssignmentsDropped` diagnostics as simple text in the tree pane — closing the same "never silently drop information" gap the original document already fixed for orphans but left open for these two siblings.)** |

## Step 1: The Goal (Where are we going?)

Today, `DetectDynamicChains()` (`src/Physics/DynamicChainDetection.h/.cpp`)
derives every simulated "jiggle" chain **exclusively from
`Bone::deformAfterPhysics`** and the skeleton's own bone-parent hierarchy. It
reads `PhysicsData`/`RigidBody` only as an afterthought — to borrow
`mass`/`linearDamping` for a joint that was ALREADY discovered by the bone
flag — and it **never looks at `PhysicsData::joints` (PMX Joints) at all**.
This is backwards from how MMD models actually author physics: the
authoritative topology of "what is connected to what, and where does it
anchor" is the **RigidBody + Joint graph** (`root -(Joint A)- [RigidBody A]
-(Joint B)- [RigidBody B] - ...`, per the user's own diagram), with
`RigidBodyMotionType::Static` marking the fixed anchor(s) and everything else
(`Dynamic`/`DynamicAndBoneMerge`) marking what should actually swing.

This campaign makes `DetectDynamicChains()` build chains by **traversing the
real RigidBody/Joint graph** instead of trusting `deformAfterPhysics`, per
this exact directive from the user and their own follow-up answers (quoted
verbatim below, each of which is now a binding design constraint for every
later phase):

- *"i want you to make a modification for `DynamicChainDetection` so it
  pick-up random joint and iterate all rigid bodies, then pick up neighboring
  or connecting other joint, repeat this until find dead-end and root where
  rigid body motion type is static, use this data to create verlet chain and
  joint"* — the graph traversal itself (Phase 2 + Phase 3).
- *"Fully replace the old algorithm everywhere"* — no dual-path/fallback;
  `Bone::deformAfterPhysics` stops being read by chain detection at all
  (Phase 3). It remains untouched as a normal `Bone` field/Inspector checkbox
  for everything else (`Editor/Panels/InspectorPanel.cpp` line 222) — this
  campaign only removes it from `DynamicChainDetection.cpp`.
- *"Order-independent but fully deterministic (same input always produces
  same chains)"* — every phase's algorithm must be specified as a canonical,
  order-independent construction (sorted by bone index, never by "whatever
  order the array happened to be in"), and every new test must include an
  explicit shuffled-input regression case (Phase 2 and Phase 3).
- *"for skirt, the joint will form like a spider web, a rigid body might
  connect to 4 or more rigid bodies. in this case i want to treat this skirt
  as a single chain with convoluted joints"* — a single connected,
  same-anchor region becomes exactly ONE `DynamicChainDefinition`, however
  many branches/cross-braces it internally has, never split per-branch the
  way the old bone-hierarchy algorithm did. This is the single biggest
  architectural driver of this campaign: it is why `DynamicChainDefinition`
  itself must stop being a flat list and become a tree (Phase 1), and why a
  brand-new `extraConstraints` concept exists at all (Phase 1 + Phase 3).
- *"this very hard question, i really dont know how to answer it. but
  ultimately verlet integration will be used to animate skirt or hair using
  physics simulation. so you must do anything to make the animation work
  perfect."* — this is the user deferring the hard bone-hierarchy-alignment
  call to us. Step 2, Culprit A below explains the actual constraint this
  answer runs into, and Step 3 explains the concrete, code-level resolution
  chosen: EVERY authored PMX Joint that connects two bones this campaign
  decides to simulate is used as a REAL simulation constraint — either as a
  tree edge that drives visible bone rotation (when it happens to align with
  the skeleton's own real parent/child relationship) or as a pure
  `ExtraStructuralConstraint` (when it doesn't) — nothing authored is ever
  silently thrown away for no reason.
- *"i dont think such scenario possible but to make safe, flag it as
  non-physics simulated instances. and make gizmo appear red, or anthing so i
  can see what happen in the editor"* — orphaned Dynamic rigid bodies (no
  Static anchor reachable at all) are detected, excluded from simulation, and
  surfaced as a new diagnostic list all the way through to a red Editor
  gizmo (Phase 3 produces the data, Phase 4 threads it through the cache,
  Phase 5 draws it). **v2 note: this exact requirement is what Culprit F
  below turned out to violate in the original draft — a body with genuinely
  ZERO Static anchor anywhere in its own connected component was silently
  dropped instead of flagged. Phase 3's Step C/D are corrected so this
  requirement is actually met end-to-end, not just described.**

## Step 2: The Situation / The Problem (Where are we now?)

A full read of `src/Physics/DynamicChainDetection.h/.cpp`,
`src/Physics/DynamicChainDefinition.h/.cpp`,
`src/Physics/BoneChainPhysicsResolver.h/.cpp`,
`src/Physics/DynamicChainSolver.h/.cpp`, `src/Physics/ChainConstraints.h/.cpp`,
`src/Assets/PhysicsData.h`, `src/Game/Physics/PhysicsSystem.cpp`,
`src/Game/Physics/DynamicChainRigCache.h`, and the Verlet-mode code added by
`verlet-integration-5` (`src/Editor/BoneViewerWindow.cpp`,
`src/Editor/Panels/InspectorPanel.cpp`) turned up the exact culprits this
campaign's phases each close:

1. **Culprit A — the engine's bone-rotation trick only works along REAL
   skeleton parent/child pairs, and the current chain model conflates "rest
   length neighbor" with "rotation-driving parent" into a single, implicit
   `i-1` relationship.** `BoneChainPhysicsResolver.cpp`'s own header comment
   (`BoneChainPhysicsResolver.h`, "IMPORTANT DESIGN NOTE") is explicit: a
   bone's rotation can only ever move its **descendants**, never itself — so
   to make joint `i` visually land at its simulated position,
   `ApplyDynamicChainPhysicsToPose()` must rewrite the rotation of joint
   `i`'s **actual skeleton parent bone**
   (`skeleton.bones[jointBoneIndices[i]].parentBoneIndex`), and today that
   parent is HARD-CODED as `definition.jointBoneIndices[i - 1]`
   (`BoneChainPhysicsResolver.cpp` line 52) — i.e. the code silently assumes
   the detected chain's array order already IS the real skeleton hierarchy
   order. `DynamicChainSolver.cpp`'s own structural-constraint loop (lines
   94-97) makes the identical `i-1` assumption for which particle to
   distance-constrain against which. This is exactly why the OLD
   `DetectDynamicChains()` could get away with `Bone::deformAfterPhysics` +
   walking `childrenByParent` (`WalkChainRun()`,
   `DynamicChainDetection.cpp` lines 36-60) — that walk is, by construction,
   ALREADY a genuine skeleton parent-to-child walk, so "index i-1" and "real
   parent bone" were always the same thing. A graph traversal driven by
   `PhysicsData::joints` has NO such guarantee — an MMD skirt's authored
   Joints commonly include **horizontal "ring"/cross-bracing constraints
   between SIBLING bones** (two bones that share a parent, or aren't related
   in the skeleton at all) specifically to stop the cloth from
   interpenetrating/flipping. Naively feeding raw joint-graph order into the
   existing flat `jointBoneIndices` array would silently break the bone
   rotation trick for any such edge (the resolver would rotate the wrong
   bone, or a bone that has no actual FK effect on the intended joint at
   all) — a subtle, hard-to-diagnose "chain looks static/wrong in the
   viewport" bug, not a compile error. **Fixed by Phase 1** (generalizing the
   data model to an explicit tree, so "rotation-driving parent" and "extra
   stabilizing neighbor" become two clearly separate concepts instead of one
   overloaded array index) **and Phase 3** (which derives the tree ONLY from
   real skeleton ancestry, and routes every other authored Joint into the new
   non-hierarchy-constrained `extraConstraints` list instead — see this
   phase's own Step 3 for the exact algorithm).
2. **Culprit B — `DetectDynamicChains()` never reads `PhysicsData::joints` at
   all, so PMX-authored connectivity/anchoring information the user
   explicitly wants used is completely ignored today.** The only physics-data
   read in the whole file is a `boneIndex`-keyed `RigidBody` lookup used
   purely to override `mass`/`damping` on a joint the BONE FLAG already found
   (`DynamicChainDetection.cpp` lines 152-162) — `RigidBodyMotionType::Static`
   is checked only as a "don't borrow settings from a collider" filter, never
   as "this is where a chain roots." **Fixed by Phase 2** (a real graph over
   `rigidBodies`/`joints`) **and Phase 3** (which makes that graph the actual
   source of chain membership/anchoring).
3. **Culprit C — the old algorithm splits every branch into a SEPARATE
   `DynamicChainDefinition`, which is the opposite of what a spider-web skirt
   needs.** `DetectDynamicChains()`'s outer loop treats "more than one
   `deformAfterPhysics` child" as a hard chain-start boundary
   (`CountPhysicsChildren(...) > 1` at line 100), i.e. one new chain object
   PER branch, confirmed by
   `DynamicChainDetectionTests.cpp`'s own
   `BranchingRunDetectsTwoSeparateChainsEachStartingAtItsOwnBranchChild` test.
   The user explicitly wants the opposite for a skirt's web topology: ONE
   chain, with the branching AND the cross-bracing joints both represented
   inside it. **Fixed by Phase 1's tree generalization + Phase 3's
   single-chain-per-anchor-region construction.**
4. **Culprit D — nothing today distinguishes "a Dynamic rigid body physics
   data describes but that isn't actually reachable from any anchor" from
   "a normal, working joint," so there is no way for `PhysicsSystem` or the
   Editor to ever tell a user their rig has a floating/misconfigured physics
   island.** `DynamicChainRigCache::ModelEntry` only stores `chains` +
   `skeleton`, `PhysicsSystem::AttachDynamicChainRigIfNeeded()`
   (`PhysicsSystem.cpp` lines 148-158) only reacts to `model->chains`, and
   the Verlet-mode gizmo built by `verlet-integration-5`
   (`BoneViewerWindow.cpp` lines 1158-1170, 1372-1386) only ever iterates
   `chain.jointBoneIndices` — there is no code path anywhere that could show
   an "orphaned" rigid body even if `DetectDynamicChains()` identified one
   today (it can't — see Culprit B). **Fixed by Phase 3** (produces the
   orphan list — see also Culprit F below, a bug found in this exact piece
   of Phase 3 during the v2 review) **+ Phase 4** (threads it through the
   cache) **+ Phase 5** (renders it in red, per the user's explicit request).
5. **Culprit E — the parallel job-dispatch path
   (`PhysicsSystem::Update()`, `PhysicsSystem.cpp` lines 199-223) is only
   safe because every two chains' `jointBoneIndices` sets are DISJOINT — a
   comment-documented invariant (`PhysicsSystem.cpp` lines 44-49) that was
   trivially true under the old "one linear walk per branch" algorithm but is
   no longer obviously, structurally true once chain membership comes from an
   anchor-reachability walk over an arbitrary graph** (e.g. could two
   different anchors' BFS regions overlap on a shared bone under some
   pathological rig?). **Fixed by Phase 3's construction rule (each
   participating bone is assigned to EXACTLY ONE chain, by explicit,
   deterministic tie-break)** and **Phase 4's defensive re-verification**
   (a debug-time assertion in `RegisterDynamicChains()` that no bone index
   appears in two different chains' `jointBoneIndices`, so any future
   regression here fails loudly in a debug build instead of silently
   corrupting a shared pose buffer under the parallel path).
6. **Culprit F (found during this campaign's own v2 review pass) — the
   originally-drafted Phase 3 algorithm silently DROPS every genuinely
   graph-orphaned Dynamic rigid body instead of ever flagging it, directly
   contradicting the user's own explicit orphan-flagging requirement (quoted
   in Step 1 above) and Phase 3's own test list.** Phase 2's
   `ComputeReachabilityFromStaticAnchors()` already computes
   `orphanedDynamicRigidBodyIndices` — bodies with NO Static anchor reachable
   via the joint graph AT ALL (e.g. two Dynamic bodies jointed only to each
   other, no Static body anywhere in the whole `PhysicsData`). The original
   Phase 3 Step C built `boneIndexToRigidBodyIndex` using only `Static`
   bodies and `reach.reachableDynamicRigidBodyIndices`, explicitly EXCLUDING
   every body already in `reach.orphanedDynamicRigidBodyIndices` "so Step D
   can never accidentally treat it as a valid chain member." That exclusion
   itself is correct, but Step D's own `orphanedDynamicBoneIndices` output was
   ONLY ever populated from Step D's own case-3 discoveries (a bone that IS
   graph-reachable but whose real skeleton ancestry alone doesn't reach an
   anchor) — Step D never iterates a bone that was excluded from its own
   input map to begin with, so a TRUE graph-orphan was never walked, never
   classified, and therefore never appeared in `orphanedDynamicBoneIndices`
   at all. The bone would simply vanish from the result entirely: not in any
   chain, not in the orphan diagnostic either — silently invisible, exactly
   the failure mode the user explicitly asked to avoid. This also directly
   contradicts Phase 3's own `DynamicBodyWithNoReachableStaticAnchorIsOrphanedNotSimulated`
   test (Step 3.3), which asserts precisely this scenario DOES appear in
   `diagnostics.orphanedDynamicBoneIndices` — the algorithm as originally
   specified could never have passed its own test. **Fixed entirely inside
   Phase 3 (v2):** Step C now seeds `orphanedDynamicBoneIndices` directly
   from `reach.orphanedDynamicRigidBodyIndices` (via each body's own
   `RigidBody::boneIndex`, skipping any with `boneIndex < 0` — there is no
   bone to flag/render for a rigid body that isn't attached to one at all)
   BEFORE Step D ever runs; Step D's own case-3 discoveries are UNIONED into
   that same list afterward (sorted, deduplicated), never replacing it. See
   `PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md`'s own Revision Notes
   (v2) for the full corrected Step C/D text and the new regression test.

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 5, strictly in order:

1. **Phase 1** generalizes the DATA MODEL and the two runtime consumers that
   hard-code the old flat-list assumption, entirely independent of how chains
   get detected: `DynamicChainDefinition` gains `parentJointIndex` (explicit
   tree-parent per joint) and `extraConstraints` (non-hierarchy stabilizing
   edges); `BoneChainPhysicsResolver.cpp` and `DynamicChainSolver.cpp` are
   rewritten to use `parentJointIndex` instead of `i - 1`, and the solver's
   structural-constraint loop additionally relaxes every `extraConstraints`
   entry each iteration. This phase is deliberately detection-algorithm-free —
   it must land, compile, and pass its own updated Tier-1 tests BEFORE Phase 2
   touches a single line of `DynamicChainDetection.cpp`, so that when the new
   detection algorithm lands in Phase 3 it has a already-correct runtime to
   target. **v2 note:** this phase's own underlying DESIGN needed no change —
   only its own illustrative code snippet for the resolver fix (originally
   self-contradictory: it showed a per-iteration `continue` guard, then
   immediately told the implementer to not actually write it that way) was
   corrected to show only the final, correct form, so an implementer cannot
   accidentally transcribe the wrong intermediate version.
2. **Phase 2** builds the pure, reusable RigidBody/Joint graph analysis
   Phase 3 needs: adjacency construction from `PhysicsData`, connected
   components, and "is this Dynamic body graph-reachable from a Static body"
   membership — order-independent and fully unit-tested in isolation, with
   zero `SkeletonData` dependency (this module only ever reasons about
   `RigidBody`/`Joint` indices, never bones).
3. **Phase 3** is the heart of the campaign: `DetectDynamicChains()`'s entire
   body is deleted and rewritten to (a) use Phase 2's graph purely to decide
   WHICH bones participate and WHERE the anchor(s) are, (b) walk the REAL
   skeleton bone-ancestor chain (never the raw joint graph) to build each
   chain's tree (`parentJointIndex`), guaranteeing Culprit A can never
   resurface, (c) fold every original PMX Joint whose two endpoints land in
   the SAME final chain, but that isn't already a tree edge, into an
   `ExtraStructuralConstraint`, and (d) collects every Dynamic/
   DynamicAndBoneMerge rigid body whose own ancestry never reaches an anchor
   — **and every rigid body Phase 2 already proved has NO anchor reachable
   at all (v2 fix for Culprit F)** — into a new orphan-diagnostic output. The
   old `Bone::deformAfterPhysics`-based implementation and its entire test
   file are deleted outright (per the user's explicit "fully replace" answer)
   and replaced with new tests covering linear rigs, spider-web skirts,
   multi-anchor rigs, orphans (both flavors, post-v2), a corrupted/cyclic
   skeleton, and determinism-under-shuffled-input.
4. **Phase 4** rewires the two real call sites (`PhysicsSystem.cpp`,
   `DynamicChainRigCache.h`) to the new signature/output shape, adds the
   defensive disjointness assertion (Culprit E), and updates every existing
   hand-built `DynamicChainDefinition` test fixture across the codebase
   (Phase 1 fallout: `DynamicChainDefinitionTests.cpp`,
   `BoneChainPhysicsResolverTests.cpp`, `DynamicChainSolverTests.cpp`,
   `DynamicChainRigCacheTests.cpp`, `PhysicsSystemParallelTests.cpp`) so the
   whole test suite compiles and passes again.
5. **Phase 5** updates the Editor's existing Verlet-mode visualization
   (`verlet-integration-5`'s `BoneViewerWindow.cpp`/`InspectorPanel.cpp`) so
   it draws the new tree/branch shape correctly (replacing the now-provably-
   wrong "straight line through array order" connector drawing), renders
   `extraConstraints` as a visually distinct "web brace" line style, and
   renders orphaned rigid bodies as a red, clearly-labeled marker — directly
   satisfying the user's own explicit ask for a way to "see what happen in
   the editor." **v2 addition:** a hover tooltip on the orphan marker states
   the actual reason ("no Static rigid body reachable" vs. "graph-reachable,
   but no real skeleton ancestor is a Static anchor"), and the two other
   diagnostic lists Phase 3 v2 now guarantees are never silently dropped
   (`crossChainJointsDropped`, `duplicateBoneRigidBodyAssignmentsDropped`)
   get a minimal, cheap textual line in the tree pane — closing the same
   visibility gap for these two as the original document already closed for
   orphans.

## Step 4: What We Will NOT Do (Focus)

- We will **not** keep the old `Bone::deformAfterPhysics`-based detection
  algorithm as a fallback path for models without PMX Joint data — the user
  explicitly chose full replacement. A model with rigid bodies but zero
  joints, or zero rigid bodies entirely, will detect zero chains under the
  new algorithm; this is an intentional, accepted behavior change, called out
  explicitly in Phase 3's own document so it is never mistaken for a bug.
- We will **not** touch `Bone::deformAfterPhysics` as a *data field* — it
  stays exactly as-is in `SkeletonData.h`, `PmxLoader.cpp`, `RigFile.cpp`,
  and the Inspector's "Deform After Physics" checkbox
  (`InspectorPanel.cpp` line 222); only `DynamicChainDetection.cpp` stops
  reading it for chain detection.
- We will **not** touch `VerletIntegration.cpp`, `WindField.cpp`,
  `FixedTimestepAccumulator.cpp`, `SphereCollider.cpp`, or
  `GlobalPhysicsSettings.h` at all — none of them make any per-joint
  hierarchy assumption; they operate on already-resolved particles/positions
  and are untouched by this campaign.
- We will **not** add cross-chain constraints (a Joint whose two endpoints
  land in two DIFFERENT final chains, e.g. two independently-anchored skirt
  panels braced to each other) — Phase 3 explicitly detects and drops these
  with a diagnostic note (never silently ignored, never crashes/asserts);
  making the runtime support constraints that reach across two entirely
  separate `DynamicChainRuntimeState` particle buffers is a genuinely
  separate, much larger follow-up (shared/merged runtime state across
  chains) and explicitly out of scope here.
- We will **not** add per-instance (per-entity) override support for the new
  `parentJointIndex`/`extraConstraints` data — this inherits the exact same,
  already-documented, already-accepted limitation the Inspector's "Dynamic
  Chain Physics" section has for every other per-joint field today (edits
  apply per model path, shared by every entity spawned from it).
- We will **not** implement genuine live-jiggle simulation preview inside the
  Bone Viewer (Phase 5 remains bind-pose-only, exactly like
  `verlet-integration-5`'s own explicit scope boundary) — only the STATIC
  bind-pose topology/tree-shape/orphan-flagging visualization changes.
- **(v2)** We will **not** differentiate `RigidBodyMotionType::Dynamic` from
  `RigidBodyMotionType::DynamicAndBoneMerge` anywhere in this campaign's own
  chain-membership/anchoring logic — both are treated identically as
  "simulated, participating" bodies (this matches the ORIGINAL algorithm's
  own treatment, which never distinguished them for its `mass`/`damping`
  borrowing either). A real "bone-merge leash" behavior (constraining the
  simulated particle back toward the animated bone position with its own
  independent strength, distinct from the existing `stiffness` goal
  constraint) would be a legitimate, separate future campaign — flagged here
  explicitly, in this section, specifically so it is never mistaken for an
  oversight this campaign silently papered over.
- **(v2)** We will **not** build a general N-way "average multiple children's
  corrective rotation at a shared hub bone" solution — Phase 3's own "Known
  Limitation" callout (carried over unmodified from the original draft, see
  that phase's own Step 3.5) remains the accepted behavior: a hub bone's last-
  processed child wins that frame's FK rotation. Confirmed still true and
  still acceptable during this v2 review; no change made.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact struct/function signatures to write,
the exact existing call sites to touch, and the exact test files to add,
rewrite, or extend. Do not skip a phase's own test file — every new pure/
Tier-1-testable piece of logic in this plan (Phase 1's resolver/solver
changes, Phase 2's graph module, Phase 3's whole new detection algorithm)
must land with its `tests/` counterpart in the same change, per this
codebase's own "Testability & Regression Safety" convention, which every
prior `verlet-integration-*` campaign already followed and this one inherits
unmodified. Do not reorder the phases — Phase 2 has no runtime to target
without Phase 1's generalized data model already compiling; Phase 3 cannot
build a tree-shaped `DynamicChainDefinition` without Phase 1's
`parentJointIndex`/`extraConstraints` fields existing, nor decide chain
membership without Phase 2's graph-reachability analysis; Phase 4 cannot
wire up a call site to a function whose new signature/output shape Phase 3
hasn't landed yet; Phase 5 cannot draw a tree/web/orphan gizmo for data that
doesn't exist until Phase 3 (chains) and Phase 4 (the cache's new orphan
field) both land.

Whenever this document's own root-cause analysis and a later phase's own
detailed section disagree on any exact line number or signature, trust the
later phase's own re-verification against the actual current source tree
first (this document is the ROADMAP; each child phase is re-checked against
the real files at the moment it is written/executed).

## Revision Notes (v2)

Full second-pass review method: every claimed file/line/signature in every
child phase document was re-read directly against the current source tree
(`src/Physics/*`, `src/Assets/PhysicsData.h`, `src/Game/Physics/*`,
`src/Editor/BoneViewerWindow.cpp`, `src/Editor/Panels/InspectorPanel.cpp`,
and every listed `tests/**` file) before any edit was made. Every line number
and code excerpt quoted anywhere in Phase 0/1/2/3/4/5 (v1) was confirmed
BYTE-ACCURATE against the real files — no stale references were found
anywhere in the original draft, which is why this v2 pass is a set of
targeted, surgical corrections rather than a rewrite.

Findings, in order of severity:

1. **(Correctness, Critical) Culprit F** — see Step 2 above for the full
   write-up. Fixed entirely inside `PHASE3_SKELETON_ALIGNED_CHAIN_DETECTION_REWRITE.md`'s
   own Step C/D and diagnostics struct, with a new dedicated regression test.
2. **(Robustness)** Phase 3's Step D skeleton-ancestor walk had no cycle
   guard — a corrupted/malformed `SkeletonData` with a `parentBoneIndex`
   cycle (e.g. bone A's parent is B, B's parent is A — never supposed to
   happen, but never a schema-enforced impossibility either) would hang the
   walk forever instead of degrading gracefully. Fixed in Phase 3 v2 with a
   per-walk visited-set guard (treat a revisit as an orphan, exactly like a
   dead end) plus a new regression test.
3. **(Robustness)** Phase 3's Step D only explicitly named `parentBoneIndex
   < 0` as the "no parent, stop" terminal condition; an out-of-range (`>=
   boneCount`) parent index — itself already a form of corrupted data — was
   not explicitly called out as an equally-valid terminal condition, risking
   an out-of-bounds `skeleton.bones[...]` access if an implementer only
   guarded the literal `< 0` case. Fixed in Phase 3 v2 by explicitly stating
   both conditions terminate the walk identically.
4. **(Robustness/clarity)** Phase 3's Step C duplicate-boneIndex tie-break
   rule ("keep the lowest rigid-body-index") did not explicitly exclude
   unattached (`boneIndex == -1`) rigid bodies before applying that rule,
   risking two logically-unrelated unattached bodies "colliding" on the same
   sentinel map key for no meaningful reason. Fixed in Phase 3 v2 by
   excluding `boneIndex < 0` bodies from the eligibility map entirely, up
   front, before any tie-break logic runs.
5. **(QoL/diagnostics parity)** A Static-vs-Dynamic collision on the same
   `boneIndex` (two different rigid bodies of DIFFERENT motion types
   attached to the same bone) silently decides, via Step C's tie-break,
   whether that bone becomes a chain ANCHOR or a chain MEMBER — a much more
   structurally significant outcome than the already-diagnosed
   same-motion-type duplicate case, yet it was invisible. Fixed in Phase 3 v2
   by adding a new `duplicateBoneRigidBodyAssignmentsDropped` diagnostic field
   (mirrors the already-existing `crossChainJointsDropped` field/convention
   exactly) and threading it through Phase 4/5 for free (Phase 4 needs no
   code change, Phase 5 gets one new cheap text line).
6. **(QoL, directly serves the user's own explicit ask)** Phase 5 (v1) drew
   an orphan marker but with no explanation of WHY a given bone is flagged,
   and never surfaced `crossChainJointsDropped` anywhere in the Editor at
   all, despite Phase 4 already threading that exact data through the cache
   — leaving it computed but completely invisible, the same "silently
   dropped information" failure mode the rest of this campaign explicitly
   set out to avoid. Fixed in Phase 5 v2 with a hover tooltip + two new,
   minimal, textual tree-pane lines (see that phase's own Revision Notes
   (v2)).
7. **(Illustrative-code correctness)** Phase 1 (v1)'s own `BoneChainPhysicsResolver.cpp`
   code excerpt for its resolver fix (Step 3.2) showed an intermediate,
   subtly-incorrect per-iteration `continue` guard (out-of-bounds for `i >=
   parentJointIndex.size()` whenever sizes mismatch), then immediately told
   the implementer, in prose, not to actually write it that way — a
   self-contradiction that risked an AI implementer transcribing the wrong
   version literally. Fixed in Phase 1 v2 by deleting the incorrect
   intermediate snippet and showing only the final, correct top-of-function
   guard + straight-line indexing.
8. **(Documentation clarity only, no code change)** Added one explicit "What
   We Will NOT Do" bullet (Step 4 above) calling out that
   `RigidBodyMotionType::DynamicAndBoneMerge` is deliberately treated
   identically to `Dynamic` throughout this whole campaign, so a future
   reader never mistakes the absence of special-casing for an oversight.

No change was made to Phase 2 (`RigidBodyJointGraph`) — its own contract
(`ReachabilityResult`'s two lists) already exposes exactly what Phase 3 v2
needs to fix Culprit F; the bug was entirely in how Phase 3 *consumed* that
already-correct output, never in Phase 2 itself.
