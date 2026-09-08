# PHASE5 — Editor Inspector UI Update (+ Bone Viewer Overlay, v2)

**v2 — SCOPE EXPANDED.** v1 of this document only covered
`src/Editor/Panels/InspectorPanel.cpp`. Re-auditing the real codebase for
this campaign's second iteration found that `src/Editor/BoneViewerWindow.cpp`
ALSO directly references the fields PHASE2 deletes, at TWO separate
locations, and was never assigned to any phase in v1 — meaning the Editor
target would have failed to compile after PHASE2 with no phase responsible
for the fix. This is now Step 3.3/3.4 below (new in v2). See
`PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)", finding #1, for the
full background. Steps 3.1/3.2 (the original `InspectorPanel.cpp` fix) are
unchanged from v1 and were re-verified correct.

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2, PHASE3, PHASE4
Followed by: `PHASE6_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md`

---

## Step 1 — The Goal

Three call sites across two Editor files directly reference the now-deleted
`DynamicChainDefinition::hasHeadCollider` / `headColliderBoneIndex` /
`headColliderRadius` fields — after PHASE2 all three fail to compile. Fix
all of them:

1. `src/Editor/Panels/InspectorPanel.cpp` — TWO call sites (the
   `ModelPartInspector`'s Verlet case, and the per-chain "Dynamic Chain
   Physics" section) — replace the old "hand-pick a bone index + guess a
   radius" widgets with the new, much simpler single `collisionEnabled`
   checkbox, plus a small, honest, read-only summary of how many real
   colliders were auto-detected for this model.
2. `src/Editor/BoneViewerWindow.cpp` — TWO more call sites (a text row in
   the Verlet tree pane, and the actual 3D-viewport head-collider
   wireframe) — replace the per-chain single-sphere visualization with a
   per-MODEL, all-shapes-drawn-once wireframe overlay of every genuinely
   auto-detected Static collider, reusing the exact same
   `BuildRigidBodyWireframe()` call this same file already makes elsewhere
   for its "Rigid Body" view mode (so the fix is a small, low-risk,
   pattern-matching change, not new geometry code).

## Step 2 — The Situation

### 2.1 — `InspectorPanel.cpp` (unchanged from v1)

Two exact call sites (both already located and read in full during this
campaign's research):

1. `BuildModelPartInspector()`'s `case ModelPartKind::Verlet:` branch
   (shown when a user clicks a physics-simulated bone in the Bone Viewer's
   Verlet tree pane) — near its end:

```cpp
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Chain-Wide Settings");
        ImGui::DragFloat("Gravity Scale", &chain.gravityScale, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Wind Scale", &chain.windScale, 0.01f, 0.0f, 10.0f);
        ImGui::Checkbox("Head Collider", &chain.hasHeadCollider);
        if (chain.hasHeadCollider) {
            ImGui::DragInt("Collider Bone Index", &chain.headColliderBoneIndex, 1.0f, 0,
                static_cast<int>(rig->skeleton.bones.size()) - 1);
            ImGui::DragFloat("Collider Radius", &chain.headColliderRadius, 0.01f, 0.0f, 10.0f);
        }
        break;
```

   In this same function/branch, `model` (a
   `DynamicChainRigCache::ModelEntry*`) is already in scope a few lines
   above (`DynamicChainRigCache::ModelEntry* model =
   physicsSystem.GetDynamicChainRigCache().TryGetMutable(source->gtaPath);`),
   so `model->colliders.size()` (PHASE3's new field) is directly available
   here with no new lookup needed.

2. `BuildEntityInspector()`'s `"Dynamic Chain Physics"` `CollapsingHeader`,
   inside the per-chain `TreeNode` loop:

```cpp
                        // PHASE5 (task_manager/verlet-integration-1/
                        // PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md,
                        // Step 5 item 2) - simple head/body collision. Disabled
                        // by default even though DynamicChainDetection.h
                        // pre-fills a reasonable starting bone/radius - a
                        // human opts in here.
                        ImGui::Separator();
                        ImGui::Checkbox("Head Collider", &chain.hasHeadCollider);
                        if (chain.hasHeadCollider) {
                            ImGui::DragInt("Collider Bone Index", &chain.headColliderBoneIndex, 1.0f, 0,
                                static_cast<int>(model->skeleton.bones.size()) - 1);
                            ImGui::DragFloat("Collider Radius", &chain.headColliderRadius, 0.01f, 0.0f, 10.0f);
                        }
```

   Here too, `model` (`DynamicChainRigCache::ModelEntry*`) is already in
   scope in the enclosing block.

`RigidBodyShapeLabel(RigidBodyShape)` (near the top of this same file,
anonymous namespace) already exists and is reusable for a shape-breakdown
summary if desired (optional nice-to-have, not required for correctness).

### 2.2 — `BoneViewerWindow.cpp`/`.h` (v2, new)

Confirmed by direct inspection of the CURRENT file (not hypothetical —
these are today's real line numbers):

1. `BoneViewerWindow::RenderVerletChainNode(std::int32_t chainIndex, const
   DynamicChainDefinition& chain, const std::string& lowerFilter,
   EditorContext& ctx)` (a per-chain row in the Bone Viewer's flat Verlet
   tree pane) ends with:

```cpp
        if (chain.hasHeadCollider) {
            ImGui::TextDisabled("Head Collider: r=%.3f", chain.headColliderRadius);
        }
        ImGui::TreePop();
```

   This function receives `chain` by value/reference directly (no `model`
   pointer in scope) — the fix only needs `chain.collisionEnabled`, which
   is already available with zero new parameters.

2. Inside the 3D-viewport overlay-drawing function, in the
   `m_viewMode == ModelPartKind::Verlet` branch, INSIDE the
   `for (const DynamicChainDefinition& chain : verletModel->chains)` loop
   (`verletModel` — a `const DynamicChainRigCache::ModelEntry*` — IS in
   scope here, confirmed by its use two lines later for the orphaned-bone
   diagnostic overlay that immediately follows this same loop):

```cpp
                        // Optional head-collider wireframe (Sphere shape -
                        // reusing RigidBodyWireframe.h's own existing
                        // per-shape geometry builder exactly like Rigid Body
                        // mode's selected-shape wireframe does) - drawn
                        // unconditionally whenever configured, NOT
                        // selection-gated (it is a debug aid for the whole
                        // chain, not itself a selectable part).
                        if (chain.hasHeadCollider && chain.headColliderBoneIndex >= 0
                            && static_cast<std::size_t>(chain.headColliderBoneIndex) < m_bones.size()
                            && chain.headColliderRadius > 0.0f) {
                            const Vec3 colliderCenter = m_bones[static_cast<std::size_t>(chain.headColliderBoneIndex)].position;
                            const std::vector<WireframeSegment> wireframe = BuildRigidBodyWireframe(
                                RigidBodyShape::Sphere, Vec3(chain.headColliderRadius, 0.0f, 0.0f), colliderCenter, Vec3::Zero());
                            for (const WireframeSegment& segment : wireframe) {
                                ImVec2 screenA, screenB;
                                if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                                    && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                                    drawList->AddLine(screenA, screenB, IM_COL32(255, 90, 170, 90), 1.25f);
                                }
                            }
                        }
