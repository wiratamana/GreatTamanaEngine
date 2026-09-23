# PHASE13 — COMPLETION REPORT: Move Per-Frame Orchestration Into `Core`

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE12_COMPLETION_REPORT.md` (all twelve prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE — this is the design doc's own explicitly-named highest-risk
phase ("genuine surgery, not a mechanical cut-and-paste"). Read
`src/Application/Application.cpp`'s entire `Run()` method (2506 lines) plus
every private method it calls, in full, before writing anything, per this
phase's own non-negotiable Step 2 instruction.

## Step 0 — Confirmed no overlapping in-flight work

`git_status` showed a clean working tree on `feature/editor-core-separation`
before starting (no other campaign has a pending diff touching
`Application.cpp`). Every other `task_manager/*` folder either belongs to a
long-finished campaign (own `CAMPAIGN_COMPLETION_REPORT.md`/final phase report
already present) or is this same campaign's own earlier phase. No
`ask_questions` call was needed for this step — the evidence was unambiguous.

## Classifying every `IEditorLayer` call site in `Application::Run()`

Read `Run()`'s real, current body (lines 1099-2505) plus the two
`Register*RenderPipelineProvider()` methods it calls from the constructor
(lines 439-1096) in full, and found **exactly the ~30 call sites** the
strategy doc's own double-check pass predicted — no call site outside Locked
Design Decision #8's own two named lists was found; both lists turned out to
be exhaustive. Full itemized accounting:

### Bucket 1 — render-graph-frame-building (moved into `Core`, called through `Core`'s own null-checked `m_editorLayer` hook, exclusively inside `Core::BuildFrame()`)

Exactly the 13 methods Locked Design Decision #8 names, in the order they are
called this phase:

1. `GameViewTarget()`
2. `SceneViewTarget()`
3. `PrepareFrameDebuggerCaptureContext()`
4. `SceneViewProjection(aspect)` (Scene View branch only)
5. `SceneViewCameraWorldPosition()` (Scene View branch only)
6. `RenderSceneGrid()` (inside the `recordSceneOverlay` lambda, itself only
   ever invoked from the "RenderTransparent" provider)
7. `ConsumePendingFrameDebuggerReplayRequest()`
8. `AddBlurValidationPass()`
9. `AddGBufferValidationPass()`
10. `SetGameViewCompositedTexture()`
11. `SetSceneViewCompositedTexture()`
12. `FinalizeBlurValidationForSampling()`
13. `FinalizeGBufferValidationForSampling()`

### Bucket 2 — host-level UI-building/input-routing/automation (stays directly on `Application`'s own `m_editorLayer`, never routed through `Core`)

22 distinct call sites (some occurring once per polled SDL event, still one
call *site*):

`ProcessEvent()`, `OnWindowResized()`, `WantsCaptureMouse()`,
`WantsCaptureKeyboard()`, `IsPlaybackPaused()`, `TryConsumeStepRequest()`,
`NotifyFrameDebuggerStepConsumed()`, `NewFrame()`, `ActivateTab()`,
`SpawnGpuDrivenTestBatch()`, `FrameDebuggerOpenWindow()`,
`FrameDebuggerSetEnabled()`, `FrameDebuggerCaptureNow()`,
`FrameDebuggerSelectEvent()`, `FrameDebuggerSetChannel()`,
`FrameDebuggerSetLevels()`, `FrameDebuggerGetState()`,
`ImportExternalAssetIntoProject()`, `BuildUI()`, `WantsExit()`, `Render(cmd)`,
`RenderPlatformWindows()`.

**One of these, `Render(cmd)`, needed a genuinely new wiring shape** (not just
"leave it where it is"): the closure that calls it
(`[this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); }`) is *invoked*
from deep inside `Core::Present()`'s own "Present" `RenderPipeline` provider
(the same place `m_recordImGuiThisFrame` was always read from), but the
closure's own body must still call the HOST's `m_editorLayer`, never `Core`'s
— per Decision #8's explicit "Core never calls these" rule. Resolved via a new
`Core::SetPresentImGuiRecorder(std::function<void(VkCommandBuffer)>)`,
called ONCE from `Application`'s own constructor body: the lambda still
textually lives in `Application.cpp`, capturing `Application`'s own `this`;
`Core` merely stores/forwards the resulting `std::function` value, never
calling `IEditorLayer::Render()` itself. Confirmed correct live (see smoke
check below — the ImGui chrome renders every frame).

**13 (Bucket 1) + 22 (Bucket 2) = 35 distinct call sites** — consistent with
the strategy doc's own "roughly 30" estimate (some Bucket 2 call sites are
inside a per-event loop, which the original estimate likely counted as one).

### A closely-related discovery that is NOT an `IEditorLayer` call site at all

The Game-View and swapchain `FrameCaptureBridge` **success-path capture**
code (`m_captureBridge.FulfillPendingRequest(...)`, PNG encode, the Game-View
fast-fail branch) was textually interleaved inside the SAME offscreen/present
`try` blocks the moved render-graph code lives in, but never once touches
`m_editorLayer`. Per design doc Section 6.1 ("Core stays a pure engine facade:
no HTTP server, no automation-bridge knowledge, ever"), this code must NEVER
move into `Core`. It was **relocated to run in `Application::Run()`,
immediately AFTER `m_core.BuildFrame()`/`m_core.Present()` return** — safe,
because both of `Core`'s own methods already fence-wait
(`EndOffscreenRenderGraphRecording()`/`Renderer::PresentViaRenderGraph()`)
before returning, so every pixel the relocated capture code reads is already
final by the time it runs. A new, small, additive `Core` accessor,
`GetGameViewTargetThisFrame()`, was added so this relocated code can react to
"is the Game View visible this frame" without a second, duplicate
`IEditorLayer::GameViewTarget()` call site (which Decision #8's own Definition
of Done forbids: that call now lives ONLY inside `Core::BuildFrame()`).

## What I did

1. **NEW `src/Renderer/Culling/GpuDrivenBatchDebugInfo.h`** — relocated the
   `GpuDrivenBatchDebugInfo` struct here, verbatim, out of
   `src/Editor/EditorLayer.h`. **Necessary, minimal, justified deviation,
   found and resolved without needing `ask_questions`**: this plain,
   dependency-free data type is *produced* every frame by `Core`'s own
   GPU-driven-batch orchestration (now inside `Core::BuildFrame()`), and a new
   `Core::GetGpuDrivenBatchDebugInfo()` accessor needs to return
   `const std::vector<GpuDrivenBatchDebugInfo>&` by COMPLETE type — but
   `Core.h` may never `#include` anything under `src/Editor/` except
   `EditorLayer.h` itself (Locked Design Decision #8). Relocating this one
   struct (co-located with `GpuDrivenBatchCache.h`, the subsystem it actually
   describes) resolves this with zero behavior/shape change.
   `src/Editor/EditorLayer.h` now `#include`s this header instead of defining
   the struct inline.
2. **`src/Core/InputFrame.h`** — extended from an empty placeholder (PHASE12)
   with `const InputState* inputState`, `bool playbackPaused`,
   `bool stepRequested` — per PHASE13's own explicitly-sanctioned "extend
   InputFrame" option (rather than growing `Core::Update()`'s own frozen
   2-parameter signature).
3. **`src/Core/Core.h`** — real class body:
   - Physically relocated (from `Application`, unchanged in shape):
     `m_offscreenRenderPipeline`/`m_presentRenderPipeline`
     (`rg::RenderPipeline`), every `...ThisFrame` scratch member (GPU
     skinning requests/handles, per-view data, the GPU-driven-batch cache/
     render-data/entity-set/view-projection/debug-info, the present regime's
     `m_needsDirectGameRenderThisFrame`/`m_directGameRenderAspectThisFrame`/
     `m_swapchainImageThisFrame`/`m_recordImGuiThisFrame`),
     `m_atmosphereLutRenderer`, `m_atmosphereSettings`, and the private
     `RegisterOffscreenRenderPipelineProviders()`/
     `RegisterPresentRenderPipelineProvider()`/`FindViewData()` methods.
   - NEW private members: `m_windowWidth`/`m_windowHeight` (mirrors
     `Application`'s own former cache, seeded from `ISurfaceProvider`'s
     CONSTRUCTION-time size, kept live via the new `NotifyWindowResized()`),
     `m_presentImGuiRecorder`, `m_gameTargetThisFrame`/`m_sceneTargetThisFrame`.
   - NEW public, additive accessors (justified under design doc Section 5.3's
     own permissive "At minimum..." language): `GetGpuDrivenBatchDebugInfo()`,
     `GetAtmosphereSettings()`, `GetAtmosphereLutRenderer()`,
     `GetGameViewTargetThisFrame()`, `SetPresentImGuiRecorder(...)`,
     `NotifyWindowResized(int, int)`.
4. **`src/Core/Core.cpp`** — the real bodies:
   - `Update()` advances `Time` (respecting the already-resolved
     Pause/Step booleans threaded in via `InputFrame`) and dispatches
     `Game::Update()`.
   - `RegisterOffscreenRenderPipelineProviders()`/
     `RegisterPresentRenderPipelineProvider()`/`FindViewData()` — relocated
     verbatim from `Application.cpp`, every `m_editorLayer->` call site now
     wrapped in an explicit `if (m_editorLayer != nullptr)` check (per Locked
     Design Decision #8's own literal "each wrapped in the exact same
     `if (m_editorLayer != nullptr) { ... }` null-check shape"), even at the
     handful of call sites already implicitly protected by an enclosing
     `gameTarget`/`sceneTarget`/`frameDebuggerCapture` non-null check (the two
     calls inside the Scene-View-only per-view-data-building block —
     `SceneViewProjection()`/`SceneViewCameraWorldPosition()` — are the one
     documented exception: reached only when `sceneTarget != nullptr`, which
     itself can only be true when `m_editorLayer != nullptr`, so a second,
     redundant check was intentionally omitted there and explicitly commented
     as such).
   - `BuildFrame()` — resolves `gameTarget`/`sceneTarget`/
     `frameDebuggerCapture` through the hook, runs the full offscreen
     build-and-`Execute()` regime (unchanged body, minus the relocated
     capture-success-path code — see above), and sets `GameView`/`SceneView`
     GPU profiling stats.
   - `Present()` — runs the swapchain-present regime (unchanged body, minus
     the relocated swapchain-capture-success-path code), and sets `Present`
     GPU profiling stats.
   - Every anonymous-namespace helper only the moved code needs
     (`AspectRatioOf`, `kFixedStepSeconds`, every blackboard key,
     `AtmosphereSharedLutBlackboardEntry`, `ToProfilingGpuSampleStatus`,
     `GpuDrivenBatchNames`/`GpuDrivenBatchNamePool`/`BatchNamePool()`)
     relocated verbatim.
5. **`src/Application/Application.h`** — removed every member/method that
   physically moved into `Core` (see above). Kept/added same-named REFERENCE
   members for `m_atmosphereSettings`/`m_atmosphereLutRenderer` (mirroring
   PHASE12's own already-established `m_renderer`/`m_game`/`m_renderGraph`/
   `m_engineContext` reference-member pattern) — both are still needed by two
   remaining host-level call sites (`BuildUI()`, the relocated Game-View
   capture-success path's `CompositedOutput()` call).
6. **`src/Application/Application.cpp`** — shrunk from 2506 to 872 lines.
   Every Bucket 2 `IEditorLayer` call site stays exactly where it was, in
   exactly the same relative order. The three former per-frame calls into the
   render-graph orchestration (`m_engineContext.time.Advance(...)`, the
   offscreen `Execute()` block, the present `Execute()` block) are now three
   short calls: `m_core.Update(inputFrame, deltaTime)`, `m_core.BuildFrame()`,
   `m_core.Present()`. The relocated FrameCaptureBridge success-path capture
   code (Game View + swapchain) runs immediately after each corresponding
   `Core` call returns. `gte::Logger::SetCurrentFrame(...)` stays directly on
   `Application` (reads `m_engineContext.time.FrameCount()` via the
   already-existing reference member) since it is not an `IEditorLayer` call
   at all, and `Core` must never `#include Editor/Logger.h`.
7. **Automation bridge access-path check (Step 4 of the phase plan)** —
   re-confirmed by direct code read: `EngineCommandBridge`/
   `EditorUiCommandBridge`/`FrameDebuggerCommandBridge`/
   `AssetImportCommandBridge`'s own dispatch code in `Run()` only ever touches
   `m_game`/`m_renderer`/`m_sceneIOCapability` — all three already
   reference-bound to `Core`'s own real instances since PHASE12. **Zero
   additional access-path fix was needed** — none of the four bridges'
   dispatch code ever touched `m_atmosphereSettings`/`m_atmosphereLutRenderer`/
   the `RenderPipeline`s/any `...ThisFrame` member, confirmed by a fresh,
   dedicated re-read (mirroring PHASE7's own "zero code changes required,
   confirmed by fresh reads" precedent in this same campaign).
8. **`CMakeLists.txt`** — registered the new
   `src/Renderer/Culling/GpuDrivenBatchDebugInfo.h`.

## Compile-check / test / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 14/19).

- `cmake --build build --target gte_core` — **succeeded cleanly on the FIRST
  attempt** (5 build steps: `NullEditorLayer.cpp`, `Core.cpp`,
  `Application.cpp` recompiled, `libgte_core.a` relinked). Only the
  pre-existing, unrelated `third_party/ktx` `git describe` warning appeared
  (same as every prior phase's own report).
- `cmake --build build --target gte_editor` — **succeeded cleanly** (5 build
  steps — every file transitively depending on the relocated
  `GpuDrivenBatchDebugInfo` struct recompiled correctly).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**,
  full executable relinked, every `.spv` shader + `SDL3.dll` staged as usual.
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly**.
- Targeted `ctest` spot-check
  (`SdlLinkageRegression|LoggerTest|GpuMemoryTracker|
  EditorGpuMemoryNameOverlay|RenderGraph|EditorCamera|SceneGrid|LogSink|
  SpawnGpuDrivenTestBatch`) — **299/299 passed (100%)**, covering every
  Render-Graph-builder/compiler/snapshot/barrier-planner Tier-1 test, every
  Logger/LogSink test, every GpuMemoryTracker/overlay test, the Editor
  Camera/Scene Grid math tests, and the SDL linkage regression test (still
  correctly reports the test binary requires `SDL3.dll` — expected, unaffected
  by this phase, Phase 14's own job to flip).
- **Live smoke check** (`run_app_background` + `gte_send_request`),
  deliberately thorough per this phase's own "highest-regression-risk"
  status:
  - `GET /get_swapchain` — screenshot confirmed the Editor renders exactly
    like every prior phase's own documented baseline (docked Hierarchy/Scene/
    Game/Inspector panels, the Pause/Step toolbar, the "Show Compute Blur
    (debug)"/"Show GBuffer Validation (debug)" checkboxes both visible and
    unchecked, identical sky-gradient Atmosphere rendering in both Scene and
    Game panels). **No dedicated separate "before" screenshot file exists
    from an earlier phase** (screenshots are embedded inline in each phase's
    own completion report, not saved to disk) — PHASE12's own most recent
    documented screenshot (identical panel layout/rendering) is the "before"
    reference this phase's own screenshots are compared against, and they
    match exactly.
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0` at boot.
  - `POST /spawn_gpu_driven_test_batch` (`{"instanceCount":6}`) — `200`,
    `{"instance_count":6,"success":true}` — exercises the single most
    heavily-relocated subsystem (GPU-driven batch collection, culling compute
    dispatch, indirect draw, count-buffer readback, all now inside
    `Core::BuildFrame()`).
  - `GET /get_game_view` — screenshot confirmed **6 real quads rendered
    correctly** via the indirect-draw path.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` — screenshot
    confirmed: (a) both Game and Scene views show the 6 spawned quads
    correctly, (b) the "GPU-Driven Batches (instances culled this frame)"
    section shows `GpuDrivenBatch0: 6 / 6 instances visible (0 culled)` —
    direct proof `Core::GetGpuDrivenBatchDebugInfo()`'s new accessor correctly
    flows this frame's data from `Core::BuildFrame()` through to
    `IEditorLayer::BuildUI()`, (c) the "Offscreen Regime" pass table shows
    real, correct per-pass draw/triangle/GPU-time stats for every Atmosphere
    LUT pass (Game View AND Scene View copies) and `RenderOpaque` (6 draws,
    12 triangles) — proving the whole relocated `RegisterOffscreenRenderPipelineProviders()`
    provider set still declares/executes correctly.
  - `GET /activate_tab?name=Profiler` + `GET /get_swapchain` — screenshot
    confirmed a live, populated "CPU Frame Time" graph (30.30 ms / 33 FPS,
    real min/max range) and a populated "CPU Scopes" table
    (`Renderer::PresentViaRenderGraph` 19.52 ms,
    `RenderGraph::Execute(Offscreen)` 9.85 ms, `IEditorLayer::BuildUI`
    0.78 ms) — direct, positive evidence the engine keeps running smoothly,
    frame after frame, entirely through the new `Core::Update()`/
    `BuildFrame()`/`Present()` call chain, with the exact same
    `GTE_PROFILE_SCOPE` names preserved verbatim through the relocation.
  - `GET /get_logs?since_id=0&limit=100` — only the two expected startup
    entries (`Network`/`"listening on..."`, `Application`/`"...started."`),
    zero warnings/errors accumulated across the entire session (spawning a
    batch, switching tabs, multiple screenshots).
  - `stop_app_background`'d the process cleanly when done.

### Playback Pause/Resume/Step and the two debug toggles — an honest, documented automation limitation

**Neither this engine's HTTP surface nor my currently-loaded tool set exposes
a way to click an ImGui checkbox/button** (`GET /get_logs`-style endpoints
exist for Frame Debugger commands, but a dedicated search of
`src/Network/` confirmed **no HTTP endpoint exists for Play/Pause/Step, "Show
Compute Blur (debug)", or "Show GBuffer Validation (debug)"** — these are
pure mouse-driven ImGui widgets with no automation bridge at all, and no
mouse/keyboard-injection tool is available in my active tool set for a native
desktop app like this one, unlike the web-only `playwright` category). I could
not literally toggle these three UI controls this session. Rather than skip
verifying them silently, I instead confirmed correctness through the
strongest evidence actually available:

- **Playback Pause/Resume/Step**: `Core::Update()`'s own body is a
  byte-for-byte-unchanged relocation of `Application::Run()`'s former
  `m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame,
  kFixedStepSeconds)` call — only WHERE it is called from changed (now inside
  `Core::Update()`, fed via `InputFrame` instead of three separate local
  variables), never the call's own arguments/logic. The "Pause"/"Step" toolbar
  buttons themselves render correctly and are visible, unchecked/idle, in
  every screenshot above (host-level `BuildUI()`, completely untouched by this
  phase). The Profiler panel's own live, continuously-updating Frame Time
  graph (screenshot above) is itself proof `Time::Advance()` runs correctly,
  every frame, through the new call chain, in its default (not-paused) state
  — the overwhelmingly common case this whole session exercised.
- **"Show Compute Blur (debug)"/"Show GBuffer Validation (debug)"**: both
  checkboxes were confirmed OFF (their default state) in every screenshot,
  meaning `AddBlurValidationPass()`/`AddGBufferValidationPass()` returned
  `std::nullopt` every single frame — but **the call sites themselves were
  genuinely, repeatedly EXECUTED** every frame this whole session (guarded
  only by `sceneVisibleForBlurValidation`, which was `true` throughout, since
  the Scene panel was visible in every screenshot) with zero crash, zero log
  warning/error, across dozens of frames and several tab switches/spawns —
  real, repeated exercise of the exact relocated code path (including the new
  explicit `m_editorLayer != nullptr` guards), just with the toggle itself
  left at its default OFF value.

This is flagged here loudly and honestly, per this campaign's own "no
silent-skip" discipline (mirroring PHASE1's own detailed "Deviation" writeup
for a comparable automation-limitation finding) — not claimed as a full,
literal "clicked the checkbox and watched it turn on" verification, which
genuinely did not happen this session.

No `bug_report` was filed — no tool malfunctioned during this phase; the
Pause/Step/debug-toggle limitation above is a genuine ABSENCE of an
automation surface for this specific UI, not a broken tool call.

## Definition of Done — checklist

- [x] `Application::Run()`'s loop body matches the fuller shape Step 3.3
      describes — every Bucket 2 (host-level) `IEditorLayer` call site still
      lives directly on `Application`, interleaved around three much-shorter
      calls into `Core` (`Update()`/`BuildFrame()`/`Present()`).
- [x] Every Bucket 1 (render-graph-frame-building) `IEditorLayer` call site
      now lives inside `Core::BuildFrame()`, reached through `Core`'s own
      null-checked `m_editorLayer` hook, never directly on `Application`
      anymore (confirmed via `search_in_dir` for each of the 13 method names
      scoped to `Application.cpp` — zero remaining hits).
- [x] Live visual smoke check confirms zero rendering regression across Game
      View, Scene View, Atmosphere (all 6 LUT/composite passes per view),
      GPU Skinning (unexercised this session — no animated model in the
      current scene — but its provider registration/dispatch code moved
      verbatim and compiled/linked cleanly; no behavior change was made to
      it), and GPU-driven batching (directly exercised and visually
      confirmed).
- [x] Playback Pause/Resume/Step and the two debug toggles — verified as
      thoroughly as this session's actual available tooling allows; the
      genuine automation-surface gap (no HTTP endpoint, no UI-click tool) is
      documented explicitly above, not silently skipped.
- [x] `PHASE13_COMPLETION_REPORT.md` written (this file), including the full
      itemized accounting of every `IEditorLayer` call site's new home and
      the before/after screenshot comparison description.
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did not move `Window`/`SdlContext` (Phase 14's job) — `gte_core` still
  legitimately links `SDL3::SDL3`/compiles `Window.cpp` today.
- Did not build `EditorHost` (Phase 15) — `Application` remains the
  composition root; the automation bridges (`EngineCommandBridge`,
  `FrameCaptureBridge`, `EditorUiCommandBridge`, `FrameDebuggerCommandBridge`,
  `AssetImportCommandBridge`, `Network::NetworkServer`) stay
  `Application`-owned, exactly as before this phase (Phase 16's job to move
  them to `EditorHost`).
- Did not touch `RenderPasses.h`/`.cpp` or `RenderPassViewData.h`'s own file
  location — both stay physically under `src/Application/` (already part of
  `gte_core`'s own CMake target since before this campaign began, per the
  design doc's Section 2.2 inventory) — `Core.cpp` simply `#include`s them
  from their existing location, exactly as this phase's own "Files Touched"
  section anticipated.
- Did not touch `GTE_ENABLE_PROJECT_PANEL`, `IEditorLayer`'s own method
  set/shape (Locked Design Decision #8 forbids redesigning/fragmenting it —
  only its ownership and which-subset-runs-where changed), or any CMake
  target-split concern.

## Files touched

- NEW: `src/Renderer/Culling/GpuDrivenBatchDebugInfo.h`
- MODIFIED: `src/Editor/EditorLayer.h` (relocated `GpuDrivenBatchDebugInfo`'s
  own definition out to the new header above; `#include`s it instead)
- MODIFIED: `src/Core/InputFrame.h` (extended with `inputState`/
  `playbackPaused`/`stepRequested`)
- MODIFIED: `src/Core/Core.h` (real class body: every relocated member/method
  + 6 new, additive, justified accessors)
- MODIFIED: `src/Core/Core.cpp` (real method bodies: `Update()`/`BuildFrame()`/
  `Present()`/`RegisterOffscreenRenderPipelineProviders()`/
  `RegisterPresentRenderPipelineProvider()`/`FindViewData()`, relocated
  anonymous-namespace helpers)
- MODIFIED: `src/Application/Application.h` (shrunk — every relocated
  member/method removed; two new reference members added)
- MODIFIED: `src/Application/Application.cpp` (shrunk from 2506 to 872 lines —
  every Bucket 2 `IEditorLayer` call site preserved verbatim, three render-
  graph orchestration calls replaced with `m_core.Update()`/`BuildFrame()`/
  `Present()`, FrameCaptureBridge success-path capture code relocated to run
  immediately after each)
- MODIFIED: `CMakeLists.txt` (registered the new header)
- NEW: `task_manager/editor-core-separation-1/PHASE13_COMPLETION_REPORT.md`
  (this file)
