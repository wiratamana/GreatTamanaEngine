# PHASE13 — Move Per-Frame Orchestration Into `Core`

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 12 (`Core`'s skeleton, INCLUDING
its `IEditorLayer*` hook from Locked Design Decision #8, must already
exist). **The design doc calls this out by name as "genuine surgery, not a
mechanical cut-and-paste" — treat it with real care.**

## Step 1: The Goal

`Core::Update()`, `Core::BuildFrame()`, `Core::Present()` contain the REAL
per-frame orchestration logic that lives inside `Application::Run()`'s body
today: offscreen regime, present regime, GPU-skinning dispatch requests,
per-view data, Atmosphere passes, GPU-driven batch culling readback.
`Application::Run()` afterward keeps calling directly into its own
`m_editorLayer` for every UI-building/input-routing concern (Locked Design
Decision #8's "stays host-level" list), interleaved AROUND its calls into
`m_core.Update()`/`BuildFrame()`/`Present()` — it is NOT reduced to the
design doc's illustrative 3-line loop sketch verbatim (that sketch is a
simplification; see Step 3.5 below for why).

## Step 2: The Situation / The Problem

Read `src/Application/Application.cpp`'s `Run()` method (and every private
method it calls) in FULL, current state, before writing anything — this is
non-negotiable given the design doc's own warning. Identify:
- Exactly what "offscreen regime" vs "present regime" means in the real,
  current code (find the actual branch/flag controlling this).
- Every place `FrameDebuggerCaptureContext*` is threaded through this
  orchestration (must keep working as a nullable pointer/reference — this
  is `Core`'s "one two-way wrinkle", design doc Section 5.4 — never becomes
  a hard dependency).
