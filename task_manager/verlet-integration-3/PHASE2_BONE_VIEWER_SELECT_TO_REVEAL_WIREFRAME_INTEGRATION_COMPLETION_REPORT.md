# PHASE2 — Bone Viewer: Select-to-Reveal Wireframe Integration (`src/Editor/BoneViewerWindow.cpp`) — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md` (the Culprit itself — the unconditional,
shape-blind size-hint circle). Implements
`PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md` in full,
building on Phase 1's already-completed `src/Editor/RigidBodyWireframe.h/.cpp`
(`BuildRigidBodyWireframe()`, `PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE_COMPLETION_REPORT.md`).

## What was done

- **`src/Editor/BoneViewerWindow.cpp`** — added `#include "RigidBodyWireframe.h"`
  alongside the existing includes (right after `ProjectPanelData.h`).
- **Deleted the flawed, unconditional "size hint circle" block** (the
  `if (m_viewMode == ModelPartKind::RigidBody) { ... }` sub-block that used to
  run for EVERY rigid body regardless of selection, collapsing Sphere/Box/
  Capsule into the same shape-blind `pixelRadius`-based unfilled circle via
  `characteristicSize`/`Length(body.shapeSize)`).
- **Replaced it with the select-to-reveal block per the phase document's 3.2,
  verbatim**: gated on `m_viewMode == ModelPartKind::RigidBody && isSelected`
  (both pre-existing locals in the same scope — no new plumbing), it calls
  Phase 1's `BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate,
  body.rotateRadians)`, projects each returned segment's two endpoints
  independently via the existing `ProjectToScreen()` helper, and draws each
  fully-on-screen segment via `drawList->AddLine(..., dotColor, 1.5f)` — reusing
  the same `dotColor` local already resolved to orange
  (`IM_COL32(255, 140, 0, 255)`) whenever `isSelected` is true, so the
  wireframe automatically matches the existing selection-ring/dot highlight
  color with zero new color constant.
- Every other rigid body (unselected) now shows ONLY its plain dot — exactly
  the existing shared dot-drawing code Bone/Joint mode already use, unchanged.
  No new "plain point" behavior needed to be added; removing the flawed block
  was sufficient, exactly as the phase document predicted.

## What was NOT touched (per the phase document's own "What We Will NOT Do")

- No new color constant was added for the wireframe.
- The wireframe is not drawn for a hovered-but-unselected rigid body — only
  `isSelected` gates it.
- Each wireframe segment's two endpoints are checked independently
  (`ProjectToScreen(...) && ProjectToScreen(...)`) — a partially-offscreen
  wireframe still draws whichever of its own segments remain fully on-screen,
  matching the existing bone-parent-line/joint-connector-line convention.
- The dot/selection-ring drawing above this block, hover/click hit-testing,
  the name-label block below it, and Bone/Joint mode are all completely
  unchanged.
- Joints did not receive this wireframe treatment — out of scope per
  `PHASE0_MASTER_STRATEGY.md`, Step 4.
- No `Selection.h/.cpp`, `RigidBodyEntry`, or `EnsureDataLoaded()` change was
  needed — every field `BuildRigidBodyWireframe()` needs was already present
  and already populated correctly.

## Verification

- **Fast compile check** (per this campaign's workflow rules — no full build/
  regression test yet): `cmake --build build --target gte_core` —
  succeeded: `BoneViewerWindow.cpp` recompiled cleanly, `libgte_core.a`
  relinked, zero warnings/errors.
- This phase adds no new Tier-1-testable logic (the change is a pure
  ImGui-drawing call site calling Phase 1's already-tested
  `BuildRigidBodyWireframe()`), so no new test file was required — matching
  the phase document's own Step 5 checklist item #4. Phase 1's own
  `RigidBodyWireframeTest.*` suite (8 passing tests, see Phase 1's completion
  report) already covers the geometry this phase now calls.
- A full build/regression test run (`GreatTamanaEngineTests`, real-model
  visual verification against a live MMD rigid-body model) was intentionally
  NOT run this session, per this task's own workflow rules ("No Full Build" —
  only a fast compile check is required unless a phase explicitly calls for a
  full build; this phase's own Step 5 checklist item #5, the real-executable
  visual check, is a manual follow-up for whoever next opens the Editor
  against a real rigged model).

## Campaign status

This completes Phase 2 of `verlet-integration-3`
(`PHASE0_MASTER_STRATEGY.md`) — both phases of this campaign
(`PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md` and
`PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md`) are now
implemented and compiling cleanly. The Bone Viewer's "Rigid Bodies" view mode
no longer renders every rigid body as a generic shape-blind circle: each body
defaults to a plain gizmo dot, and selecting one reveals its real, per-shape
wireframe (wire sphere/box/capsule) built from its actual `shape`/`shapeSize`/
`translate`/`rotateRadians` data.