```

   This block is drawn ONE PER CHAIN, once per chain iterated by the
   enclosing loop — which was always slightly wrong even for the OLD
   single-sphere feature whenever a model had more than one
   `collisionEnabled` chain sharing the same head collider (the exact same
   wireframe would be drawn multiple times, harmlessly but redundantly).
   With PHASE2/PHASE3's new MODEL-WIDE collider list, the natural, MORE
   correct fix is to draw the collider overlay **once per model**, not once
   per chain — see Step 3.4 below.

`RigidBodyEntry` (`BoneViewerWindow.h`, this window's own flattened,
overlay-only copy of `PhysicsData::RigidBody`) currently stores `name`,
`translate`, `rotateRadians`, `shape`, `shapeSize`, `boneIndex`, `group` —
confirmed by direct inspection — but **not** `motionType`. Since
`translate`/`rotateRadians` are already, by `PhysicsData.h`'s own
documented contract, an **absolute model-space bind-pose transform** (the
exact same space as `Bone::position`), this window's EXISTING "Rigid Body"
view mode already draws every body's real wireframe directly from
`m_rigidBodies[i].translate/rotateRadians` with **no bone-tracking at all**
(confirmed at this file's own current line ~1609:
`BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate,
body.rotateRadians)`). This means the model-wide collider overlay this
phase adds can reuse that exact same zero-bone-tracking pattern — it only
needs one new filter criterion (`motionType == RigidBodyMotionType::Static`,
mirroring `ModelColliderDetection.cpp`'s own eligibility rule) that
`RigidBodyEntry` does not yet expose.

## Step 3 — The Plan

### 3.1 — Fix call site 1 (`BuildModelPartInspector`'s Verlet case) — unchanged from v1

Replace the 6-line block quoted in 2.1.1 above with:

```cpp
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Chain-Wide Settings");
        ImGui::DragFloat("Gravity Scale", &chain.gravityScale, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Wind Scale", &chain.windScale, 0.01f, 0.0f, 10.0f);
        // task_manager/verlet-integration-9, PHASE5 - replaces the old
        // "Head Collider" single-sphere bone-index/radius pair entirely.
        // Collision now automatically targets EVERY Static rigid body
        // (Sphere/Box/Capsule) the model's own PMX data describes (see
        // Physics/ModelColliderDetection.h) - there is nothing left to
        // hand-author beyond this one opt-in checkbox.
        ImGui::Checkbox("Enable Collision", &chain.collisionEnabled);
        if (chain.collisionEnabled) {
            ImGui::TextDisabled(
                "Collides against every auto-detected Static rigid body for this model (%zu total).",
                model->colliders.size());
            if (model->colliders.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "This model has no detected Static rigid-body colliders - enabling this has no effect.");
            }
        }
        break;
