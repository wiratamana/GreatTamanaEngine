# PHASE0 — MASTER STRATEGY: Bone Viewer — Real Per-Shape Rigid Body Wireframes, Selected-Only

Orchestrator document for this campaign (folder kept as `verlet-integration-3`
per the task's own filing convention — the campaign's actual subject is the
Bone Viewer's Rigid Body gizmo, not Verlet physics). Every child phase
document in this folder implements one slice of this plan. This document is
the single source of truth for **ordering, ownership, and the identified
root cause** — read this first, then execute `PHASE1_...md` → `PHASE2_...md`
in order. Both phases are real, compilable increments that leave the engine
building correctly end-to-end; neither is "just planning" — each phase
produces new/modified `.h`/`.cpp`/`CMakeLists.txt`/test files.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis + ordering. |
| `PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md` | New `src/Editor/RigidBodyWireframe.h/.cpp` — a pure, Tier-1-tested function, `BuildRigidBodyWireframe(shape, shapeSize, translate, rotateRadians)`, that turns one `RigidBody`'s shape data into a real, oriented set of 3D line segments (a sphere's 3 great circles, a box's 12 oriented edges, a capsule's 2 rings + 4 verticals + 4 hemisphere arcs) in the SAME model-local space as `RigidBody::translate` — genuine per-shape geometry, not a screen-space circle. No `BoneViewerWindow` code touched yet. |
| `PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md` | `src/Editor/BoneViewerWindow.cpp` — deletes the flawed always-on, shape-blind "size hint circle" block and replaces it with: every rigid body defaults to a single plain gizmo dot (matching the story's "single selectable point" requirement, and matching how Bones/Joints already render), and ONLY the currently-`Selection`-selected rigid body additionally projects+draws Phase 1's real wireframe segments, in the same color its selection highlight already uses. |

## Step 1: The Goal (Where are we going?)

Per the story: the Bone Viewer's "Rigid Bodies" view mode
(`src/Editor/BoneViewerWindow.h/.cpp`, opened from the Inspector's "Open Bone
Viewer" button) must stop rendering every rigid body as a generic circle.
Concretely:

1. By default (nothing selected, or a DIFFERENT part selected), every rigid
   body in the currently-active model shows as a single small, selectable
   gizmo point in the 3D viewport — exactly like a Bone's or a Joint's own
   dot today, nothing more.
2. The moment the user selects one specific rigid body (clicking its row in
   the left tree pane, or clicking its dot directly in the viewport — both
   already route through `Selection::SelectModelPart()`, see
   `src/Editor/Selection.h`), that ONE rigid body's gizmo additionally draws
   a real wireframe outline that actually matches its physics data: a wire
   sphere for `RigidBodyShape::Sphere`, a wire (oriented) box for `Box`, a
   wire capsule for `Capsule` — built from its real `shapeSize`/`translate`/
   `rotateRadians` fields (`src/Assets/PhysicsData.h`), not a flat,
   shape-blind screen-space circle.
3. Every other, unselected rigid body in the same frame keeps showing only
   its plain dot — the wireframe is a "select to reveal" affordance, never a
   permanent per-body decoration (that would recreate the exact same
   "everything is circles" clutter the story complains about, just with a
   fancier circle).

## Step 2: The Situation / The Problem (Where are we now?)

A close read of the current source tree
(`src/Editor/BoneViewerWindow.h/.cpp`, `src/Assets/PhysicsData.h`,
`src/Math/Quat.h/.cpp`, `src/Math/MathTypes.h`) found the exact root cause —
the "culprit" — and confirmed the surrounding pieces (Selection, the
per-mode dot-drawing loop, `ProjectToScreen()`) already do everything else
this fix needs, unchanged:

1. **Culprit — `BoneViewerWindow::Build()`'s Rigid Body overlay block draws
   an unconditional, shape-blind "size hint circle" for EVERY rigid body,
   every frame, regardless of selection.** The exact code (inside `Build()`,
   the `if (m_viewMode == ModelPartKind::RigidBody) { ... }` block that runs
   once per part inside the shared per-part drawing loop):

   ```cpp
   if (m_viewMode == ModelPartKind::RigidBody) {
       const RigidBodyEntry& body = m_rigidBodies[i];
       const float characteristicSize = body.shape == RigidBodyShape::Box ? Length(body.shapeSize) : body.shapeSize.x;
       if (characteristicSize > kEpsilon) {
           Vec3 viewRight = Normalize(Cross(Vec3::Up(), Normalize(m_camTarget - eye)));
           if (LengthSquared(viewRight) < kEpsilon) {
               viewRight = Vec3::Right();
           }
           ImVec2 sizeScreen;
           if (ProjectToScreen(body.translate + viewRight * characteristicSize, viewProj, imageMin, imageMax, sizeScreen)) {
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

   This runs for **every** `m_rigidBodies[i]`, selected or not — that is
   exactly why "everything" renders as a circle: a `Box` and a `Capsule` both
   collapse to the SAME `pixelRadius`-based unfilled circle as a `Sphere`
   (`characteristicSize` is a single scalar for all three shapes — a box's
   own half-extents even get flattened through `Length(body.shapeSize)`,
   throwing away its actual proportions entirely), and it is drawn
   regardless of whether that body is the current `Selection`. This block
   was a KNOWN, DOCUMENTED simplification when it was written (see
   `task_manager/verlet-integration-2/PHASE0_MASTER_STRATEGY.md`'s own "What
   We Will NOT Do": *"We will not attempt a true, oriented 3D wireframe
   box/capsule/sphere for the Rigid Body gizmo"*) — this campaign is exactly
   the follow-up that document already anticipated needing, now that the
   story requires real per-shape wireframes.
2. **There is currently no code anywhere in the engine that turns a
   `RigidBody`'s shape/size/rotation into actual 3D line-segment geometry.**
   `src/Assets/PhysicsData.h` is pure data (no physics simulation backend is
   vendored yet — see that file's own comment), and nothing under
   `src/Editor/` or `src/Renderer/` builds a wireframe mesh for anything.
   This must be added as new code (Phase 1), not adapted from an existing
   helper.
3. **The correct Euler-rotation convention for a PMX `RigidBody::rotateRadians`
   is already established elsewhere in this codebase and must be reused,
   not reinvented.** `third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp`
   (the reference MMD physics implementation this engine's own
   `PmxLoader.cpp` sources its `RigidBody`/`Joint` data from — see
   `PmxLoader.cpp` lines 398/430, `out.rotateRadians = ToVec3(body.m_rotate)`)
   builds a PMX rigid body's world rotation matrix as:

   ```cpp
   auto rx = glm::rotate(glm::mat4(1), pmxRigidBody.m_rotate.x, glm::vec3(1, 0, 0));
   auto ry = glm::rotate(glm::mat4(1), pmxRigidBody.m_rotate.y, glm::vec3(0, 1, 0));
   auto rz = glm::rotate(glm::mat4(1), pmxRigidBody.m_rotate.z, glm::vec3(0, 0, 1));
   glm::mat4 rotMat = ry * rx * rz; // apply rz first, then rx, then ry
   ```

   This engine's own `Quat::FromEulerDegrees(pitchX, yawY, rollZ)`
   (`src/Math/Quat.h/.cpp`) already documents its own composition order as
   **"Yaw(Y) * Pitch(X) * Roll(Z) — i.e. roll is applied first ... then
   pitch, then yaw"** — the exact same `Ry * Rx * Rz` order as saba's own
   `rotMat` above, just with `x`→pitch, `y`→yaw, `z`→roll naming. This means
   `Quat::FromEulerDegrees(RadToDeg(rotateRadians.x), RadToDeg(rotateRadians.y),
   RadToDeg(rotateRadians.z))` is already the byte-for-byte correct
   conversion — no new rotation-order code needs to be invented or
   hand-verified from scratch; Phase 1 just has to call the existing
   function correctly (and say so, so nobody "fixes" it into `x,y,z` order
   later thinking that's more natural).
4. **`ProjectToScreen()` (an anonymous-namespace free function local to
   `BoneViewerWindow.cpp`) already does exactly the "model-space point →
   on-screen pixel, or false if behind the camera" job a wireframe segment's
   two endpoints each need** — no new projection math is needed, only new
   calls to the existing helper, once per wireframe segment endpoint,
   exactly like the deleted size-hint block already did for its one extra
   offset point.
5. **`ctx.selection.IsModelPartSelected(m_targetEntity, ModelPartKind::RigidBody, i)`
   is already computed once per part, per frame, right where the deleted
   block lives** (`const bool isSelected = ctx.selection.IsModelPartSelected(...)`,
   a few lines above it) — gating the new wireframe draw on this existing
   local is a one-line `if`, not a new selection-plumbing concern (Phase 2
   touches nothing in `Selection.h/.cpp`).

## Step 3: The Plan (How will we get there?)

Execute the two phases in order — Phase 2 depends on Phase 1's
`BuildRigidBodyWireframe()` existing and compiling first:

1. **Phase 1** adds `src/Editor/RigidBodyWireframe.h/.cpp` — a small, pure,
   ImGui/Vulkan-free module (per `AGENTS.md`'s "Testability & Regression
   Safety" rule: "design new logic to be Tier-1-testable whenever the
   underlying problem allows it") that takes a `RigidBodyShape` + its
   `shapeSize`/`translate`/`rotateRadians` and returns a
   `std::vector<WireframeSegment>` (each a plain `{Vec3 a, b;}` pair,
   already fully positioned/oriented in model-local space) tracing that
   exact shape's real silhouette. Covered by
   `tests/Editor/RigidBodyWireframeTests.cpp`, gated the same way
   `tests/Editor/ModelRigCacheTests.cpp` already is
   (`GTE_ENABLE_EDITOR` + `GTE_ENABLE_PROJECT_PANEL`).
2. **Phase 2** rewires `BoneViewerWindow::Build()`'s Rigid Body overlay
   block: deletes the shape-blind size-hint circle entirely, and — only for
   whichever ONE rigid body (if any) `IsModelPartSelected()` currently
   reports true for — calls Phase 1's `BuildRigidBodyWireframe()`, projects
   every returned segment's two endpoints via the existing
   `ProjectToScreen()`, and draws each on-screen segment with
   `drawList->AddLine()` in the same `dotColor` the selection highlight
   already resolves to (orange). Every other rigid body, and the selected
   one's OWN plain dot, are otherwise completely unaffected — this is a
   pure "add a conditional extra draw, delete an unconditional wrong one"
   change, isolated to one function.

## Step 4: What We Will NOT Do (Focus)

- We will **not** touch `src/Assets/PhysicsData.h`, `RigFile.h/.cpp`,
  `PmxLoader.cpp`, or `ModelRigCache.h/.cpp` at all — every field this
  campaign needs (`shape`/`shapeSize`/`translate`/`rotateRadians`) is
  already decoded and already flows into `BoneViewerWindow::RigidBodyEntry`
  today (see `BoneViewerWindow.cpp`'s `EnsureDataLoaded()`); this is purely
  a viewport-drawing fix.
- We will **not** change how Bone mode or Joint mode render in any way —
  both already draw a correct, unconditional dot + connector-line overlay
  today and are completely out of scope; only the Rigid Body branch of the
  overlay block changes.
- We will **not** build a true, GPU-rendered 3D mesh wireframe (a real
  `VkPipeline` with `VK_POLYGON_MODE_LINE`, or a separate line-list vertex
  buffer) — the existing "hand-drawn `ImDrawList` overlay projected through
  the same `viewProj` every frame" approach this whole window already uses
  for every other gizmo (dots, parent-lines, connector-lines, name labels)
  is sufficient and consistent; Phase 1's geometry is expressed as plain
  `Vec3` line segments specifically so Phase 2 can draw them exactly the
  same way the existing overlay already draws everything else.
- We will **not** attempt full 6-DOF constraint/joint-limit visualization,
  or extend this wireframe treatment to Joints — the story is specifically
  about Rigid Bodies ("Rigid body views now render everything as circle").
- We will **not** add multi-select (more than one rigid body's wireframe
  visible at once) — `Selection` stays single-selection end-to-end, exactly
  as it is today; "selecting it" in the story means the one current
  `Selection`, not a new concept.
- We will **not** change `RigidBodyEntry`'s own fields or
  `BoneViewerWindow::EnsureDataLoaded()` — `body.shape`/`body.shapeSize`/
  `body.translate`/`body.rotateRadians` are already exactly what Phase 1's
  new function needs, verbatim.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact structs/functions/signatures to write,
the exact existing call site to touch, and the exact test file to add.
Follow `AGENTS.md`'s "Testability & Regression Safety" rule and land Phase
1's `tests/Editor/RigidBodyWireframeTests.cpp` in the SAME change as
`RigidBodyWireframe.h/.cpp` itself — do not defer it. Do not reorder the
phases: Phase 2's edit to `BoneViewerWindow.cpp` calls
`BuildRigidBodyWireframe()` directly, so Phase 1 must exist and compile
first. Build `gte_core` + `GreatTamanaEngineTests` after each phase and
confirm every new/existing test still passes before starting the next
phase.
