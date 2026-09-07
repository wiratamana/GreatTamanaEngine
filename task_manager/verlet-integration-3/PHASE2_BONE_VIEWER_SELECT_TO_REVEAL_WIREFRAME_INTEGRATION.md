# PHASE2 — Bone Viewer: Select-to-Reveal Wireframe Integration (`src/Editor/BoneViewerWindow.cpp`)

Parent: `PHASE0_MASTER_STRATEGY.md` (the Culprit itself — the unconditional,
shape-blind size-hint circle). Depends on: Phase 1's
`RigidBodyWireframe.h`'s `BuildRigidBodyWireframe()` existing and compiling.
Produces: the user-visible fix — every rigid body defaults to a single
plain gizmo point; selecting one reveals its real, per-shape wireframe.

## Step 1: The Goal

1. Delete the flawed always-on "size hint circle" block entirely — no
   rigid body, selected or not, ever draws that generic unfilled circle
   again.
2. Every rigid body, by default, shows exactly what a Bone/Joint already
   shows today: a small filled dot (this already exists, unchanged, in the
   shared per-part drawing loop — see `PHASE0_MASTER_STRATEGY.md`'s Culprit
   #5) — i.e. this requirement is ALREADY satisfied by existing code once
   the flawed circle block is removed; this phase's job is removing what's
   in the way, not adding new "plain point" behavior.
3. The ONE rigid body (if any) that `ctx.selection.IsModelPartSelected(
   m_targetEntity, ModelPartKind::RigidBody, i)` reports true for
   additionally draws Phase 1's real wireframe segments, projected and
   drawn in the exact same viewport overlay pass, in the same orange
   selection color its dot/ring already use.

## Step 2: The Situation / The Problem

See `PHASE0_MASTER_STRATEGY.md`'s Culprit #1 for the exact current code and
why it's wrong. The fix is entirely contained inside
`BoneViewerWindow::Build()`'s existing per-part overlay-drawing loop (the
`for (std::size_t i = 0; i < overlayParts.size(); ++i) { ... }` loop, inside
the `if (!overlayParts.empty()) { ... }` block) — specifically the
`if (m_viewMode == ModelPartKind::RigidBody) { ... }` sub-block at its very
end (today: `BoneViewerWindow.cpp`, immediately after the `if (isSelected) {
drawList->AddCircle(...) }` selection-ring line and immediately before the
`if (m_showAllNames || matchesFilter || isHovered || isSelected) { ... }`
name-label block).

## Step 3: The Plan

### 3.1 `BoneViewerWindow.cpp` — new include

Add, alongside the existing includes at the top of the file:

```cpp
#include "RigidBodyWireframe.h"
```

### 3.2 `BoneViewerWindow.cpp` — replace the flawed block

Find this exact block (today, inside the per-part overlay loop, right after
the `isSelected` outline-circle draw):

```cpp
                // Rigid Body mode also draws an approximate "size" hint - an
                // unfilled circle whose pixel radius is the on-screen
                // distance to a second point offset along the view-right
                // axis by the shape's characteristic size - a deliberate,
                // documented screen-space approximation (see
                // PHASE0_MASTER_STRATEGY.md, "What We Will NOT Do"), never a
                // true oriented 3D wireframe.
                if (m_viewMode == ModelPartKind::RigidBody) {
                    const RigidBodyEntry& body = m_rigidBodies[i];
                    const float characteristicSize = body.shape == RigidBodyShape::Box ? Length(body.shapeSize) : body.shapeSize.x;
                    if (characteristicSize > kEpsilon) {
                        Vec3 viewRight = Normalize(Cross(Vec3::Up(), Normalize(m_camTarget - eye)));
                        if (LengthSquared(viewRight) < kEpsilon) {
                            viewRight = Vec3::Right();
                        }
                        ImVec2 sizeScreen;
                        if (ProjectToScreen(body.translate + viewRight * characteristicSize, viewProj, imageMin, imageMax,
                                sizeScreen)) {
                            const float dx = sizeScreen.x - screenPositions[i].x;
                            const float dy = sizeScreen.y - screenPositions[i].y;
                            const float pixelRadius = std::sqrt(dx * dx + dy * dy);
                            if (pixelRadius > 1.0f) {
                                drawList->AddCircle(screenPositions[i], pixelRadius, dotColor, 0, 1.5f);
                            }
                        }
                    }
                }
```

**Delete it entirely** and replace it with:

