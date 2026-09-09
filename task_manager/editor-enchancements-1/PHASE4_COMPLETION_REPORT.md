# PHASE4_COMPLETION_REPORT — Wiring The Grid Into The Real Frame

> Parent: `PHASE0_MASTER_STRATEGY.md`. Implements `PHASE4_RENDERGRAPH_INTEGRATION.md`,
> which itself depends on `PHASE3_SCENE_GRID_RENDERER.md` (completed — see
> `PHASE3_COMPLETION_REPORT.md`).

## What was done

Implemented Phase 4 in full, exactly as specified in
`PHASE4_RENDERGRAPH_INTEGRATION.md` (Sections 3.1–3.8): wired the already-built
`SceneGridRenderer` (Phase 3) into the real, per-frame `"SceneView"` RenderGraph
pass, via a new `recordSceneOverlay` callback parameter threaded through
`AddSceneViewPass()`, invoked from inside that pass's `execute` lambda
immediately after `Game::Render()`'s own draws finish — still inside the same
`vkCmdBeginRendering`/`vkCmdEndRendering` bracket that pass opened, exactly as
Locked Design Decision #5 requires. **No brand-new RenderGraph pass was
created** — this was the whole point of the phase's own "Situation/Problem"
section, and its reasoning (a separate `"SceneGrid"` pass writing the same
`sceneViewTarget` handle a second time would be a real, unbarriered
write-after-write hazard per `RenderGraphBarrierPlanner::RequiresBarrier()`'s
own `!(previous == next)` logic — see `PHASE0_MASTER_STRATEGY.md`'s "Step 2")
was re-verified against the real, current source before writing any code (see
"Pre-flight verification" below).

### Modified files

- **`src/Editor/EditorLayer.h`** — added one new pure-virtual method,
  `RenderSceneGrid(Renderer&, VkCommandBuffer, const Mat4&)`, immediately after
  the existing `FinalizeBlurValidationForSampling()` declaration (same
  "per-frame Scene-view-adjacent GPU work" neighborhood), with the exact doc
  comment specified in Section 3.1. No new `#include` was needed — `Mat4.h` was
  already included and `VkCommandBuffer` was already visible transitively (via
  `RenderGraphTypes.h`, already used by `AddBlurValidationPass()`'s own
  `VkExtent2D` parameter).
- **`src/Editor/NullEditorLayer.cpp`** — added the trivial no-op override,
  `void RenderSceneGrid(Renderer&, VkCommandBuffer, const Mat4&) override { }`,
  right next to `FinalizeBlurValidationForSampling`'s own no-op.
- **`src/Editor/ImGuiEditorLayer.cpp`** — added `#include "SceneGridRenderer.h"`
  next to the existing `#include "ComputeBlurValidation.h"`; added a new
  `SceneGridRenderer m_sceneGrid;` member right next to the existing
  `EditorCamera m_sceneCamera;` member; added the real override right next to
  `FinalizeBlurValidationForSampling()`'s own override, calling straight into
  `m_sceneGrid.Draw(renderer, cmd, sceneViewProjection)`. The one stray `#`
  typo the phase document's own pasted comment snippet contained (`# sceneTarget
  != nullptr` instead of `// sceneTarget != nullptr`) was fixed during
  transcription, exactly as the document's own trailing note instructs. No
  destructor change was needed — re-verified directly against the real,
  current `~ImGuiEditorLayer()` body (confirmed it does **not** manually reset
  `m_blurValidation`, the closest existing precedent), confirming `m_sceneGrid`
  (a plain, non-pointer member with its own correct RAII destructor via
  `SceneGridRenderer::~SceneGridRenderer()` → `Reset()`) needs no special
  cleanup either.
- **`src/Application/RenderPasses.h`** — `AddSceneViewPass()` gained one new,
  defaulted parameter,
  `const std::function<void(VkCommandBuffer, const Mat4&)>& recordSceneOverlay = {}`,
  with its doc comment extended to describe the new parameter (mirroring
  `AddPresentPass()`'s own `recordImGui` doc-comment style, exactly as the
  phase document specifies). No new `#include` was needed — `<functional>` was
  already included for the pre-existing `recordImGui` parameter on
  `AddPresentPass()`.
- **`src/Application/RenderPasses.cpp`** — `AddSceneViewPass()`'s body now
  captures `recordSceneOverlay` into its `execute` lambda and, immediately
  after `renderer.EndGraphPassRecording()` (i.e. still inside the pass's own
  open dynamic-rendering bracket), calls
  `if (recordSceneOverlay) { recordSceneOverlay(ctx.cmd, sceneViewProjection); }`.
  Per the phase document's own explicit instruction, `recordSceneOverlay` is
  **not** wired through `ctx.recordDraw`/`DrawStats` — mirroring
  `AddPresentPass()`'s own `recordImGui` precedent, which is likewise invisible
  to the Profiler's draw-call/triangle counters today.
- **`src/Application/Application.cpp`** — at the real `AddSceneViewPass(...)`
  call site (inside the offscreen `RenderGraph::Execute()` `build` lambda,
  inside the `if (sceneTarget != nullptr)` block), added a new
  `const std::function<void(VkCommandBuffer, const Mat4&)> recordSceneGrid`
  local that closes over `this` and calls
  `m_editorLayer->RenderSceneGrid(m_renderer, cmd, viewProj)`, then passed it
  as `AddSceneViewPass(...)`'s new trailing argument. Everything after this
  call site (`FinalizeRenderTextureForExternalSampling(...)`, the compute-blur
  validation pass, etc.) is unaffected, exactly as the phase document predicts
  — the grid is already fully composited into `sceneTarget` by the time any of
  that runs.

### Pre-flight verification (required by the task prompt)

Before editing anything, the exact current signatures/bodies of every file
this phase touches were re-opened and cross-checked against
`PHASE4_RENDERGRAPH_INTEGRATION.md`'s own quoted before/after snippets:

- `src/Editor/EditorLayer.h` (the `IEditorLayer` interface, its
  `FinalizeBlurValidationForSampling()` neighborhood) — matched exactly.
- `src/Editor/NullEditorLayer.cpp` — matched exactly.
- `src/Editor/ImGuiEditorLayer.cpp` — `SceneViewProjection()`, the
  `AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()` overrides,
  the `m_sceneCamera` member neighborhood, and the real `~ImGuiEditorLayer()`
  destructor body all matched exactly, confirming no drift since
  `PHASE0_DOUBLE_CHECK_REPORT.md`'s own verification pass.
- `src/Application/RenderPasses.h`/`.cpp` — `AddSceneViewPass()`'s exact
  current signature/body matched the phase document's quoted "before" snippet
  verbatim (`gpuSkinningOutputBuffers` defaulted to `{}`, no
  `recordSceneOverlay` parameter yet).
- `src/Application/Application.cpp`'s `Run()` — the exact call site (inside
  the `if (sceneTarget != nullptr)` block, right after
  `b.ImportTexture("SceneView", ...)`) matched the phase document's quoted
  "before" snippet verbatim.

