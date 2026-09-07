# PHASE5 — Editor Visualization: Tree Shape, Web Braces, and the Orphan Gizmo (v2)

Part of the `verlet-integration-6` campaign — read `PHASE0_MASTER_STRATEGY.md`
through `PHASE4_RUNTIME_INTEGRATION_AND_CACHE_UPDATES.md` first; all four
must already compile and pass their own tests before starting this phase.
This phase updates the EXISTING Verlet-mode Bone Viewer visualization built
by `verlet-integration-5` (`src/Editor/BoneViewerWindow.cpp`,
`src/Editor/Panels/InspectorPanel.cpp`) so it correctly reflects Phase 1's
tree-shaped `DynamicChainDefinition` and Phase 3/4's new orphan diagnostics,
instead of the now-provably-wrong "straight line through array order"
assumption those files were written against.

**v2 note:** sections 3.1-3.4 below are unchanged from v1 (independently
re-verified against the current source tree during this revision and found
fully accurate — every quoted line number/excerpt still matches the real
file). This revision ADDS one new section, 3.5, closing a QoL gap the v1
review found: `crossChainJointsDropped` and (after Phase 3 v2)
`duplicateBoneRigidBodyAssignmentsDropped` were already computed and already
threaded all the way through the cache (Phase 4), yet completely invisible
anywhere in the Editor — the exact "silently dropped information" failure
mode this whole campaign otherwise takes pains to avoid. 3.2's orphan marker
also gains a hover tooltip (still additive, same section). See
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", findings #5/#6, for
the full rationale.

## Step 1: The Goal (Where are we going?)

The Bone Viewer's Verlet mode gizmo draws exactly what the new detection
algorithm actually produces: a real branching tree (not a false straight
line), the spider-web skirt's cross-bracing `extraConstraints` in a visually
distinct style, and every orphaned (non-simulated) Dynamic rigid body as a
clearly red, clearly labeled marker — directly satisfying the user's own
explicit request ("make gizmo appear red, or anthing so i can see what
happen in the editor"). **(v2)** The two remaining diagnostic lists Phase 3
computes but never previously surfaced anywhere in the Editor
(`crossChainJointsDropped`, `duplicateBoneRigidBodyAssignmentsDropped`) also
become visible, at minimum as plain text, so a content author debugging an
unexpectedly-shaped rig has a complete picture of every decision the
detector made, not just the orphan case.

## Step 2: The Situation / The Problem (Where are we now?)

Three spots in `src/Editor/BoneViewerWindow.cpp` hard-code the old flat-list
assumption, confirmed by direct inspection of the current file:

1. **Lines 1372-1386 (the connector-line drawing block, inside the Verlet
   overlay-rendering pass)** — accumulates `prevPos`/`havePrev` by iterating
   `chain.jointBoneIndices` IN ARRAY ORDER and drawing a line from whatever
   the PREVIOUS array entry's position was to the current one:
   ```cpp
   for (const std::int32_t boneIndex : chain.jointBoneIndices) {
       // ...
       if (havePrev) { drawList->AddLine(prevScreen, jointScreen, ...); }
       prevPos = jointPos;
       havePrev = true;
   }
   ```
   This is EXACTLY the "straight line through array order" bug this
   campaign's own `PHASE0_MASTER_STRATEGY.md` (Step 1) calls out as
   provably wrong the moment `jointBoneIndices` contains a real branch (a
   spider-web skirt's hub will draw one long, visually nonsensical zig-zag
   line touching every joint in `jointBoneIndices` order instead of the
   actual tree shape).
2. **Lines 1158-1170 (Verlet mode's own `overlayParts` construction)** — only
   ever iterates `chain.jointBoneIndices`; has no path today for rendering
   anything that isn't a simulated joint (i.e. no path for an orphaned rigid
   body at all).
3. **Lines 686-727 (`RenderVerletChainNode()`, the tree-pane list rendering)**
   — iterates `chain.jointBoneIndices` as a flat list per chain header; has
   no indentation/parent-relationship display and no "orphaned" section at
   all, and (v2) has no way to surface `crossChainJointsDropped`/
   `duplicateBoneRigidBodyAssignmentsDropped` either, even though both are
   already sitting in `verletModel->diagnostics` by the time this function
   runs (Phase 4 already threads them through the cache — this is purely an
   Editor-side visibility gap, not a missing data problem).

`src/Editor/Panels/InspectorPanel.cpp`'s own "Dynamic Chain Physics"/"Verlet
Joint" sections (confirmed at lines 363-410 and 550-575) display per-joint
text (`"Chain: %d (%zu joints)"`, `"Position In Chain: %d of %zu"`) that
implicitly assumes a flat, linear position-in-chain concept — this remains
harmless/still-accurate as "this joint's own array position" but should be
augmented to also show its actual tree PARENT so a user inspecting a
branchy chain understands what's actually driving it.

## Step 3: The Plan

### 3.1 Fix the connector-line drawing (`BoneViewerWindow.cpp`, ~lines
1340-1400, the whole per-chain overlay block)

