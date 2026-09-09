# PHASE0_MASTER_STRATEGY — Unity-Style Procedural Scene-View Grid

## Role of this document

This is the **orchestrator**. It does not contain implementation code itself
— it defines the goal, the current situation/problem, the locked design
decisions (already confirmed with the project owner — see "Locked Design
Decisions" below), and the execution order of the child phase documents.
Every child phase document assumes this document has been read first.

Child phases, in strict execution order (each is independently compilable —
a fast compile check must pass at the end of every phase before moving to
the next):

- `PHASE1_GRID_MATH_FOUNDATION.md` — pure, Tier-1-testable C++ math (no
  Vulkan, no ImGui) that is the reviewable "spec" the GPU shader will mirror.
- `PHASE2_GRID_SHADERS.md` — the actual procedural GLSL shader pair
  (`SceneGrid.vert` / `SceneGrid.frag`), compiled to SPIR-V, not wired to
  anything yet.
- `PHASE3_SCENE_GRID_RENDERER.md` — a new `SceneGridRenderer` C++ class that
  owns the dedicated `VkPipeline`/`VkPipelineLayout` for those shaders and
  knows how to record one draw call with them — still not wired to anything
  yet (nothing calls it in production).
- `PHASE4_RENDERGRAPH_INTEGRATION.md` — wires `SceneGridRenderer` into the
  real per-frame rendering path so the grid actually appears in the Editor's
  "Scene" view, correctly depth-tested against scene geometry.
- `PHASE5_POLISH_AND_VERIFICATION.md` — axis-line colors, LOD tuning
  constants, edge-case hardening (grazing camera angles, camera looking
  straight down/up, degenerate matrices), and full regression verification.

---

## Step 1: The Goal (Where are we going?)

Add a Unity Scene-view-style **infinite ground grid** to this engine's
Editor "Scene" panel (`src/Editor/Panels/ScenePanel.cpp`) — and **only**
that panel, never the "Game" view, never a release build.

Concretely, per the user's own story and the confirmed design decisions
below, the finished feature must:

1. Render as a **procedural fragment shader effect** — no 3D mesh, no
   texture/image asset — using a **ray-plane intersection** computed
   per-pixel in the shader against the world's `Y = 0` plane, so the grid
   looks infinite in every direction with no real geometry footprint.
2. Draw crisp, **anti-aliased** grid lines at any camera distance/angle
   using screen-space derivatives (`fwidth()`) + `smoothstep()`-style
   coverage math — never a blurry/shimmering textured grid.
3. Show **two LOD levels** — 1-world-unit "minor" lines and 10-world-unit
   "major" lines (Unity's own default spacing, confirmed) — with the minor
   lines fading out automatically as the camera zooms out (so the pattern
   never aliases into moiré noise).
4. Draw **colored X/Z axis highlight lines** through the world origin (red
   for the world X axis, blue for the world Z axis — matching Unity
   exactly, confirmed).
5. **Fade to fully transparent** with distance from the camera, so the
   horizon never looks like a messy converging line-field.
6. Be **correctly depth-tested/occluded** by real scene geometry already in
   front of it (an opaque cube sitting on the grid must hide the grid lines
   underneath it), exactly like Unity's own Scene grid.
7. Be **non-selectable / non-interactive** — it must never appear in
   `RenderSystem::CollectRenderables()`, never be an `Entity`, never be
   picked by any future ray-cast/selection system, and must have zero
   effect on `Game`'s ECS world. This falls out for free from the chosen
   architecture (see "Locked Design Decisions" below) — the grid is pure
   Editor-side, GPU-only visual output, never touching `Registry`.
8. Render **only in the Editor's "Scene" panel** (confirmed) — never
   "Game", and (since `NullEditorLayer` always reports "Scene" as
   nonexistent) it is automatically, structurally absent from a release
   (`GTE_ENABLE_EDITOR=OFF`) build, at zero cost, with no separate switch
   needed.
9. Always be visible whenever "Scene" is visible — **no on/off toggle**
   (confirmed, matches Unity, which has no such toggle either).

## Step 2: The Situation / The Problem (Where are we now?)

This codebase already has a fully real Vulkan renderer using **dynamic
rendering** (no `VkRenderPass`/`VkFramebuffer`), a from-scratch ECS, and — as
of the Render Graph campaign — a genuine, declarative `gte::rg::RenderGraph`
(`src/Renderer/RenderGraph/`) that drives every real frame. Concretely,
relevant to this feature:

- `grep`-ing the whole `src/` tree for "grid" today only turns up unrelated
  hits (VMD's fixed 30fps *frame* grid). **There is no ground/ground-plane
  visual of any kind in this engine yet.** This is a brand-new feature, not
  a fix.
- The Editor's "Scene" panel (`Panels/ScenePanel.cpp`) already has its own
  independently-orbitable `EditorCamera` (`src/Editor/EditorCamera.h`) and
  its own off-screen `RenderTexture` (`m_sceneView`, owned by
  `ImGuiEditorLayer`). Real per-frame rendering into it is declared as a
  named RenderGraph pass, `"SceneView"`, built by
  `AddSceneViewPass()` in `src/Application/RenderPasses.cpp` — **this is the
  exact pass whose output the grid must composite into**, correctly
  depth-tested against whatever `Game::Render()` already drew that pass.
- **A critical, VERIFIED gotcha that shapes the whole architecture below:**
  it would be tempting to declare the grid as a brand-new, second RenderGraph
  pass (`"SceneGrid"`) that writes the *same* `sceneViewTarget` texture
  handle a second time, right after `"SceneView"`, using
  `PassBuilder::WriteColorAttachment(handle)` with **no** clear value (i.e.
  `VK_ATTACHMENT_LOAD_OP_LOAD`, preserving `"SceneView"`'s just-rendered
  pixels). Reading `RenderGraphCompiler::Compile()`
  (`src/Renderer/RenderGraph/RenderGraphCompiler.cpp`, the WAW edge-building
  loop) confirms the graph WOULD correctly order `"SceneGrid"` strictly
  after `"SceneView"` (a real dependency edge is created for two writes to
  the same handle). **However**, reading
  `RenderGraphBarrierPlanner::RequiresBarrier()`
  (`src/Renderer/RenderGraph/RenderGraphBarrierPlanner.cpp`) shows it returns
  `!(previous == next)` — i.e. **no GPU barrier is emitted between two
  passes that both declare `ColorAttachmentWrite`/
  `DepthStencilAttachmentReadWrite` on the same resource**, since the
  `ResourceState` doesn't change. Two *separate*
  `vkCmdBeginRendering`/`vkCmdEndRendering` brackets writing the same image
  with no barrier in between is **not** the same thing as two draw calls
  inside *one* bracket — Vulkan does not guarantee write-visibility/ordering
  across two separate rendering instances without an explicit
  `vkCmdPipelineBarrier2`. A separate-pass design would therefore be a real,
  subtle, hard-to-diagnose synchronization bug (the grid depth-testing
  against a not-yet-visible depth buffer, or the driver reordering the two
  passes' fragment output). **This design is explicitly REJECTED below —
  see "Locked Design Decisions".**
- `src/Editor/AssetPreviewMesh.h/.cpp` (the Inspector's mesh-preview viewer)
  and `src/Editor/ComputeBlurValidation.h/.cpp` (the Scene-view compute
  box-blur debug pass) are the two proven, existing precedents for "a small,
  Editor-owned class that builds its own dedicated `VkPipeline`/
  `VkPipelineLayout` directly (bypassing `Renderer::CreatePipeline()`, which
  only knows this engine's fixed built-in vertex layouts) and records its
  own raw Vulkan draw/dispatch commands" — this feature's own
  `SceneGridRenderer` (Phase 3) follows that exact same proven shape.
- `IEditorLayer` (`src/Editor/EditorLayer.h`) already has a proven precedent
  for "a RenderGraph-aware capability that only the real `ImGuiEditorLayer`
  implements, always a safe no-op on `NullEditorLayer`" —
  `AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()`. This
  feature's own new `IEditorLayer::RenderSceneGrid()` (Phase 4) follows that
  same interface-seam pattern.
- `src/Application/RenderPasses.cpp`'s `AddPresentPass()` already has a
  proven precedent for "an opaque `std::function` callback, supplied by the
  caller, invoked from *inside* an already-open pass's execute callback,
  still inside that pass's own `vkCmdBeginRendering`/`vkCmdEndRendering`
  bracket, without `RenderPasses.cpp` itself ever needing to know what that
  callback actually does" — `recordImGui`. This feature's own
  `AddSceneViewPass()` change (Phase 4) adds a directly analogous
  `recordSceneOverlay` parameter, keeping `src/Application/` (which must
  never depend on `src/Editor/` concrete types — see `AGENTS.md`, Clean
  Architecture) completely Editor-agnostic.
- This engine's math library (`src/Math/Mat4.h`) already has everything the
  ray-plane math needs: `Mat4::TryInverse()`, `Vec4 operator*(const Mat4&,
  const Vec4&)` (the **projective**, non-divided multiply — critically
  `Mat4::TransformPoint()` is the WRONG function to reach for here, since it
  assumes `w == 1` stays true after the transform, which is false for a
  general projective inverse-view-projection matrix), and the exact
  push-constant convention (`model` then `viewProj`, each a raw
  `Mat4::Data()` memcpy, 128 bytes total) every existing pipeline in this
  engine already uses.

## Step 3: The Plan (detailed strategy — see child phases for full code)

### Locked Design Decisions (confirmed with the project owner before writing
the child phases — do not revisit without a fresh discussion)

1. **Axis highlight lines: YES.** The grid includes Unity-style colored
   X-axis (red) / Z-axis (blue) highlight lines through the world origin,
   in addition to the plain grey grid lines.
2. **No visibility toggle.** The grid is always drawn whenever the "Scene"
   panel itself is visible/rendered this frame — exactly mirroring Unity,
   which has no such toggle either. (This also means `EditorContext` gains
   **no** new boolean field for this feature — contrast with the existing
   `showBlurredSceneOutput` precedent, which this feature deliberately does
   **not** copy.)
3. **LOD spacing: 1 world-unit minor / 10 world-unit major** — Unity's own
   default, kept as-is for this engine.
4. **Scene view only, never Game view.** The grid is wired in exactly one
   place: `AddSceneViewPass()`. `AddGameViewPass()`/`AddPresentPass()` are
   never touched by this feature at all.
5. **Rendering architecture: ONE draw call, inside the ALREADY-OPEN
   `"SceneView"` pass's existing dynamic-rendering bracket, issued
   immediately after `Game::Render()`'s own draws finish, never a second,
   separate RenderGraph pass.** This is what makes the grid correctly
   depth-tested against real scene geometry (multiple draw calls inside one
   `vkCmdBeginRendering`/`vkCmdEndRendering` bracket have a well-defined,
   already-relied-upon-everywhere-else ordering guarantee — see
   `FrameRecorder::RecordFrame()`'s own multi-item draw loop, which is
   exactly how this engine's existing primitive shapes already occlude each
   other correctly) — see "Step 2" above for why the tempting
   separate-pass alternative is rejected outright, not merely deprioritized.
6. **Depth handling:** the grid pipeline enables depth **testing**
   (`VK_COMPARE_OP_LESS`, matching every other pipeline in this engine) but
   **disables** depth **writing** — it is drawn last in the pass, and as a
   semi-transparent overlay it must never poison the depth buffer for
   anything else. Its `gl_FragDepth` is written explicitly (not the
   rasterizer's default interpolated depth), computed from the real
   ray-plane intersection point re-projected through the real `viewProj`
   matrix — this is what makes the depth **test** (read-only) correct.
7. **Blending:** standard alpha blending
   (`src/color * srcAlpha + dst * (1 - srcAlpha)`), so the grid composites
   naturally over both the dark "Scene" clear color and any opaque geometry
   already drawn.
8. **No new asset/texture/mesh of any kind.** Zero `*.gta` involvement, zero
   `Mesh`/`Buffer` vertex data — the vertex shader synthesizes a
   full-screen triangle purely from `gl_VertexIndex` (the standard,
   zero-vertex-buffer "full-screen triangle" trick), exactly matching Step
   1's "no 3D model with an image texture" requirement.
9. **Selection/picking:** requires no code at all — the grid is drawn via a
   raw `vkCmdDraw` call from `SceneGridRenderer`, entirely outside
   `Renderer::Submit()`/`RenderSystem`/`Registry`. There is no `Entity`,
   no `MeshRenderer`, nothing for a future ray-cast/selection system to
   ever find. This requirement is satisfied structurally, by construction,
   not by an exclusion check anyone has to remember to add later.

### File-by-file map of every change this campaign makes

New files:
- `src/Editor/SceneGridMath.h` / `.cpp` (Phase 1)
- `tests/Editor/SceneGridMathTests.cpp` (Phase 1)
- `src/Shaders/SceneGrid.vert` / `SceneGrid.frag` (Phase 2)
- `src/Editor/SceneGridRenderer.h` / `.cpp` (Phase 3)

Modified files:
- `CMakeLists.txt` — new sources under the existing `if(GTE_ENABLE_EDITOR)`
  `target_sources()` block (Phase 1 + Phase 3), new `gte_add_shader()` calls
  under a new `if(GTE_ENABLE_EDITOR)` block (Phase 2).
- `tests/CMakeLists.txt` — new test source under the existing
  `if(GTE_ENABLE_EDITOR)` list (Phase 1).
- `src/Editor/EditorLayer.h` — one new pure-virtual method,
  `RenderSceneGrid()` (Phase 4).
- `src/Editor/NullEditorLayer.cpp` — trivial no-op override (Phase 4).
- `src/Editor/ImGuiEditorLayer.cpp` — real override, owns a
  `SceneGridRenderer m_sceneGrid;` member (Phase 4).
- `src/Application/RenderPasses.h` / `.cpp` — `AddSceneViewPass()` gains one
  new, defaulted `recordSceneOverlay` parameter (Phase 4).
- `src/Application/Application.cpp` — the existing `AddSceneViewPass(...)`
  call site supplies the new callback (Phase 4).

`Game`, `Renderer`, `RenderGraph`, `ECS`, and every asset-pipeline file are
**never** touched by this campaign — this is a purely additive, Editor-only,
GPU-visual feature.

### Why this order specifically

Each phase is a strictly-growing, independently-compilable slice:

1. Phase 1 produces pure C++ math with real unit tests — verifiable
   correctness (hand-computed expected values) *before* any GPU code exists,
   the same "prove the algorithm on the CPU first" discipline this engine
   already used for GPU vertex skinning (`GpuSkinningValidation.h` — "the
   CPU path is the permanent oracle").
2. Phase 2 produces the GLSL mirror of that exact algorithm, compiled
   in isolation (`glslc` catches syntax errors immediately), still unused by
   any C++ code — a clean compile checkpoint with zero risk of touching
   live rendering.
3. Phase 3 produces the pipeline-owning class that CAN draw the grid, fully
   built and compilable, but still not called from anywhere real — another
   clean, zero-risk compile checkpoint.
4. Phase 4 is the one and only phase that changes real, already-running
   per-frame behavior — by the time it happens, the shader math (Phase 1),
   the shader itself (Phase 2), and the pipeline object (Phase 3) are all
   already known-good in isolation, so Phase 4 is "just" wiring, minimizing
   the chance of a mistake in the riskiest part of the change.
5. Phase 5 is tuning/hardening/final full regression — by design, real
   code changes only (constant tuning, edge-case guards), never a
   "verification-only, no code" phase.
