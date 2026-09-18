# PHASE2 COMPLETION REPORT — Render Opaque/Sky Split + Transparent Stub

_Child of `PHASE0_MASTER_STRATEGY.md`. Campaign: `render-pass-1`, branch
`feature/render-pass-impl`._

## Summary

Implemented PHASE2 exactly per
`PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`: the old, monolithic
`"GameView"` pass (which drew every mesh AND hand-fused the atmosphere sky
background into the same `vkCmdBeginRendering`/`vkCmdEndRendering` bracket) is
now THREE real, separately-declared, individually-inspectable Render Graph
passes, all writing the same imported `gameViewTarget` texture handle, back to
back, in the same `Application::Run()` `build` lambda:

1. **`"RenderOpaque"`** (renamed from `AddGameViewPass()` → `AddRenderOpaquePass()`)
   — clears color+depth, calls `Game::Render()` exactly as before. No longer
   draws the sky at all.
2. **`"DrawSkyBackground"`** (brand-new `AddDrawSkyBackgroundPass()`) — LOADs
   both attachments (never clears), invokes the same
   `recordSkyBackground`/`AtmosphereSkyBackgroundRenderer::Draw()` callback
   that used to run inline inside the old pass, relying on
   `AtmosphereSkyBackgroundRenderer`'s own pre-existing `EQUAL`-depth-test
   pipeline to only paint pixels `"RenderOpaque"` didn't already cover. A true
   no-op (declares nothing) when `recordSkyBackground` is empty.
3. **`"RenderTransparent"`** (brand-new `AddRenderTransparentPass()`) — a
   genuine, permanent, always-empty scaffold call site wired into
   `Application::Run()` between the Sky Background pass and the Aerial
   Perspective composite pass, backed by a new
   `RenderSystem::CollectTransparentRenderables()` that always returns `{}`
   today (no `isTransparent`/`renderQueue` concept exists on `MeshRenderer`
   yet).

Both `AddRenderOpaquePass()` and `AddDrawSkyBackgroundPass()` are declared via
PHASE1's new `RenderGraphBuilder::AddRenderPass()` chokepoint
(`PassKind::Graphics`, `ViewScope::GameView`, `RenderPassCategory::General` —
`General` is correct/expected here, only Atmosphere LUT passes get
`AtmosphereLut` in PHASE3). `AddRenderTransparentPass()` never calls
`AddRenderPass()` at all today (a true no-op, mirroring
`AddGpuSkinningPasses()`'s own "declare nothing when there's nothing to do"
precedent) — there is no pass to categorize yet.

Per PHASE2's own explicit scope: `AddSceneViewPass()` ("SceneView") was left
COMPLETELY UNTOUCHED — Scene View keeps its sky background hand-fused inline
exactly as before, since it is permanently out of scope for the whole Frame
Debugger campaign. The Frame Debugger's own tree-building logic
(`FrameDebuggerData.cpp`) was also left untouched, per PHASE2's own "What We
Will NOT Do" — it still hardcodes a search for the literal pass name
`"GameView"`, which no longer exists after this phase; this is EXPECTED and
temporarily leaves the Frame Debugger's tree slightly wrong/incomplete for
`"RenderOpaque"`/`"DrawSkyBackground"` specifically, until PHASE4 fixes this
generically.

## What Changed

### 1. `src/Game/RenderSystem.h` / `.cpp`

- Added `static std::vector<DrawCommand> CollectTransparentRenderables(Registry&
  registry)` — the transparency-equivalent of `CollectRenderables()`, always
  returning `{}` today (no filtering concept exists on `MeshRenderer` yet).
  `registry`'s parameter name is deliberately kept for future signature
  stability.

### 2. `src/Application/RenderPasses.h`

- `AddGameViewPass()` → **renamed** to `AddRenderOpaquePass()`, its
  `recordSkyBackground` parameter **removed entirely** (sky drawing is no
  longer this function's concern).
- Added `AddDrawSkyBackgroundPass()` declaration (renderer, gameViewTarget,
  recordSkyBackground, optional frameDebuggerCapture).
- Added `AddRenderTransparentPass()` declaration (builder, game, renderer,
  gameViewTarget, aspectWidthOverHeight).
- Updated every present-tense doc-comment reference to the old
  `AddGameViewPass()` name inside THIS file to `AddRenderOpaquePass()` (the
  historical "RENAMED from AddGameViewPass()" rename-history comments were
  deliberately left as-is, mirroring PHASE1's own established precedent for
  narrating history truthfully).

### 3. `src/Application/RenderPasses.cpp`

- `AddGameViewPass()`'s body split into `AddRenderOpaquePass()` (declared via
  `builder.AddRenderPass("RenderOpaque", rg::PassKind::Graphics,
  rg::ViewScope::GameView, rg::RenderPassCategory::General, ...)`, sky-drawing
  code entirely removed) and the new `AddDrawSkyBackgroundPass()` (declared via
  `builder.AddRenderPass("DrawSkyBackground", ...)`, no clear values on either
  attachment, invokes `recordSkyBackground(ctx.cmd)` then, Editor builds only,
  `frameDebuggerCapture->RecordSkyBackgroundDraw(...)` — the exact same
  temporary bridge call the old code made, now living in its own real pass
  instead of hand-fused into the mesh-drawing one).