Replace the `prevPos`/`havePrev` accumulation entirely with an explicit,
random-access, parent-relative lookup, since the tree shape means "the
previous array entry" is no longer meaningfully related to "this entry's
actual parent":

```cpp
// Root/anchor marker - UNCHANGED (still chain.rootBoneIndex, still drawn once per chain).

// Tree edges: for EVERY joint i, draw a line from ITS OWN resolved parent
// position (rootBoneIndex if parentJointIndex[i] < 0, otherwise
// jointBoneIndices[parentJointIndex[i]]'s own bind-pose position) to its
// own position - task_manager/verlet-integration-6, Phase 5. Replaces the
// old prevPos/havePrev straight-line accumulation, which assumed
// jointBoneIndices' own array order was already the tree order (true only
// for the OLD, non-branching algorithm).
for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
    const std::int32_t boneIndex = chain.jointBoneIndices[i];
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
        continue;
    }
    const std::int32_t parentJoint = (i < chain.parentJointIndex.size()) ? chain.parentJointIndex[i] : -1;
    const bool parentIsRoot = parentJoint < 0;
    const std::int32_t parentBoneIndex = parentIsRoot
        ? chain.rootBoneIndex
        : ((static_cast<std::size_t>(parentJoint) < chain.jointBoneIndices.size())
              ? chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)]
              : -1);
    if (parentBoneIndex < 0 || static_cast<std::size_t>(parentBoneIndex) >= m_bones.size()) {
        continue;
    }
    const Vec3 parentPos = m_bones[static_cast<std::size_t>(parentBoneIndex)].position;
    const Vec3 jointPos = m_bones[static_cast<std::size_t>(boneIndex)].position;
    ImVec2 parentScreen, jointScreen;
    if (ProjectToScreen(parentPos, viewProj, imageMin, imageMax, parentScreen)
        && ProjectToScreen(jointPos, viewProj, imageMin, imageMax, jointScreen)) {
        drawList->AddLine(parentScreen, jointScreen, IM_COL32(255, 90, 170, 160), 2.0f);
    }
}

// Extra structural ("web brace") constraints - task_manager/
// verlet-integration-6, Phase 5 - visually distinct (thinner, dashed-look
// via a lighter/desaturated color and reduced thickness) from the primary
// tree edges above, so a user can tell "what drives bone rotation" apart
// from "what only stabilizes the simulation" at a glance - directly
// reflects PHASE1/PHASE3's own extraConstraints concept.
for (const ExtraStructuralConstraint& extra : chain.extraConstraints) {
    if (extra.jointIndexA < 0 || extra.jointIndexB < 0
        || static_cast<std::size_t>(extra.jointIndexA) >= chain.jointBoneIndices.size()
        || static_cast<std::size_t>(extra.jointIndexB) >= chain.jointBoneIndices.size()) {
        continue;
    }
    const std::int32_t boneA = chain.jointBoneIndices[static_cast<std::size_t>(extra.jointIndexA)];
    const std::int32_t boneB = chain.jointBoneIndices[static_cast<std::size_t>(extra.jointIndexB)];
    if (boneA < 0 || boneB < 0 || static_cast<std::size_t>(boneA) >= m_bones.size()
        || static_cast<std::size_t>(boneB) >= m_bones.size()) {
        continue;
    }
    ImVec2 screenA, screenB;
    if (ProjectToScreen(m_bones[static_cast<std::size_t>(boneA)].position, viewProj, imageMin, imageMax, screenA)
        && ProjectToScreen(m_bones[static_cast<std::size_t>(boneB)].position, viewProj, imageMin, imageMax, screenB)) {
        drawList->AddLine(screenA, screenB, IM_COL32(120, 200, 255, 110), 1.0f);
    }
}
```

