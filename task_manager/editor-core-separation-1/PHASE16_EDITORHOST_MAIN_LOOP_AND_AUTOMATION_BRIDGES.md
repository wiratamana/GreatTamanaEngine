# PHASE16 — `EditorHost::Run()` Main Loop + Automation Bridges

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 15.

## Step 1: The Goal

`EditorHost::Run()` owns the REAL main loop (event pump, translate input,
call `m_editorLayer` directly for every host-level UI-building/input-
routing concern, call `Core::Update()`/`BuildFrame()`/`Present()` for the
engine-core work) — matching the exact fuller shape Phase 13 already gave
`Application::Run()` (NOT the design doc's own illustrative 3-line
simplification — see Phase 13's own Step 3.3 note on this). `EngineCommandBridge`,
`FrameCaptureBridge`, `EditorUiCommandBridge`, `FrameDebuggerCommandBridge`,
`AssetImportCommandBridge`, and the embedded `Network::NetworkServer` all
move from `Application` to `EditorHost` — these are host-level concerns,
never `Core`'s. After this phase, the `EditorHost`-based executable is
FULLY functionally equivalent to the old `Application`-based one.

## Step 2: The Situation / The Problem

Read `Application.h`/`.cpp`'s current (post-Phase-13) shape in full —
specifically its `Run()` loop's real, full shape (input/time/UI/automation
calls directly against `m_editorLayer`, interleaved around its three calls
into `m_core`) and every automation bridge member it still owns, plus how
each bridge currently reaches into `Core`'s accessors (Phase 13 already
fixed the ACCESS PATH — this phase just relocates the OWNERSHIP).

## Step 3: The Plan

1. Move `EngineCommandBridge`, `FrameCaptureBridge`,
   `EditorUiCommandBridge`, `FrameDebuggerCommandBridge`,
   `AssetImportCommandBridge`, and the embedded `Network::NetworkServer`
   member declarations from `Application` to `EditorHost`. Each bridge's
   own constructor/wiring logic moves identically — it should already be
   reaching `Core` only through `m_core.GetXxx()` accessors (Phase 13), so
   this move should not require changing what each bridge itself does,
   only who owns/constructs it.
2. Wire each Bucket B capability adapter (`EditorSceneIOCapability`,
   `EditorUiCapabilityImpl`, `EditorAssetImportCapabilityImpl`,
   `EditorGpuDrivenBatchTestCapabilityImpl` from Phases 5-7) into
   `EditorHost` instead of `Application` — this was flagged as "temporary
   home in Application" in those phases' own plans; this is where they
   move to their REAL, permanent home.
3. Implement `EditorHost::Run()`'s real body — copy `Application::Run()`'s
   OWN real, post-Phase-13 loop shape (every `m_editorLayer->...()` call
   Phase 13 deliberately kept OUTSIDE `Core`, interleaved around
   `m_core.Update()`/`BuildFrame()`/`Present()`), byte-for-byte where
   possible:
   ```cpp
   void EditorHost::Run() {
       while (running) {
           PumpSdlEvents(); // forwards SDL_Event to m_editorLayer->ProcessEvent()
           InputFrame input = m_eventTranslator.Translate(sdlEvents);
           m_editorLayer->NewFrame();
           const bool paused = m_editorLayer->IsPlaybackPaused();
           const bool stepRequested = m_editorLayer->TryConsumeStepRequest();
           // ... automation-bridge pumps (ActivateTab, asset import,
           // GPU-driven-batch spawn, Frame Debugger UI-state commands),
           // exactly matching Application::Run()'s own post-Phase-13 shape ...

           m_core.Update(input, deltaTime); // paused/stepRequested threaded in
           m_core.BuildFrame();             // Editor render-graph hooks fire
                                             // INSIDE here, via Core's own
                                             // m_editorLayer pointer (Phase 12/13)
           m_core.Present();

           m_editorLayer->BuildUI(m_core.GetGame(), m_core.GetRenderer(),
               m_core.GetRenderGraph(), /* ... */);
           if (m_editorLayer->WantsExit()) { running = false; }
           m_editorLayer->RenderPlatformWindows();
       }
   }
   ```
   This is illustrative, not literal — copy Phase 13's ACTUAL, real,
   verified ordering from `Application::Run()` rather than re-deriving it
   from scratch here. If Phase 13's real code diverged from this exact
   shape for a good reason, document why and adapt here consistently
   rather than forcing an artificial match to either this sketch or the
   design doc's own simplified one.
4. Confirm `Core` itself NEVER gains a member/reference to any of these
   bridges or the `NetworkServer` — re-read `Core.h` after this phase and
   confirm it still only exposes the plain accessors plus the
   `IEditorLayer*` hook from Phase 12, with zero HTTP/automation-bridge
   awareness of any kind (design doc Section 6.1's explicit rule).
5. Compile-check: incremental build. Live smoke check: `run_app_background`
   the `EditorHost`-based executable, exercise EVERY automation endpoint
   this campaign has touched so far via `gte_send_request` (`/save_scene`,
   `/load_scene`, `/activate_tab`, `/list_tabs`,
   `/spawn_gpu_driven_test_batch`, `/get_swapchain`, `/get_game_view`,
   `/frame_debugger/*`, `/get_logs`) and confirm every single one still
   works exactly as before this entire campaign started. Also re-exercise
   the "Show Compute Blur (debug)"/"Show GBuffer Validation (debug)"
   toggles specifically (the two features whose render-graph pass
   declaration now flows through `Core`'s own `IEditorLayer*` hook, per
   Phase 13) to confirm the hook survived the ownership move from
   `Application` to `EditorHost` intact.

## Files Touched

- `src/Editor/EditorHost.h`/`.cpp`
- `src/Application/Application.h`/`.cpp` (shrink further — bridges removed)
- Wherever Phases 5-7's capability adapters were temporarily wired

## Definition of Done

- Every automation bridge lives on `EditorHost`, none on `Core`.
- Every HTTP endpoint this campaign touched is confirmed working live,
  end-to-end, through the new `EditorHost`-based executable, including the
  two render-graph-hook-dependent debug toggles above.
- `PHASE16_COMPLETION_REPORT.md` (listing every endpoint tested and its
  result) + git commit.

## Out of Scope

Do not delete `Application.h`/`.cpp` yet (Phase 17) — even though it should
now be nearly empty/unused, leave the actual deletion as its own reviewable
step.