- **Every one of `Application::Run()`'s own `m_editorLayer->...()` call
  sites** — this strategy's own double-check pass found roughly 30 of them,
  spanning event handling, playback-pause checks, per-frame UI building,
  render-graph pass declaration (blur/GBuffer validation, scene grid,
  composited-texture handoff), and Frame-Debugger/automation-bridge
  plumbing. Classify EVERY ONE of them against PHASE0's Locked Design
  Decision #8 two-bucket list (re-read it now) before moving anything —
  do not assume the list in Decision #8 is exhaustive; if a call site is
  found that isn't named there, classify it yourself using the same rule
  ("does this genuinely participate in building this frame's Render Graph
  passes, or is it UI-building/input-routing/automation?") and document the
  new classification in the completion report.
- Whether any OTHER in-flight work (check `git_status` and recent commit
  history/other `task_manager/*` folders for anything ALSO touching
  `Application.cpp`'s orchestration body right now) overlaps this phase —
  if so, flag it via `ask_questions` before proceeding, per the design
  doc's own explicit warning (Section 11) that this phase must be
  sequenced alongside, not independently of, any such overlapping work.

## Step 3: The Plan

1. Confirm no overlapping in-flight campaign is currently mid-flight on
   `Application.cpp` (check `git_status`, check other `task_manager/*`
   folders' own most recent phase files for anything mentioning
   `Application.cpp` as still-open work). If found, use `ask_questions` to
   decide whether to proceed, pause, or coordinate.
2. Move the ACTUAL BODY of the main-loop's per-frame RENDER-GRAPH-BUILDING
   work into `Core`'s three methods, preserving exact behavior:
   - `Core::Update(const InputFrame& input, float deltaTime)` — whatever
     `Application::Run()` currently does for input processing, `Time`
     advancement, `Game::Update()` dispatch (respecting the existing
     Pause/Resume/Step semantics documented in `AGENTS.md`'s "Time and
     Playback Pause" section — do not regress this). Playback pause STATE
     itself (`IsPlaybackPaused()`/`TryConsumeStepRequest()`/
     `NotifyFrameDebuggerStepConsumed()`) stays a call `Application` makes
     directly against `m_editorLayer` BEFORE calling `m_core.Update()`,
     passing the already-resolved paused/step booleans IN as plain
     arguments (extend `InputFrame`, or add a small separate parameter) —
     these three methods are NOT part of the render-graph-building subset
     (Locked Design Decision #8) and must never be called through `Core`'s
     `m_editorLayer` hook.
   - `Core::BuildFrame()` — Render Graph pass registration/building for the
     current frame (Atmosphere passes, GPU-skinning dispatch requests,
     per-view data, GPU-driven batch culling readback, opaque/sky/
     transparent passes) — the offscreen-vs-present regime logic included.
     This is ALSO where every render-graph-frame-building `IEditorLayer`
     call site moves to, called through `Core`'s own `m_editorLayer` hook
     (Phase 12), each wrapped in the exact same
     `if (m_editorLayer != nullptr) { ... }` null-check shape
     `FrameDebuggerCaptureContext*` already uses today: `GameViewTarget()`,
     `SceneViewTarget()`, `SceneViewProjection()`,
     `SceneViewCameraWorldPosition()`, `RenderSceneGrid()`,
     `AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()`,
     `AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`,
     `SetGameViewCompositedTexture()`/`SetSceneViewCompositedTexture()`,
     `PrepareFrameDebuggerCaptureContext()`,
     `ConsumePendingFrameDebuggerReplayRequest()`. A `nullptr` hook must
     make every one of these branches a safe, silent no-op — mirroring
     exactly what a release/Player build already gets from
     `NullEditorLayer`'s own all-no-op methods, just reached through one
     extra null-check layer instead of a guaranteed-non-null pointer.
   - `Core::Present()` — the actual present/submit call.
3. Everything else `Application::Run()` currently calls on `m_editorLayer`
   — `ProcessEvent()`, `OnWindowResized()`, `WantsCaptureMouse()`/
   `WantsCaptureKeyboard()`, `NewFrame()`, `BuildUI()`, `Render()`,
   `RenderPlatformWindows()`, `WantsExit()`, `ActivateTab()`,
   `ImportExternalAssetIntoProject()`, `SpawnGpuDrivenTestBatch()`, every
   Frame-Debugger-UI-state method (`FrameDebuggerOpenWindow()`,
   `FrameDebuggerSetEnabled()`, `FrameDebuggerCaptureNow()`,
   `FrameDebuggerSelectEvent()`, `FrameDebuggerSetChannel()`,
   `FrameDebuggerSetLevels()`, `FrameDebuggerGetState()`) — STAYS exactly
   where it is today: a direct call from `Application::Run()`'s own body
   against its own `m_editorLayer` member (never routed through `Core`).
   `Application::Run()`'s own loop shape after this phase interleaves these
   calls around its now-much-shorter calls into `Core`:
   ```cpp
   while (running) {
       PumpSdlEvents(); // still forwards SDL_Event to m_editorLayer->ProcessEvent()
       InputFrame input = m_eventTranslator.Translate(sdlEvents);
       // Host-level UI/input-routing calls against m_editorLayer directly -
       // NEVER moved into Core (Locked Design Decision #8):
       m_editorLayer->NewFrame();
       const bool paused = m_editorLayer->IsPlaybackPaused();
       const bool stepRequested = m_editorLayer->TryConsumeStepRequest();
       // ... automation-bridge pumps, ActivateTab/asset-import/GPU-driven-
       // batch-spawn command handling, exactly as today ...

       m_core.Update(input, deltaTime); // paused/stepRequested threaded in
       m_core.BuildFrame();             // Editor render-graph hooks fire
                                         // INSIDE here, via Core's own
                                         // m_editorLayer pointer (Phase 12)
       m_core.Present();

       m_editorLayer->BuildUI(m_core.GetGame(), m_core.GetRenderer(),
           m_core.GetRenderGraph(), /* ... */);
       if (m_editorLayer->WantsExit()) { running = false; }
       // m_editorLayer->Render(cmd) already happens via Renderer::Present()'s
       // recordExtra hook, as today - confirm exact current wiring, do not
       // assume it needs to move.
       m_editorLayer->RenderPlatformWindows();
   }
   ```
   This is illustrative, not literal — confirm the EXACT current ordering
   of every one of these calls by reading `Application::Run()`'s real body,
   and preserve that exact ordering relative to `Core`'s three calls. The
   design doc's own Section 6 code sketch (`PumpSdlEvents(); ...
   m_core.Update(...); m_core.BuildFrame(); m_core.Present();`) is a
   DELIBERATE SIMPLIFICATION of this fuller shape, not a literal target to
   force this file into — do not delete real, still-needed `m_editorLayer`
   calls just to make the loop match that 3-line sketch exactly.
4. Every automation bridge (`EngineCommandBridge`, `FrameCaptureBridge`,
   `EditorUiCommandBridge`, `FrameDebuggerCommandBridge`,
   `AssetImportCommandBridge`, embedded `NetworkServer`) that currently
   reaches into `Application`'s now-moved members must be updated to reach
   through `m_core.GetXxx()` accessors instead — do NOT move the bridges
   themselves yet (Phase 16 does that), just fix their access path.
5. Compile-check: incremental build. Live, careful smoke check — this is
   the phase most likely to introduce a subtle behavior regression, so
   check thoroughly: `run_app_background`, `gte_send_request` screenshots
   of BOTH Game View and Scene View, confirm Atmosphere rendering, GPU
   skinning, and GPU-driven batch culling all still visually match
   pre-campaign baseline screenshots (take a fresh baseline screenshot
   NOW, before this phase, if one doesn't already exist, so you have
   something concrete to diff against). Exercise Play/Pause/Step via the
   Editor's playback controls and confirm `Time`'s freeze semantics still
   work. Exercise the "Show Compute Blur (debug)"/"Show GBuffer Validation
   (debug)" toggles specifically — these are the two features whose
   render-graph pass declaration now flows through `Core`'s own
   `m_editorLayer` hook rather than `Application` calling it directly, so
   they are the most likely place a mis-wired hook would show up as a
   regression. Pull `GET /get_logs` and confirm no new error-level entries.

## Files Touched

- `src/Core/Core.cpp` (real method bodies, including the new
  render-graph-frame-building `m_editorLayer` call sites)
- `src/Application/Application.cpp` (shrink; keeps every host-level
  `m_editorLayer` call site directly)
- Every automation bridge file needing an access-path fix (confirmed
  during execution)

## Definition of Done

- `Application::Run()`'s loop body matches the fuller shape in Step 3.3 —
  every host-level `IEditorLayer` call site (Locked Design Decision #8's
  second list) still lives directly on `Application`, interleaved around
  three much-shorter calls into `Core`.
- Every render-graph-frame-building `IEditorLayer` call site (Locked Design
  Decision #8's first list) now lives inside `Core::BuildFrame()`, reached
  through `Core`'s own null-checked `m_editorLayer` hook (Phase 12), never
  directly on `Application` anymore.
- Live visual smoke check confirms zero rendering regression across Game
  View, Scene View, Atmosphere, GPU Skinning, GPU-driven batching, Compute
  Blur Validation, GBuffer Validation.
- Playback Pause/Resume/Step confirmed still working.
- `PHASE13_COMPLETION_REPORT.md` (including the before/after screenshot
  comparison description, and an explicit accounting of every
  `IEditorLayer` call site's new home) + git commit.

## Out of Scope

Do not move `Window`/`SdlContext` yet (Phase 14). Do not build `EditorHost`
yet (Phase 15) — `Application` remains the composition root for one more
phase, just with a much thinner `Run()` body now.