Leave the root/anchor marker block (immediately above this, drawing the
small filled rect at `chain.rootBoneIndex`'s position) exactly as-is — it
does not depend on chain order at all. Leave the head-collider wireframe
block (immediately below this) exactly as-is for the same reason.

### 3.2 Render orphaned rigid bodies as a red marker, with a hover tooltip (v2)

In the SAME per-chain overlay pass (Verlet mode branch), after iterating
`verletModel->chains`, add one more loop over
`verletModel->diagnostics.orphanedDynamicBoneIndices` (Phase 4's new field):

```cpp
// task_manager/verlet-integration-6, Phase 5 - orphaned (non-simulated)
// rigid-body bones, per the user's own explicit request: "flag it as
// non-physics simulated instances... make gizmo appear red... so i can see
// what happen in the editor." Drawn UNCONDITIONALLY alongside the chain
// overlay (not selection-gated), same convention as the head-collider
// wireframe.
for (const std::int32_t boneIndex : verletModel->diagnostics.orphanedDynamicBoneIndices) {
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
        continue;
    }
    ImVec2 screen;
    if (ProjectToScreen(m_bones[static_cast<std::size_t>(boneIndex)].position, viewProj, imageMin, imageMax, screen)) {
        constexpr float kHalf = 5.0f;
        drawList->AddRectFilled(ImVec2(screen.x - kHalf, screen.y - kHalf), ImVec2(screen.x + kHalf, screen.y + kHalf),
            IM_COL32(220, 40, 40, 255));
        drawList->AddRect(ImVec2(screen.x - kHalf - 1, screen.y - kHalf - 1), ImVec2(screen.x + kHalf + 1, screen.y + kHalf + 1),
            IM_COL32(255, 255, 255, 200));

        // task_manager/verlet-integration-6, Phase 5 (v2) - a plain hover
        // tooltip explaining WHY this bone is flagged, not just THAT it is -
        // reuses this file's own existing ImGui::IsMouseHoveringRect()
        // idiom (already used elsewhere in this same function for the
        // regular overlayParts hover-name-reveal) rather than introducing a
        // new hover-detection mechanism. A precise "which of the two
        // Culprit-F sources flagged this bone" distinction is deliberately
        // NOT attempted here (both sources land in the very same
        // orphanedDynamicBoneIndices list by Phase 3's own design, see
        // PHASE3's Step H) - one clear, correct, general reason string is
        // more useful to a content author than a technically-precise but
        // confusing internal distinction.
        const ImVec2 hoverMin(screen.x - kHalf - 1, screen.y - kHalf - 1);
        const ImVec2 hoverMax(screen.x + kHalf + 1, screen.y + kHalf + 1);
        if (ImGui::IsMouseHoveringRect(hoverMin, hoverMax)) {
            const char* boneName = (static_cast<std::size_t>(boneIndex) < m_bones.size())
                ? m_bones[static_cast<std::size_t>(boneIndex)].name.c_str()
                : "(unknown)";
            ImGui::SetTooltip(
                "Orphaned: %s\nNo Static rigid body is reachable from this bone, either via the "
                "Joint graph at all, or via this bone's own real skeleton ancestry.\nNot physics-"
                "simulated - the animated (FK) pose is used unchanged.",
                boneName);
        }
    }
}
```

Orphaned bones are deliberately NOT added to `overlayParts` (Verlet mode's
hit-testable/selectable list, lines 1158-1170) in this phase — they carry no
`DynamicJointSettings` to inspect (they are, by definition, not part of any
`DynamicChainDefinition`), so making them clickable/selectable would need a
brand-new Inspector code path with nothing meaningful to show. This phase
draws them as a purely informational marker, made discoverable via the
hover tooltip above (v2) but still non-interactive/non-selectable — a
future campaign may add a dedicated read-only Inspector panel for an
orphaned bone if that turns out to be needed in practice.

### 3.3 Tree-pane list rendering (`RenderVerletChainNode()`, lines 686-727)

Two additive changes:

1. Show each joint row's own resolved parent alongside its name, so the tree
   shape is legible even in the flat list (full nested-indentation-per-
   branch is a nice-to-have, not required — a single extra text suffix is
   enough to satisfy this phase's own goal of "the user can tell what the
   detected topology actually is"):
   ```cpp
   for (std::size_t j = 0; j < chain.jointBoneIndices.size(); ++j) {
       const std::int32_t boneIndex = chain.jointBoneIndices[j];
       // ... existing bounds check ...
       const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
       const std::int32_t parentJoint = (j < chain.parentJointIndex.size()) ? chain.parentJointIndex[j] : -1;
       std::string label = bone.name;
       if (parentJoint < 0) {
           label += " (root child)";
       } else if (static_cast<std::size_t>(parentJoint) < chain.jointBoneIndices.size()) {
           const std::int32_t parentBoneIndex = chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
           if (parentBoneIndex >= 0 && static_cast<std::size_t>(parentBoneIndex) < m_bones.size()) {
               label += " (parent: " + m_bones[static_cast<std::size_t>(parentBoneIndex)].name + ")";
           }
       }
       RenderFlatPartRow(ModelPartKind::Verlet, boneIndex, label, bone.position, lowerFilter, ctx);
   }
   ```
2. Add a new, separate, non-selectable "Orphaned (Not Simulated)" tree
   section directly below the per-chain loop in `BuildPartListPane()`'s
   Verlet-mode branch, iterating `verletModel->diagnostics.
   orphanedDynamicBoneIndices` and rendering each with `ImGui::TextColored`
   in a red tint (e.g. `ImVec4(0.95f, 0.35f, 0.35f, 1.0f)`) plus its bone
   name — mirrors this same file's own established "informational, non-
   selectable list row" convention (compare to how the head-collider radius
   line is already rendered via `ImGui::TextDisabled` at line 722, just
   colored red instead of the disabled-gray to convey "problem," not merely
   "extra info").

### 3.4 `Panels/InspectorPanel.cpp` — show the tree parent in the "Verlet
Joint" single-selection section

At the existing `case ModelPartKind::Verlet:` block (added by
`verlet-integration-5`'s own Phase 3, confirmed around lines 385-410),
immediately after the existing `"Position In Chain: %d of %zu"` line, add
one more line showing the resolved parent's name (root bone name if
`parentJointIndex[jointIndexInChain] < 0`, otherwise the sibling joint
bone's own name) using the exact same resolution logic as 3.1/3.3 above —
and, if `chain.extraConstraints` contains any entry referencing this exact
`jointIndexInChain`, one additional `ImGui::TextDisabled` line, e.g.
`"Extra brace constraints: %zu"`, so a user inspecting one particle also
learns it participates in the skirt's web bracing, not just its own tree
edge.

### 3.5 (v2, new) Surface `crossChainJointsDropped` and
`duplicateBoneRigidBodyAssignmentsDropped` — the two diagnostics that were
already computed and already cached, but completely invisible in the Editor

Both lists are cheap (typically empty, and even in a pathological rig never
more than a handful of entries), already sitting in
`verletModel->diagnostics` by the time either `BoneViewerWindow.cpp` or
`InspectorPanel.cpp` runs (Phase 4 already threads the whole
`DynamicChainDetectionDiagnostics` struct through the cache) — this section
is pure Editor-side plumbing, zero new engine/runtime code.

1. **Tree pane (`BuildPartListPane()`'s Verlet-mode branch,
   `BoneViewerWindow.cpp`)** — directly below the "Orphaned (Not Simulated)"
   section added in 3.3, add two more short, non-selectable sections, each a
   single `ImGui::TextColored`/`ImGui::TreeNode` summary line followed by one
   row per entry (skip the whole section entirely when its list is empty —
   the overwhelmingly common case — so a well-formed rig's Verlet tree pane
   looks exactly as clean as it did before this section existed):
   ```cpp
   if (!verletModel->diagnostics.crossChainJointsDropped.empty()) {
       ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
           "Cross-Chain Joints Dropped (%zu)", verletModel->diagnostics.crossChainJointsDropped.size());
       if (ImGui::IsItemHovered()) {
           ImGui::SetTooltip(
               "These PMX Joints connect two bones that ended up in two DIFFERENT detected chains "
               "(e.g. two independently-anchored skirt panels laced together). Not simulated as a "
               "constraint - cross-chain bracing is out of scope for this engine today.");
       }
       for (const std::int32_t jointIndex : verletModel->diagnostics.crossChainJointsDropped) {
           const std::string label = (jointIndex >= 0 && static_cast<std::size_t>(jointIndex) < m_joints.size())
               ? m_joints[static_cast<std::size_t>(jointIndex)].name
               : std::string("(unknown)");
           ImGui::TextDisabled("  Joint %d: %s", jointIndex, label.c_str());
       }
   }
   if (!verletModel->diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty()) {
       ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
           "Duplicate Rigid-Body-Per-Bone Assignments Dropped (%zu)",
           verletModel->diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size());
       if (ImGui::IsItemHovered()) {
           ImGui::SetTooltip(
               "Two or more rigid bodies are attached to the same bone. Only the lowest-index one is "
               "used for chain-detection purposes (as either an anchor or a member); the rest are "
               "listed here so an unexpected anchor/member choice can be traced back to its cause.");
       }
       for (const std::int32_t rigidBodyIndex : verletModel->diagnostics.duplicateBoneRigidBodyAssignmentsDropped) {
           const std::string label = (rigidBodyIndex >= 0 && static_cast<std::size_t>(rigidBodyIndex) < m_rigidBodies.size())
               ? m_rigidBodies[static_cast<std::size_t>(rigidBodyIndex)].name
               : std::string("(unknown)");
           ImGui::TextDisabled("  Rigid Body %d: %s", rigidBodyIndex, label.c_str());
       }
   }
   ```
2. **Inspector "Dynamic Chain Physics" section
   (`Panels/InspectorPanel.cpp`, confirmed around lines 552-575)** — directly
   below the existing `"%zu chain(s), %zu joint(s) total"` summary line, add
   one more conditional line, only shown when either list is non-empty:
   `ImGui::TextColored(warningColor, "%zu cross-chain joint(s) and %zu duplicate rigid-body assignment(s) were dropped during detection - see the Bone Viewer's Verlet tree pane for details.", model->diagnostics.crossChainJointsDropped.size(), model->diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size());`
   — a single, cheap pointer for a user who only ever opens the Inspector
   (not the Bone Viewer) toward where the full detail lives, rather than
   duplicating the full per-entry listing in two different panels.

### 3.6 Manual regression checklist (code-adjacent verification only — no
new production code beyond 3.1-3.5, listed here so this phase's own "done"
definition is unambiguous)

- Load a model whose PMX data is a simple single-strand tail/ponytail
  (linear rig, one Static anchor): Verlet mode's connector lines look
  IDENTICAL to how they looked before this campaign (a single straight
  line, root to tip) — proof 3.1's rewrite is behavior-preserving for the
  simple case, and the tree pane shows no "Orphaned"/"Cross-Chain"/
  "Duplicate" sections at all (all three lists empty).
- Load a model with a spider-web skirt: Verlet mode shows one single
  branching tree (matching Phase 3's own `SpiderWebSkirtWithFourBranches...`
  test fixture shape) with visibly thinner/different-colored cross-brace
  lines connecting sibling strands, and NO false straight zig-zag line
  connecting unrelated branch tips.
- If any test fixture/model is deliberately constructed with an orphaned
  rigid body (or one is found in real content), confirm the red marker
  renders at the correct bind-pose position, is clearly visually distinct
  from a normal (pink/magenta) particle dot, does NOT respond to
  click/hover for SELECTION purposes, and (v2) DOES show the explanatory
  tooltip on hover.
- (v2) If a test fixture/model is deliberately constructed with a
  cross-chain joint or a duplicate bone/rigid-body assignment, confirm both
  the Bone Viewer's tree pane and the Inspector's "Dynamic Chain Physics"
  section surface it, and confirm BOTH sections stay entirely absent for any
  model where the corresponding list is empty (no visual noise for the
  common case).

## Revision Notes (v2)

Sections 3.1-3.4 are unchanged from v1 (re-verified line-accurate against
the current source tree). This revision adds:

1. A hover tooltip on the orphan marker (3.2) explaining WHY a bone is
   flagged, not just that it is — closes part of the visibility gap noted in
   `PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)", finding #6.
2. A brand-new section 3.5 surfacing `crossChainJointsDropped` and (Phase 3
   v2's new) `duplicateBoneRigidBodyAssignmentsDropped` in both the Bone
   Viewer's tree pane and the Inspector — data that Phase 4 already threads
   through the cache but that v1 of this document never displayed anywhere,
   the same "silently dropped information" failure mode the rest of this
   campaign otherwise avoids by design. Both additions are deliberately
   cheap, empty-by-default, and never shown at all for a well-formed rig, so
   they add zero visual noise to the overwhelmingly common case.
3. The manual regression checklist (3.6, was 3.5 in v1) gained two new
   bullet points covering the above.