```cpp
                // As of task_manager/verlet-integration-3
                // (PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md):
                // an UNSELECTED rigid body shows ONLY its plain dot (drawn
                // above, shared with Bone/Joint mode) - exactly a "single
                // selectable point", per the story's own requirement. Only
                // the CURRENTLY SELECTED rigid body additionally reveals its
                // real, per-shape wireframe (a wire sphere/box/capsule built
                // from its actual shape/size/rotation - see
                // RigidBodyWireframe.h), never a generic screen-space circle
                // - and never for more than one body at once, since
                // Selection is single-selection end-to-end.
                if (m_viewMode == ModelPartKind::RigidBody && isSelected) {
                    const RigidBodyEntry& body = m_rigidBodies[i];
                    const std::vector<WireframeSegment> wireframe =
                        BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate, body.rotateRadians);
                    for (const WireframeSegment& segment : wireframe) {
                        ImVec2 screenA, screenB;
                        if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                            && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                            drawList->AddLine(screenA, screenB, dotColor, 1.5f);
                        }
                    }
                }
```

Note `dotColor` is already computed a few lines above this block (inside
the same loop iteration) and is already `IM_COL32(255, 140, 0, 255)` (orange)
whenever `isSelected` is true — reusing it means the wireframe automatically
matches the exact same color the dot/selection-ring already highlight with,
with no new color constant needed. `isSelected` itself is also already
computed a few lines above (`const bool isSelected =
ctx.selection.IsModelPartSelected(m_targetEntity, m_viewMode,
static_cast<std::int32_t>(i));`) — both are pre-existing locals in this
exact scope, not new plumbing.

### 3.3 No other changes

Nothing else in `BoneViewerWindow.h/.cpp` changes: `RigidBodyEntry` already
carries every field `BuildRigidBodyWireframe()` needs
(`shape`/`shapeSize`/`translate`/`rotateRadians`), `EnsureDataLoaded()`
already populates it correctly (see `PHASE0_MASTER_STRATEGY.md`'s Culprit
list — nothing there needs touching), `Build()`'s toolbar/tree-pane/Bone-
mode/Joint-mode code is completely untouched, and no `Selection.h/.cpp`
change is needed (`IsModelPartSelected()` already exists and is already
called exactly where this fix needs it).

## Step 4: What We Will NOT Do

- We will **not** add a NEW color constant for the wireframe — reusing the
  existing `dotColor` (already orange when selected) is deliberate: the
  wireframe is visually "the same highlighted thing, just showing more of
  it," not a separate visual language.
- We will **not** draw the wireframe for a HOVERED (but not selected) rigid
  body — only `isSelected` gates it, per the story's own "selecting it"
  wording; a hovered-but-unselected body keeps showing only its (whitened)
  plain dot, unchanged.
- We will **not** skip an entire wireframe if ONE segment's endpoint fails
  to project (e.g. dips behind the near camera plane) — each segment is
  independently checked (`ProjectToScreen(...) && ProjectToScreen(...)`),
  so a partially-offscreen wireframe still draws whichever of its own
  segments remain fully on-screen, exactly like the existing bone-parent-line/
  joint-connector-line drawing already does for their own single segment.
- We will **not** change the dot/selection-ring drawing above this block,
  the hover/click hit-testing, the name-label block below it, or anything
  in Bone/Joint mode.
- We will **not** add this same wireframe treatment to Joints in this
  phase — out of scope per `PHASE0_MASTER_STRATEGY.md`, Step 4.

## Step 5: Their Role

Implementer checklist for this phase:

1. Confirm Phase 1's `src/Editor/RigidBodyWireframe.h/.cpp` already exist,
   are already added to `CMakeLists.txt`, and `GreatTamanaEngineTests`
   passes `RigidBodyWireframeTest.*` before starting this phase.
2. Add the `#include "RigidBodyWireframe.h"` per 3.1.
3. Delete the old size-hint-circle block and replace it with the new
   select-to-reveal block, exactly per 3.2 — do not leave both blocks
   present at once (that would draw the wireframe AND a stray leftover
   circle for the selected body).
4. Build `GreatTamanaEngineTests` and confirm the full suite (including
   Phase 1's new tests) still passes — this phase itself adds no new
   Tier-1-testable logic (the change is a pure ImGui-drawing call site), so
   no new test file is required here; Phase 1's own tests are what already
   cover the geometry this phase now calls.
5. Build the real `GreatTamanaEngine` executable (this phase's correctness
   is primarily visual) and, against a real imported MMD model with rigid
   bodies (a jiggle-bone/skirt/hair model is ideal, since it typically has
   a mix of all three shapes), open the Bone Viewer, switch to "Rigid
   Bodies" mode, and confirm:
   - Every rigid body shows only a plain dot by default — no circle of any
     kind around any of them.
   - Clicking one rigid body's row (or its dot directly) reveals a
     wireframe that visibly matches its actual shape: a rounded wire sphere
     for a `Sphere` body, a straight-edged oriented wire box for a `Box`
     body (rotated correctly if that body's `rotateRadians` is non-zero),
     and a rounded-cap wire capsule for a `Capsule` body.
   - Selecting a DIFFERENT rigid body immediately moves the wireframe to
     the new selection and removes it from the previous one (never two
     wireframes visible at once).
   - Switching to "Bones" or "Joints" mode, then back to "Rigid Bodies",
     still shows the previously-selected body's wireframe correctly (no
     stale/incorrect state).