```

### 3.2 — Fix call site 2 (`BuildEntityInspector`'s "Dynamic Chain Physics" per-chain section) — unchanged from v1

Replace the block quoted in 2.1.2 above with:

```cpp
                        // task_manager/verlet-integration-9, PHASE5 -
                        // replaces the old "Head Collider" single-sphere
                        // bone-index/radius pair entirely - see
                        // Physics/DynamicChainDefinition.h's own
                        // `collisionEnabled` doc comment.
                        ImGui::Separator();
                        ImGui::Checkbox("Enable Collision", &chain.collisionEnabled);
                        if (chain.collisionEnabled) {
                            ImGui::TextDisabled(
                                "Collides against every auto-detected Static rigid body for this model (%zu total).",
                                model->colliders.size());
                            if (model->colliders.empty()) {
                                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                                    "This model has no detected Static rigid-body colliders - enabling this has no effect.");
                            }
                        }
```

### 3.3 — (v2, new, REQUIRED) Fix `BoneViewerWindow.cpp`'s Verlet tree-row text

Replace the 3-line block quoted in 2.2.1 above:

```cpp
        if (chain.hasHeadCollider) {
            ImGui::TextDisabled("Head Collider: r=%.3f", chain.headColliderRadius);
        }
        ImGui::TreePop();
```

with:

```cpp
        // task_manager/verlet-integration-9, PHASE5 - replaces the old
        // per-chain single-sphere "Head Collider" readout - collision is
        // now a plain per-chain opt-in against the whole model's
        // auto-detected Static rigid-body list (drawn once for the whole
        // model, not per chain - see BuildOverlayGeometry()'s own Verlet
        // branch / Step 3.4 below).
        if (chain.collisionEnabled) {
            ImGui::TextDisabled("Collision: enabled (collides against this model's auto-detected colliders)");
        }
        ImGui::TreePop();
```

### 3.4 — (v2, new, REQUIRED) Add `motionType` to `RigidBodyEntry` and replace the per-chain head-collider wireframe with a per-model collider overlay

**Step A — extend `BoneViewerWindow.h`'s `RigidBodyEntry`.** Add one new
field, matching this struct's own existing `group` field's precedent
(added by an earlier campaign purely so a UI feature had something to
compare against):

```cpp
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians; // Euler, PMX convention (see PhysicsData.h) - used only for an approximate visual orientation hint.
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1; // Index into m_bones this body is attached to (-1 if unattached) - drawn as a connecting line.
        std::uint8_t group = 0;
        // task_manager/verlet-integration-9, PHASE5 - lets the Verlet
        // overlay (BuildOverlayGeometry()'s own Verlet branch) filter down
        // to exactly the bodies DetectModelColliders() (Physics/
        // ModelColliderDetection.h) would treat as a real collision
        // obstacle (RigidBodyMotionType::Static) - mirrors that function's
        // own eligibility rule so the Bone Viewer's preview never shows a
        // shape that will not actually collide with anything at runtime.
        RigidBodyMotionType motionType = RigidBodyMotionType::Static;
    };
```

**Step B — populate it.** In `EnsureDataLoaded()` (or wherever
`m_rigidBodies` is populated — confirmed at this file's current line
~410-414), change:

```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group });
        }
```

to:

```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group, body.motionType });
        }