- Added `AddRenderTransparentPass()`: calls
  `RenderSystem::CollectTransparentRenderables(game.GetRegistry())` (no new
  forwarding method needed on `Game` — `Game::GetRegistry()` was already
  public), returns immediately (declaring nothing) since that list is always
  empty today. `builder`/`renderer`/`gameViewTarget`/`aspectWidthOverHeight`
  are `(void)`-cast in the always-taken early-return path to avoid unused-
  parameter warnings, since they only become meaningful once a future
  transparency campaign gives this function a real body.

### 4. `src/Application/Application.cpp`

- The single `AddGameViewPass(...)` call site (inside the
  `if (gameTarget != nullptr) { ... }` block) is now three calls in sequence:
  `AddRenderOpaquePass(...)` → `outputs.push_back(h)` → `AddDrawSkyBackgroundPass(...)`
  → `AddRenderTransparentPass(...)`, all writing the same imported `h`
  (`"GameView"` — the TEXTURE resource name, unrelated to and unaffected by
  this campaign's pass-name changes). `outputs.push_back(h)` still only
  happens once, per PHASE2's own note about reachability culling.
- Updated the `Profiling::GpuPass::GameView` stats-aggregation call site (PHASE2's
  own Step 3.4): it used to read a single `LastKnownStatsFor("GameView")`;
  now sums `LastKnownStatsFor("RenderOpaque")` +
  `LastKnownStatsFor("DrawSkyBackground")` + `LastKnownStatsFor("RenderTransparent")`
  via a new `rg::CombinePassGpuStats()` helper before handing the result to
  `SetGpuPassDrawStats()`/`SetGpuPassTiming()`. This is a real, confirmed FIX
  of a previously-documented gap (`docs/conventions/frame-debugger.md`'s
  `frame-debugger-8` section: "the parent `GameView` leaf's own aggregate
  'Draw Stats' row... does not count the sky's own raw `vkCmdDraw()` call") —
  the sky draw's own 1 draw call is now correctly included in the reported
  total.
- Updated two present-tense doc comments that named `AddGameViewPass()` to say
  `AddRenderOpaquePass()` instead.

### 5. `src/Renderer/RenderGraph/RenderGraphSnapshot.h` / `.cpp`

- Added `rg::CombinePassGpuStats(const std::vector<PassGpuStats>&)` — pure,
  Tier-1-testable. **Exact rule** (documented in the header, per PHASE2's own
  request to state it explicitly here):
  - `drawStats`: plain sum of every entry's `drawCallCount`/`triangleCount`.
  - `timing`: sums `milliseconds` across every entry whose status is
    `Present` (result is `Present` too, in that case). If NONE are `Present`,
    falls back to `Unsupported` if any entry is `Unsupported` (a permanent,
    device-level condition), otherwise `Absent` (today's common case — every
    pass's own timing is `Absent`, see `RenderGraph.h`'s "GPU TIMING NOTE").
  - An empty input list returns a default-constructed `PassGpuStats{}`.

### 6. Tests

- `tests/Game/RenderSystemTests.cpp`: added
  `CollectTransparentRenderablesIsAlwaysEmptyToday` — confirms the new method
  returns empty even for a scene with a real `MeshRenderer` entity, while that
  SAME entity still appears in `CollectRenderables()` (proving this is a
  genuine "nothing is transparent yet" no-op, not an accidental filter
  dropping the entity from both lists).
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`: added 7 new
  `CombinePassGpuStats*` tests covering: empty input, summed draw stats across
  3 entries (including an all-default "transparent" entry), summed
  milliseconds when all `Present`, an `Absent` entry correctly contributing
  zero when at least one other is `Present`, all-`Absent` → `Absent`,
  all-non-`Present`-with-one-`Unsupported` → `Unsupported`, and
  `Present` correctly taking priority over a mixed `Unsupported`+`Present`
  input.

## Verification

- Incremental compile: `cmake --build build --target GreatTamanaEngine` —
  clean build, zero errors/warnings.
- Incremental compile: `cmake --build build --target GreatTamanaEngineTests`
  — clean build.
- Ran the exact touched/added test suites directly:
  `GreatTamanaEngineTests.exe --gtest_filter=RenderSystemTest.*:RenderGraphSnapshotTest.*`
  — **31/31 passed** (12 `RenderSystemTest` + 19 `RenderGraphSnapshotTest`,
  including all 7 new `CombinePassGpuStats*` cases).
- Live runtime smoke test (`run_app_background` + `gte_send_request`):
  - `GET /get_game_view` with an empty scene (Camera + Directional Light only)
    showed the same correct sky gradient + horizon as before this phase.
  - `POST /instantiate_primitive` (a cube at `(0,0,-3)`) followed by
    `GET /get_game_view` showed the cube correctly occluding the sky behind
    it, sky correctly filling every pixel the cube didn't cover — confirming
    Opaque draws BEFORE Sky, and Sky's `EQUAL`-depth-test only paints
    untouched pixels, exactly as before this split.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` visually
    confirmed the Editor's "Render Graph" panel now lists **real, separate**
    `"RenderOpaque"` (1 draw, 12 tris) and `"DrawSkyBackground"` rows in the
    Offscreen Regime table, in that exact order — proving the split is real,
    not just a rename. `"RenderTransparent"` correctly does NOT appear in the
    table at all (confirming it declared zero passes, exactly the documented
    no-op behavior).
  - Cleaned up (`POST /delete_entity`) and stopped the app afterward.

## Deviations From The Phase Document

None in substance. Two minor judgment calls:

1. The phase document suggested "a new, small, PUBLIC forwarding method may
   be needed on `Game`" (mirroring `CountGameViewDrawCommandsThisFrame()`) for
   `AddRenderTransparentPass()` to reach the registry. This turned out to be
   unnecessary — `Game::GetRegistry()` is already public — so
   `AddRenderTransparentPass()` calls
   `RenderSystem::CollectTransparentRenderables(game.GetRegistry())` directly,
   with no new `Game` method added.
2. `CombinePassGpuStats()` was placed in `RenderGraphSnapshot.h`/`.cpp` (where
   `PassGpuStats` itself is already defined) rather than in
   `Application.cpp`'s own anonymous namespace — the phase document offered
   both as acceptable options ("or `RenderGraphSnapshot.h` if you judge it
   more broadly useful"); this placement keeps it directly Tier-1-testable
   alongside its sibling `BuildRenderGraphSnapshot()` with no need for a new,
   Application-specific test file.

A handful of OTHER files across the codebase (`AtmospherePassSequence.h`,
`ComputeBlurValidation.cpp`, `EditorLayer.h`, `FrameDebuggerCapture.h`,
`Game.h`, `Renderer.h`) still have present-tense doc comments naming the old
`AddGameViewPass()` — these are correctness-neutral (comments only, nothing
compiles against the old name) and were deliberately left untouched here to
keep this phase's blast radius narrow, exactly as PHASE0's own Phase Index
assigns broader documentation cleanup to PHASE6
(`PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md`). Flagging this
explicitly so PHASE6 knows to sweep these too.

## Definition of Done — Checklist

- [x] `"RenderOpaque"` and `"DrawSkyBackground"` are two real, separately
      named passes declared via `AddRenderPass()`, both writing the SAME
      `gameViewTarget` handle, in that order, the second one never clearing.
- [x] `AddRenderTransparentPass()` exists, is wired into `Application.cpp` in
      the correct position, and is a genuine no-op today (confirmed live: a
      scene with a real mesh entity shows NO `"RenderTransparent"` row in the
      Render Graph panel at all).
- [x] The rendered image is pixel-plausible-identical to before this phase
      (same sky gradient, same opaque-occludes-sky depth behavior) — verified
      live via `/get_game_view` before and after spawning a test cube.
- [x] `Profiling::GpuPass::GameView`'s stats now correctly include the sky
      draw's own draw call/triangle count via `CombinePassGpuStats()`.
- [x] Incremental compile succeeds; completion report + git commit follow.

## Handoff To PHASE3

`"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` are real,
declared, working passes. PHASE3
(`PHASE3_ATMOSPHERE_PASSES_MIGRATION.md`) can now migrate every Atmosphere
LUT/composite pass declaration onto the same `AddRenderPass()` chokepoint,
tagged `RenderPassCategory::AtmosphereLut`. PHASE4's Frame Debugger rework
still has an outstanding cleanup item flagged by this phase (per PHASE2's own
doc comment on `AddDrawSkyBackgroundPass()`): once the Frame Debugger
generically discovers `"DrawSkyBackground"` by name/category, the temporary
`FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` bridge call this
phase kept alive inside that pass's `execute` lambda can be deleted outright.