**Conclusion: zero deviations were needed beyond the one pre-flagged stray-`#`
typo fix** (the same "proofread the pasted comment" discipline every prior
phase in this campaign has already established) — every signature the phase
document assumes matches the real, current source exactly, so this phase's
edits were applied byte-for-byte as specified.

## Deviations from the phase document

**None**, other than the one deliberately pre-flagged fix the document itself
instructs (the stray `#` → `//` typo in `ImGuiEditorLayer.cpp`'s new override's
own comment).

## Build commands run and their results

### 1. Fast compile check — `gte_core` (GTE_ENABLE_EDITOR=ON, the default `build/` tree)

```
cmake --build build --target gte_core
```
Result: **succeeded** — `RenderPasses.cpp`, `Application.cpp`, and
`ImGuiEditorLayer.cpp` all recompiled cleanly and `libgte_core.a` relinked with
no errors/warnings beyond the pre-existing, unrelated KTX-Software
`git describe` version-fallback warning.

### 2. Full build — `GreatTamanaEngine` executable, `GTE_ENABLE_EDITOR=ON`

```
cmake --build build --target GreatTamanaEngine
```
(Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`)

Result: **succeeded** — `GreatTamanaEngine.exe` linked successfully, with
`SceneGrid.vert.spv`/`SceneGrid.frag.spv` (already compiled by Phase 2) staged
next to it as before.

### 3. Live visual check (Definition of Done requirement)

The built engine was launched via `run_app_background` and queried via
`gte_send_request("/get_swapchain")`.

**Important, pre-existing (not introduced by this phase) discovery during this
check:** the Editor's default, freshly-constructed `EditorCamera` starts
**exactly level with the horizon** (`position = (0, 0, -5)`, identity
rotation, matching `Game`'s own default camera per `EditorCamera.h`'s own class
comment) — i.e. its ray is parallel to the grid's `Y = 0` plane everywhere on
screen. This is precisely the degenerate case
`SceneGridMathTest.PlaneHit_LevelCameraNeverHitsThePlaneAtAnyNdcX` (Phase 1)
already covers and expects: `ComputeGridPlaneHit()` correctly reports no valid
hit, so the very first screenshot after a fresh launch, with **zero** user
camera interaction, legitimately shows no grid at all. This is expected,
correct behavior per the shader's own ray-plane math — not a Phase 4 defect —
but it meant a plain "launch and screenshot" check alone could not visually
prove the grid renders.

To get a real, camera-angled view for the screenshot (there is no network
endpoint to drive Scene-view mouse orbiting, since `NetworkRoutes` handlers
must stay pure per `AGENTS.md`'s "Networking" rules), two small, deliberately
**temporary** source edits were made, screenshotted, and then **fully
reverted** before final build/commit:

1. `EditorCamera`'s default constructor was temporarily changed to start at a
   downward-pitched angle (`position = (0, 5, -10)`, `pitch = 20°`) instead of
   level, purely so the Scene view's initial camera ray actually intersects
   the ground plane.
2. `Game::Render()` was temporarily given a one-shot static-guarded block that
   spawns one `Cube` primitive at `(0, 0.5, 3)` (scaled `(2, 1, 2)`), purely to
   provide real opaque geometry to verify depth occlusion against.

With both temporary edits in place, `cmake --build build --target
GreatTamanaEngine` was re-run, the app was launched, and two screenshots were
taken via `gte_send_request("/get_swapchain")`:

- **Screenshot 1 (grid alone, no geometry):** clearly shows the grey minor/
  major grid lines converging toward the horizon, a **red** horizontal line
  through the world X axis, and a **blue** vertical line through the world Z
  axis, all correctly fading to transparent with distance — visually matching
  every element Locked Design Decisions #1/#3/#4/#5 require.
- **Screenshot 2 (grid + spawned Cube):** the spawned cube sits visibly ON the
  grid, and the grid lines directly underneath/behind it are correctly
  occluded (not drawn on top of the cube) — confirming the depth **test**
  (`VK_COMPARE_OP_LESS`, read-only per Locked Design Decision #6) works
  correctly against real scene geometry `Game::Render()` drew in the same
  pass, exactly as required.

Both temporary edits (`EditorCamera.h`'s constructor, `Game.cpp`'s test-cube
block) were then **fully reverted** — confirmed via `git status` showing
`src/Editor/EditorCamera.h` and `src/Game/Game.cpp` back to fully unmodified
(matching HEAD) before the final build/test/commit steps below. The app was
re-launched one final time against the clean, reverted build and re-screenshotted,
confirming it is back to the original default (level-camera, no-grid-visible-
yet) behavior with zero leftover test artifacts.

### 4. Build with `GTE_ENABLE_EDITOR=OFF`

A separate, throwaway configure+build tree was used (`build_no_editor/`,
deleted again afterward — never committed):

```
cmake -S . -B build_no_editor -G Ninja -DGTE_ENABLE_EDITOR=OFF
cmake --build build_no_editor --target GreatTamanaEngine
```

Result: **succeeded, zero errors.** Confirmed `NullEditorLayer.cpp` was
compiled (not `ImGuiEditorLayer.cpp`/`SceneGridRenderer.cpp`), and confirmed no
`SceneGrid.vert.spv`/`SceneGrid.frag.spv` shader compile step ran at all in
this configuration (only `Triangle`/`Mesh`/`TexturedMesh`/
`SkinVerticesPositionNormal(Uv)`/`MeshPreview` shaders were compiled) — this
campaign's shaders stay correctly gated behind `GTE_ENABLE_EDITOR` alone, with
zero reference to any Phase 1–4 campaign file in this build configuration.

### 5. Full regression test suite

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build
ctest -C Debug --output-on-failure
```

Result: **100% passed — 1066 of 1067 tests passed, 1 pre-existing
machine-gated smoke test skipped** (`PmxLoaderRealModelSmokeTest.
LoadsAnMmdModelIfPresentOnThisMachine` — this machine has no local MMD model
fixture; a pre-existing, unrelated skip, not caused by this phase). Zero
regressions.

## What was visually observed (plain-text description)

**Screenshot 1 — grid only (default "Scene" panel, camera temporarily
pitched down for this check):** a dark, near-black viewport background with a
regular grey grid of horizontal/vertical lines receding toward a horizon line
roughly in the upper third of the panel, becoming visibly fainter (fading
toward transparent) the farther they are from the camera. A bright **red**
horizontal line runs through the middle of the grid (the world X axis) and a
bright **blue** line runs vertically through the same crossing point (the
world Z axis), both clearly distinguishable from the plain grey grid lines.
The grid pattern shows finer ("minor," 1-unit) lines near the camera and
coarser ("major," 10-unit) lines further out, consistent with the two-LOD
design.

**Screenshot 2 — grid with a spawned Cube:** identical grid/axis-line
appearance as above, but with one grey, lit ("clay-shaded") cube sitting on
top of the grid a short distance in front of the camera. The grid lines that
would otherwise pass directly underneath/behind the cube are correctly
missing exactly where the cube occupies that screen area — the cube visibly
and correctly occludes the grid, proving the depth test works as designed.

## Definition of Done — checklist against Section 3.8

- [x] Full engine build (`GreatTamanaEngine` target, `GTE_ENABLE_EDITOR=ON`)
      succeeds.
- [x] Launched the built engine and confirmed via `gte_send_request
      ("/get_swapchain")` that the "Scene" panel shows a grey grid with
      red/blue axis lines through the origin, fading with distance, and that
      spawning a primitive visibly occludes the grid lines directly
      underneath it (see "Live visual check" above for the temporary
      camera-angle adjustment needed to actually see this, since the default
      Scene camera starts level with the horizon).
- [x] Built with `GTE_ENABLE_EDITOR=OFF` and confirmed it compiles/links
      cleanly with zero reference to any new file from this campaign in that
      configuration.
- [x] Ran the full existing automated test suite (`ctest`) and confirmed zero
      regressions (1066/1067 passed, 1 pre-existing machine-gated skip).

## A note on the default Scene camera (not a defect, not fixed here)

The Editor's Scene-view camera starts perfectly level with the horizon by
design (`EditorCamera.h`'s own class comment — deliberately matching `Game`'s
own default camera so "Game" is never blank either). This means a genuinely
fresh Editor session shows no grid at all until the user orbits/pans the
camera even slightly downward (a single middle-mouse pan or right-mouse-drag
look is enough). This is **not** a Phase 4 defect — it is the correct,
by-design output of the ray-plane math specified all the way back in Phase 1
(`ComputeGridPlaneHit()`'s own "a ray parallel to the plane has no valid hit"
contract, directly tested by `PlaneHit_LevelCameraNeverHitsThePlaneAtAnyNdcX`)
— but it is worth calling out explicitly here since it was the reason a plain
"launch and screenshot with zero interaction" check would have looked like a
failure. `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decisions never
required the grid to be visible from the exact default camera pose, only that
it renders correctly whenever the camera's ray actually crosses the ground
plane — which this phase's live check now positively confirms it does. No
code change was made to address this (it is out of this phase's scope, and
changing the shared default camera pose would be a behavior change with no
basis in this campaign's own locked decisions) — noted here purely as a
transparent methodology note for whoever reads this report next.

## Git

Staged and committed as a single change: `src/Editor/EditorLayer.h`,
`src/Editor/NullEditorLayer.cpp`, `src/Editor/ImGuiEditorLayer.cpp`,
`src/Application/RenderPasses.h`, `src/Application/RenderPasses.cpp`,
`src/Application/Application.cpp`, and this report. No other file changed —
`src/Editor/EditorCamera.h` and `src/Game/Game.cpp` were temporarily modified
purely for the live visual check described above and are confirmed, via
`git status`, to be back to their original (HEAD) state, so neither appears in
this commit.
