# PHASE5 — Completion Report: Editor Visualization — Tree Shape, Web Braces, and the Orphan Gizmo

Status: **DONE**. Branch: `feature/physics-from-scratch`. Scope executed per
`PHASE5_EDITOR_VISUALIZATION_TREE_WEB_AND_ORPHAN_GIZMO.md` (v2) — the LAST
phase of the `verlet-integration-6` campaign (`DetectDynamicChains()` now
builds chains by traversing the real RigidBody/Joint graph — see
`PHASE0_MASTER_STRATEGY.md`). This phase updates the existing Verlet-mode
Bone Viewer visualization (built by `verlet-integration-5`) so it correctly
reflects Phase 1's tree-shaped `DynamicChainDefinition` and Phase 3/4's new
orphan/cross-chain/duplicate-assignment diagnostics, instead of the
now-provably-wrong "straight line through array order" assumption those
files were originally written against.

## What changed

### `src/Editor/BoneViewerWindow.cpp`

- **3.1 — Connector-line drawing rewrite** (the per-chain overlay pass,
  Verlet mode branch). Replaced the old `prevPos`/`havePrev` straight-line
  accumulation (which silently assumed `jointBoneIndices`' own array order
  was already the tree order — true only for the old, non-branching
  algorithm) with:
  - An explicit, random-access, **parent-relative** lookup: for every joint
    `i`, a line is drawn from its own resolved parent position
    (`chain.rootBoneIndex` if `parentJointIndex[i] < 0`, otherwise
    `jointBoneIndices[parentJointIndex[i]]`'s own bind-pose position) to its
    own position — correctly draws a real branching tree instead of a false
    zig-zag line through unrelated branch tips.
  - A new loop over `chain.extraConstraints`, drawing each
    `ExtraStructuralConstraint` as a visually distinct "web brace" line
    (thinner, desaturated blue, `IM_COL32(120, 200, 255, 110)`, 1.0px) so a
    user can tell "what drives bone rotation" (the pink/magenta tree edges)
    apart from "what only stabilizes the simulation" at a glance.
  - The root/anchor marker block is otherwise unchanged (still drawn once
    per chain, independent of chain order).
- **3.2 — Orphaned rigid-body red marker with hover tooltip.** A new loop,
  drawn unconditionally alongside the chain overlay (not selection-gated,
  same convention as the head-collider wireframe), over
  `verletModel->diagnostics.orphanedDynamicBoneIndices`: each orphaned bone
  gets a red filled square with a white outline (`IM_COL32(220, 40, 40,
  255)` / `IM_COL32(255, 255, 255, 200)`), directly answering the user's
  explicit request ("flag it as non-physics simulated instances... make
  gizmo appear red... so i can see what happen in the editor"). Hovering the
  marker shows a tooltip naming the bone and explaining that it has no
  reachable Static anchor (either via the joint graph at all, or via its own
  real skeleton ancestry) and is therefore not physics-simulated. Orphaned
  bones are deliberately NOT added to `overlayParts` (no click/selection —
  they carry no `DynamicJointSettings` to inspect), matching the strategy
  document's own explicit scope decision.
- **3.3 — Tree-pane list rendering** (`RenderVerletChainNode()` and
  `BuildPartListPane()`'s Verlet-mode branch):
  - Each joint row in a chain's tree node now shows its own resolved tree
    parent as a text suffix — `"(root child)"` if `parentJointIndex[j] < 0`,
    otherwise `"(parent: <bone name>)"` — so the detected topology is
    legible even in the flat per-chain joint list.
  - A new, separate, non-selectable **"Orphaned (Not Simulated)"** tree
    section renders directly below the per-chain loop, each entry shown via
    `ImGui::TextColored` in a red tint (`ImVec4(0.95f, 0.35f, 0.35f, 1.0f)`).
  - `BuildPartListPane()`'s early-out ("(no dynamic bone chains)") now also
    checks all three diagnostic lists — a model with zero detected chains
    but a non-empty orphan/cross-chain/duplicate diagnostic list still shows
    those sections instead of being swallowed by the empty-state message.
- **3.5 (v2) — Surfaced `crossChainJointsDropped` and
  `duplicateBoneRigidBodyAssignmentsDropped`.** Both lists were already
  computed (Phase 3) and already threaded through the cache (Phase 4), but
  were completely invisible in the Editor until now — the same "silently
  dropped information" failure mode this whole campaign otherwise avoids.
  Two new sections in the tree pane, each entirely skipped when its list is
  empty (the overwhelmingly common case, so a well-formed rig's tree pane
  looks exactly as clean as before): "Cross-Chain Joints Dropped (N)" and
  "Duplicate Rigid-Body-Per-Bone Assignments Dropped (N)", each with a hover
  tooltip explaining what the diagnostic means and a per-entry
  `ImGui::TextDisabled` row naming the dropped joint/rigid body.

### `src/Editor/Panels/InspectorPanel.cpp`

- **3.4 — "Verlet Joint" single-selection section** (`case
  ModelPartKind::Verlet:`): immediately after the existing "Position In
  Chain" line, added a `"Tree Parent: %s"` line resolving the exact same way
  as 3.1/3.3 above (root bone name if `parentJointIndex[jointIndex] < 0`,
  otherwise the sibling joint bone's own name), plus a conditional
  `"Extra brace constraints: %zu"` `ImGui::TextDisabled` line whenever
  `chain.extraConstraints` contains any entry referencing this exact
  `jointIndexInChain` (checked against both `jointIndexA`/`jointIndexB`).
- **3.5 (v2) — "Dynamic Chain Physics" section**: directly below the
  existing `"%zu chain(s), %zu joint(s) total"` summary line, added one more
  conditional `ImGui::TextColored` line (only shown when either diagnostic
  list is non-empty) pointing a user who only opens the Inspector (not the
  Bone Viewer) toward the Bone Viewer's Verlet tree pane for the full
  per-entry detail, rather than duplicating the full listing in two panels.

## Manual regression checklist (per the strategy document's own 3.6 — not
independently exercised this session, since it requires a live Editor
session with real orphan/cross-chain/duplicate model content; the checklist
itself is unchanged from the strategy document and remains the reference for
a future interactive verification pass):

- A simple single-strand linear rig should render an unchanged straight
  connector line and show no "Orphaned"/"Cross-Chain"/"Duplicate" sections.
- A spider-web skirt fixture should render one single branching tree with
  visibly thinner/differently-colored cross-brace lines, no false straight
  zig-zag line.
- A deliberately-orphaned rigid body should render the red marker at the
  correct bind-pose position, non-selectable, with the explanatory tooltip
  on hover.
- A deliberately cross-chain/duplicate-assignment fixture should surface
  both in the Bone Viewer's tree pane AND the Inspector's "Dynamic Chain
  Physics" section, with both sections absent for any model where the
  corresponding list is empty.

## Compile check

```
cmake --build build --target gte_core
```
completed with **zero errors** — only the two files actually touched this
phase needed to recompile (`src/Editor/Panels/InspectorPanel.cpp`,
`src/Editor/BoneViewerWindow.cpp`), followed by linking `libgte_core.a`
successfully.

As an extra sanity check (not required by this phase, but cheap and
confirms nothing else in the dependency graph broke):

```
cmake --build build --target GreatTamanaEngine
```
also completed with **zero errors**, linking `GreatTamanaEngine.exe` cleanly
and staging every shader as usual.

Per this campaign's own "No Full Build" rule, no full regression test suite
(`ctest`) was run this session — only the fast compile checks above, as this
is not explicitly called out as the final/last phase requiring one.

## Campaign status

This was the FIFTH and final phase of `PHASE0_MASTER_STRATEGY.md`'s plan
(Phase 1 → Phase 5, all now DONE). `DetectDynamicChains()` builds chains by
traversing the real RigidBody/Joint graph (Phase 2/3), the runtime/cache
correctly carries the new tree-shaped `DynamicChainDefinition` and
diagnostics end-to-end (Phase 1/4), and the Editor now visually reflects
every part of that new data model — tree shape, web braces, and orphaned
rigid bodies — closing out the user's own original request in full.