```

**Step C — replace the per-chain wireframe block with a per-model one,
drawn ONCE, OUTSIDE the `for (const DynamicChainDefinition& chain :
verletModel->chains)` loop.** Delete the block quoted in 2.2.2 above (the
entire `if (chain.hasHeadCollider && ...) { ... }` block) from INSIDE that
loop. Immediately AFTER that loop's own closing brace (still inside the
enclosing `if (verletModel != nullptr) { ... }`, and still BEFORE the
existing orphaned-bone diagnostic loop that already reads
`verletModel->diagnostics.orphanedDynamicBoneIndices` right after — confirm
by re-reading this exact spot in the real file before editing, since this
is the one place in this whole phase where getting the brace nesting
subtly wrong would either silently draw nothing or silently draw once per
chain again), insert:

```cpp
                    // task_manager/verlet-integration-9, PHASE5 (v2) -
                    // replaces the old PER-CHAIN single-sphere
                    // "head collider" wireframe with a single, PER-MODEL
                    // pass over every genuinely auto-detected Static
                    // rigid-body collider (Physics/ModelColliderDetection.h's
                    // own eligibility rule, mirrored here via `motionType`)
                    // - drawn exactly ONCE per model regardless of how many
                    // chains have collisionEnabled, since the collider list
                    // itself is shared/model-wide, not owned by any one
                    // chain (see PHASE0_MASTER_STRATEGY.md's "one shared
                    // model-wide collider list" rationale). Reuses the SAME
                    // BuildRigidBodyWireframe() call this window's own
                    // "Rigid Body" view mode already makes (see this
                    // function's own m_viewMode == ModelPartKind::RigidBody
                    // branch) - translate/rotateRadians are already an
                    // absolute model-space bind-pose transform
                    // (PhysicsData.h), so no bone-world-matrix tracking is
                    // needed for this static preview, exactly like that
                    // existing branch.
                    for (const RigidBodyEntry& body : m_rigidBodies) {
                        if (body.motionType != RigidBodyMotionType::Static) {
                            continue;
                        }
                        const std::vector<WireframeSegment> wireframe =
                            BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate, body.rotateRadians);
                        for (const WireframeSegment& segment : wireframe) {
                            ImVec2 screenA, screenB;
                            if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                                && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                                drawList->AddLine(screenA, screenB, IM_COL32(255, 90, 170, 90), 1.25f);
                            }
                        }
                    }
```

(Indentation shown matches this call site's existing nesting depth — adjust
to whatever the real file's exact brace depth is at that point; the
important structural rule is "after the per-chain loop's closing brace,
still inside `if (verletModel != nullptr)`, still before the orphaned-bone
diagnostic loop," not the exact number of leading spaces.)

`BuildRigidBodyWireframe()` already returns an empty vector for any
degenerate shape (confirmed against `RigidBodyWireframe.cpp`'s own
documented per-shape degenerate rules) — no separate degenerate-shape
filter is needed here beyond the `motionType == Static` check; a
Static body with a zero radius/all-zero half-extents simply draws nothing,
exactly matching `DetectModelColliders()`'s own runtime behavior.

### 3.5 — Optional (nice-to-have, not required for correctness): per-shape breakdown

If time allows, extend either (or both) of the `TextDisabled("...%zu
total)")` lines in 3.1/3.2 with a one-line shape breakdown, reusing the
already-existing `RigidBodyShapeLabel()` helper and `model->colliders`
(each entry's own `.shape` field, `Assets::RigidBodyShape`):

```cpp
                std::size_t sphereCount = 0, boxCount = 0, capsuleCount = 0;
                for (const ModelColliderDefinition& c : model->colliders) {
                    switch (c.shape) {
                    case RigidBodyShape::Sphere: ++sphereCount; break;
                    case RigidBodyShape::Box: ++boxCount; break;
                    case RigidBodyShape::Capsule: ++capsuleCount; break;
                    }
                }
                ImGui::TextDisabled("  (%zu sphere, %zu box, %zu capsule)", sphereCount, boxCount, capsuleCount);
```

This is genuinely optional polish — do not treat it as blocking; the
required, functional part of this phase is 3.1/3.2/3.3/3.4 only (without
them, the Editor build does not compile at all after PHASE2, which is the
actual blocking concern).

### 3.6 — No test file needed for this phase

`InspectorPanel.cpp`/`BoneViewerWindow.cpp` are plain ImGui immediate-mode
rendering code with no independently testable pure logic extracted from
them for this specific change (unlike e.g. `MemoryPanelData.h`/
`ProfilerPanelData.h`, which DO have a pure-logic half deliberately split
out for Tier-1 testing) — there is nothing here to unit-test beyond "does
it compile", which is exactly what building the Editor target
(`GTE_ENABLE_EDITOR=ON`, the default) already proves. Do not invent a test
file for this phase; per this campaign's own instructions, a phase step
that would only ever be "build it and eyeball it" is not written as a
distinct chunk — the compile-through of the whole solution (this phase's
own code, plus every previous phase's code) is verified together as part of
PHASE6's final sweep.

---

At the end of this phase: the Editor builds and runs again
(`GTE_ENABLE_EDITOR=ON` and `GTE_ENABLE_PROJECT_PANEL=ON`, the defaults),
with EVERY reference to the removed `hasHeadCollider`/
`headColliderBoneIndex`/`headColliderRadius` trio gone from BOTH
`InspectorPanel.cpp` and `BoneViewerWindow.cpp`/`.h` — exposing exactly one
new checkbox per chain — "Enable Collision" — that, once PHASE1-4's code is
in place, makes that chain's joints collide against every Sphere/Box/
Capsule Static rigid body the model ships, and a single, correct, per-model
(not per-chain) wireframe preview of those same colliders in the Bone
Viewer's 3D overlay. PHASE6 is the final wrap-up: a true end-to-end
regression test plus a full build-registration audit.
