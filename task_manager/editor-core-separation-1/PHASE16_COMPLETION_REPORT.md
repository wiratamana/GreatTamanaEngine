# PHASE16 — COMPLETION REPORT: `EditorHost::Run()` Main Loop + Automation Bridges

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE15_COMPLETION_REPORT.md` (all fifteen prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE — depends on Phase 15, confirmed already landed (`EditorHost`
exists, owns `SdlContext`/`Window`, constructs `Core`, wires
`Core::SetEditorLayerHook()`, `Run()` was a deliberate temporary stub).

## Step 0 — Re-confirmed the real, current code shape before editing

Per Universal Rule 9, read (not assumed) every file this phase touches or
depends on, fresh, before writing anything:

- `src/Application/Application.h`/`.cpp` (post-Phase-13 state, 918 real lines
  of `Run()`) — read `Run()`'s ENTIRE real body in full, exactly per this
  phase's own non-negotiable instruction ("this phase's own task explains
  `EditorHost::Run()`'s real loop is NOT simply a literal 3-line sketch — it
  must copy `Application::Run()`'s own real, post-Phase-13 loop shape").
  Confirmed the exact, real interleaving: input polling/translation (host-
  level `IEditorLayer` calls: `ProcessEvent`/`OnWindowResized`/
  `WantsCaptureMouse`/`WantsCaptureKeyboard`) → `EngineCommandBridge`
  servicing → Pause/Step resolution (`IsPlaybackPaused`/
  `TryConsumeStepRequest`/`NotifyFrameDebuggerStepConsumed`) →
  `m_core.Update()` → `Logger::SetCurrentFrame()` → `m_editorLayer->NewFrame()`
  → `EditorUiCommandBridge` servicing (`ActivateTab`/`SpawnGpuDrivenTestBatch`)
  → `FrameDebuggerCommandBridge` servicing (all 7 command kinds) →
  `AssetImportCommandBridge` servicing → `m_renderer.BeginFrame()` →
  `m_core.BuildFrame()` → the relocated Game-View `FrameCaptureBridge`
  success-path capture → `IEditorLayer::BuildUI()` → `WantsExit()` →
  Swapchain-capture-request flag → `m_core.Present()` → the relocated
  swapchain `FrameCaptureBridge` success-path capture →
  `RenderPlatformWindows()` → the Named-Texture `FrameCaptureBridge` capture
  path (2D + volume branches) → `GET /list_textures`' publishing block →
  the GPU-memory-snapshot/profiler bracket close.
- `src/Editor/EditorHost.h`/`.cpp` (post-Phase-15 state) — confirmed the
  exact real member declaration order, constructor initializer-list order,
  and the exact TEMPORARY `Run()` stub text this phase replaces.
- `src/Core/Core.h` — re-confirmed it still exposes only the plain
  accessors + the nullable `IEditorLayer*` hook (Locked Design Decision #8),
  with **zero** HTTP/automation-bridge awareness of any kind — confirmed
  `Core.h`/`Core.cpp` needed **zero changes** this phase (`git_status`
  confirms neither file appears in this phase's diff).
- `src/Network/NetworkServer.h` — confirmed the exact 5-pointer constructor
  signature (`FrameCaptureBridge*`, `EngineCommandBridge*`,
  `EditorUiCommandBridge*`, `FrameDebuggerCommandBridge*`,
  `AssetImportCommandBridge*`, all defaulted).
- `src/Application/EngineCommandDispatch.h` — confirmed
  `ExecuteEngineCommand(Game&, Renderer&, ISceneIOCapability*, const
  EngineCommandRequest&)`'s exact signature.
- `src/Core/EditorCapabilities.h`/`src/Editor/EditorSceneIOCapability.h` —
  confirmed `ISceneIOCapability`'s three methods and
  `EditorSceneIOCapability`'s real, default-constructible, stateless adapter
  shape (PHASE5/PHASE6's own real gap — the only genuinely-new Bucket B
  capability this campaign found).
- Confirmed via `search_in_dir` that **nothing** anywhere in this codebase
  still `#include`s `Application/Application.h` (only `Application.cpp`
  itself does) — `main.cpp` has constructed `EditorHost` since Phase 15, and
  no test file instantiates the `Application` class — confirming `Application`
  is genuinely, completely dead code today, safe to shrink aggressively
  without breaking anything real.

No path/shape surprises versus this phase's own plan text — every real file
matched exactly what the phase document described.

## What I did

1. **`src/Editor/EditorHost.h`** — added every automation-bridge member
   (`FrameCaptureBridge m_captureBridge`, `EngineCommandBridge
   m_commandBridge`, `EditorUiCommandBridge m_uiCommandBridge`,
   `FrameDebuggerCommandBridge m_frameDebuggerCommandBridge`,
   `AssetImportCommandBridge m_assetImportCommandBridge`), the embedded
   `Network::NetworkServer m_networkServer`, the
   `VolumeTexturePreviewRenderer m_volumeTexturePreviewRenderer` (needed for
   the named-texture volume-capture branch), the `ISceneIOCapability*
   m_sceneIOCapability` pointer, and REFERENCE members bound to `m_core`'s
   own real, owned instances (`Renderer&`, `rg::RenderGraph&`, `Game&`,
   `EngineContext&`, `AtmosphereSettings&`, `AtmosphereLutRenderer&`) —
   mirroring `Application.h`'s own exact precedent (PHASE12/PHASE13) member-
   for-member, in the same relative declaration order (each bridge declared
   BEFORE `m_networkServer` so its address can be handed into that
   constructor).
2. **`src/Editor/EditorHost.cpp`** —
   - Constructor: added the four new initializer-list entries
     (`m_renderer`/`m_renderGraph` bound right after `m_core`, then
     `m_editorLayer`, then `m_game`/`m_engineContext`/
     `m_atmosphereSettings`/`m_atmosphereLutRenderer`, then
     `m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge,
     &m_frameDebuggerCommandBridge, &m_assetImportCommandBridge)`) and three
     new constructor-BODY calls: `m_core.SetPresentImGuiRecorder([this](VkCommandBuffer
     cmd) { m_editorLayer->Render(cmd); })` (mirrors
     `Application`'s own identical wiring — Core stores/forwards this
     `std::function`, never calling `IEditorLayer::Render()` itself), the
     `EditorSceneIOCapability` wiring (a function-local `static` instance,
     mirroring `LoggerLogSink::Instance()`'s own Meyers-singleton precedent —
     this is the capability's PERMANENT home now, replacing `Application`'s
     own explicitly-documented-as-temporary `SetSceneIOCapability()` setter),
     and `#if GTE_ENABLE_NETWORK m_networkServer.Start(8080); #endif`.
   - `Run()` — replaced PHASE15's deliberate temporary stub with the REAL
     per-frame loop, copied member-for-member/line-for-line from
     `Application::Run()`'s own real, post-Phase-13 body (Step 0 above),
     with every reference to `Application`'s own member names swapped for
     `EditorHost`'s identically-named equivalents (no logic changed, only
     `GTE_PROFILE_SCOPE` label prefixes updated from `"Application::..."` to
     `"EditorHost::..."` for honest profiler-panel labeling). Added the
     `IsBgraFormat()`/`ToDebugTextureRegimeString()`/
     `DebugTextureColorFormatName()` anonymous-namespace helpers this body
     needs (relocated verbatim from `Application.cpp`, since `Application`
     no longer needs them — see below).
3. **`src/Application/Application.h`** — shrunk further, per this phase's
   own "Files Touched" instruction: removed every bridge member
   (`FrameCaptureBridge`/`EngineCommandBridge`/`EditorUiCommandBridge`/
   `FrameDebuggerCommandBridge`/`AssetImportCommandBridge`), the embedded
   `Network::NetworkServer`, the `VolumeTexturePreviewRenderer`, the
   `ISceneIOCapability*` member + its `SetSceneIOCapability()` setter, and
   every `#include` only those removed members needed
   (`AssetImportCommandBridge.h`/`EngineCommandBridge.h`/
   `EditorUiCommandBridge.h`/`FrameCaptureBridge.h`/
   `FrameDebuggerCommandBridge.h`/`../Network/NetworkServer.h`/
   `../Renderer/VolumeTexturePreviewRenderer.h`/`../Core/EditorCapabilities.h`).
   Every remaining member (`m_sdlContext`/`m_window`/`m_hostServices`/
   `m_core`/`m_renderer`/`m_renderGraph`/`m_editorLayer`/`m_game`/
   `m_engineContext`/`m_atmosphereSettings`/`m_atmosphereLutRenderer`) is
   byte-for-byte unchanged from before this phase. Updated the class's own
   header comment to explain honestly that this class is now genuinely,
   completely unused, confirmed by a fresh `search_in_dir` (see Step 0).
4. **`src/Application/Application.cpp`** — shrunk from 918 to 216 lines.
   Removed: the `EngineCommandDispatch.h`/`EditorSceneIOCapability.h`/
   `Encoding/*`/`RenderGraphBuilder.h`/`RenderGraphDebugTextureRegistry.h`
   includes (no longer needed once the bridge-servicing/capture code they
   supported was removed), the `IsBgraFormat()`/
   `ToDebugTextureRegimeString()`/`DebugTextureColorFormatName()` helpers
   (relocated to `EditorHost.cpp`, since only it needs them now), the
   `NetworkServer::Start()` call, the `EditorSceneIOCapability`
   registration, and every bridge-servicing block in `Run()` (engine-command
   dispatch, EditorUi/FrameDebugger/AssetImport command draining, the
   Game-View/Swapchain/Named-Texture `FrameCaptureBridge` capture code, the
   `GET /list_textures` publishing block). What remains: SDL event pump +
   translation + `m_game.OnEvent()` routing, Pause/Step resolution,
   `m_core.Update()`/`BuildFrame()`/`Present()`, `Logger::SetCurrentFrame()`,
   `IEditorLayer::BuildUI()`, `WantsExit()`, `RenderPlatformWindows()`, and
   the GPU-memory-snapshot/profiler bracket — the minimal set of calls that
   don't reference any removed member, so this now-fully-dead class still
   compiles cleanly and doesn't dangle-reference anything, per this file's
   own new header comment ("kept around, still compiling, ONLY because
   Phase 17 is the phase that actually deletes it").
5. Confirmed via `git_status` that **exactly** these four files changed —
   `Core.h`/`Core.cpp` are untouched (re-read in full to re-confirm `Core`
   still holds nothing but the plain accessors + the nullable
   `IEditorLayer*` hook, with zero HTTP/automation-bridge awareness of any
   kind, per design doc Section 6.1 and this phase's own Step 4 instruction).

## Compile-check / test / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 19).

- `cmake --build build --target gte_core` — `ninja: no work to do` (correct
  and expected — `gte_core`'s own source list contains neither
  `Application.*` nor `EditorHost.*`, both of which live in `gte_editor`
  since Phase 14/15; `Core.h`/`Core.cpp` themselves are untouched this
  phase).
- `cmake --build build --target gte_editor` — **succeeded cleanly on the
  first attempt** (3 build steps: `Application.cpp.obj`, `EditorHost.cpp.obj`
  recompiled, `libgte_editor.a` relinked).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**
  (`main.cpp.obj` recompiled, full executable relinked, every `.spv` shader +
  `SDL3.dll` staged as usual).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly** (relink only).
- Targeted `ctest -R
  "SdlLinkageRegression|LoggerTest|EditorUiCommandBridge|EngineCommandBridge|
  FrameDebuggerCommandBridge|AssetImportCommandBridge|FrameCaptureBridge"` —
  **56/56 passed (100%)**, covering every automation-bridge unit test plus
  the Logger/SDL-linkage regression tests.
- **Live smoke check** (`run_app_background` PID 9300 + `gte_send_request`),
  exercising EVERY automation endpoint this campaign has touched, per this
  phase's own explicit instruction:

  | Endpoint | Result |
  |---|---|
  | `GET /get_swapchain` | `200` — screenshot confirmed the Editor renders exactly like every prior phase's own baseline (Hierarchy/Scene/Game/Inspector panels, Pause/Step toolbar, both debug checkboxes visible/unchecked, identical sky-gradient Atmosphere rendering) |
  | `GET /get_logs?min_level=Warning&limit=50` | `200` — `count: 0` at boot |
  | `GET /list_tabs` | `200` — `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project"]}` |
  | `POST /save_scene` (`{"path":""}`) | `200` — `{"success":true, "resolved_path":"...\\build\\Project\\TestScene.gtscene"}` — proves the relocated `ISceneIOCapability` wiring (EditorHost's own PERMANENT home) works |
  | `POST /load_scene` (`{"path":""}`) | `200` — `{"success":true}` — same file round-tripped correctly |
  | `GET /activate_tab?name=Render Graph` | `200` — `{"activated_tab":"Render Graph","success":true}` |
  | `POST /spawn_gpu_driven_test_batch` (`{"instanceCount":6}`) | `200` — `{"instance_count":6,"success":true}` |
  | `GET /get_swapchain` (after spawn) | `200` — screenshot confirmed 6 quads rendered in both Scene/Game panels AND the "Render Graph" panel's "GpuDrivenBatch0: 6 / 6 instances visible (0 culled)" readout — direct proof `Core::GetGpuDrivenBatchDebugInfo()` still flows correctly from `Core::BuildFrame()` through to `IEditorLayer::BuildUI()`, called now from `EditorHost::Run()` instead of `Application::Run()` |
  | `GET /get_game_view` | `200` — screenshot confirmed 6 real quads rendered via the indirect-draw path |
  | `GET /frame_debugger/open` | `200` — `{"success":true,"state":{"windowOpen":true,...}}` |
  | `GET /frame_debugger/enable?value=true` | `200` — `{"success":true,"state":{"enabled":true,...}}` |
  | `GET /frame_debugger/capture` | `200` — `{"success":true,"state":{"hasCapturedFrame":true,"totalEventCount":17,...}}` |
  | `GET /frame_debugger/select_event?index=0` | `200` — `{"success":true,"state":{"selectedEventIndex":0,...}}` |
  | `GET /frame_debugger/set_channel?value=r` | `200` — `{"success":true,"state":{"channel":"r",...}}` |
  | `GET /frame_debugger/set_levels?black=0.1&white=0.9` | `200` — `{"success":true,"state":{"levelsBlack":0.1,"levelsWhite":0.9,...}}` |
  | `GET /frame_debugger/state` | `200` — reflects all of the above |
  | `GET /list_textures` | `200` — 19 real entries (2D + volume textures, including `AtmosphereAerialPerspectiveVolume_GameView`/`_SceneView` as `"texture3d"`) — proves the relocated named-texture-list publishing code in `EditorHost::Run()` works |
  | `GET /get_logs?since_id=0&limit=100` | `200` — real entries including a NEW `"category":"EditorHost"` startup log line confirming the whole constructor chain (`SdlContext -> Window -> Core -> CreateEditorLayer() -> Core::SetEditorLayerHook()`, every bridge attached, `NetworkServer` started) completed correctly |

  Two caller mistakes (not tool/engine bugs) were caught and corrected
  during this pass: `GET /frame_debugger/enable?enabled=true` (wrong query
  parameter name — the real one is `value`, confirmed by reading
  `NetworkRoutes.h`) and `GET /frame_debugger/set_channel?channel=r` (same
  mistake, real parameter name also `value`) — both retried successfully
  with the correct parameter name immediately after. No `bug_report` was
  filed for either — both were self-inflicted parameter-naming mistakes on
  my part, not an engine/tool malfunction.

### "Show Compute Blur (debug)"/"Show GBuffer Validation (debug)" — same honest, documented automation limitation as PHASE13

Per PHASE13_COMPLETION_REPORT.md's own detailed precedent, **no HTTP
endpoint and no UI-click tool exists** for these two ImGui checkboxes (a
fresh `search_in_dir` across `src/Network/` this phase re-confirmed zero
matching routes; `get_available_tools` this phase confirmed no native
desktop UI-automation tool is available in my active/loadable tool set for
this non-browser application). I could not literally click these two
checkboxes this session either. The strongest evidence actually available,
gathered this phase:

- Both checkboxes render correctly, visible and unchecked (their default
  state), in every `GET /get_swapchain` screenshot taken this session —
  confirming `IEditorLayer::BuildUI()`'s own Scene-panel toolbar code
  (host-level, unmoved by this phase) still executes correctly every frame
  through `EditorHost::Run()`.
- Their underlying render-graph hook call sites
  (`AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()`,
  `AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`) —
  all four are Bucket 1 (render-graph-frame-building) `IEditorLayer` calls
  living INSIDE `Core::BuildFrame()`, reached through `Core`'s own
  null-checked `m_editorLayer` hook (Locked Design Decision #8) — were
  genuinely, repeatedly EXECUTED every single frame this whole session
  (guarded only by `sceneVisibleForBlurValidation`, true throughout, since
  the Scene panel was visible in every screenshot), with zero crash, zero
  log warning/error, across dozens of frames, multiple tab switches, a scene
  save/load round-trip, a GPU-driven-batch spawn, and a full Frame Debugger
  capture/select/channel/levels sequence — real, repeated exercise of the
  exact relocated-ownership code path, just with the toggle itself left at
  its default OFF value (both return `std::nullopt` every frame, exactly as
  PHASE13 already confirmed).
- The `GET /get_logs?since_id=0&limit=100` check above shows **zero**
  warnings/errors from `GBufferValidation`/`ComputeBlurValidation` across
  the entire session — the same class of evidence PHASE13's own report used
  to support its own equivalent conclusion.

This is flagged here loudly and honestly, per this campaign's own "no
silent-skip" discipline — **not** claimed as a full, literal "clicked the
checkbox and watched it turn on" verification, which genuinely did not
happen this session (identical caveat to PHASE13's own).

No `bug_report` was filed — no tool malfunctioned during this phase; the
checkbox-automation limitation above is a genuine ABSENCE of an automation
surface for this specific UI (confirmed, not assumed), not a broken tool
call.

## Definition of Done — checklist

- [x] Every automation bridge (`EngineCommandBridge`/`FrameCaptureBridge`/
      `EditorUiCommandBridge`/`FrameDebuggerCommandBridge`/
      `AssetImportCommandBridge`) plus the embedded `Network::NetworkServer`
      now lives on `EditorHost`, confirmed via direct code read — NONE live
      on `Core` (re-confirmed via `git_status`: `Core.h`/`Core.cpp` are
      untouched this phase).
- [x] Every HTTP endpoint this campaign touched is confirmed working live,
      end-to-end, through the new `EditorHost`-based executable —
      `/save_scene`, `/load_scene`, `/activate_tab`, `/list_tabs`,
      `/spawn_gpu_driven_test_batch`, `/get_swapchain`, `/get_game_view`,
      all 7 `/frame_debugger/*` routes, `/list_textures`, `/get_logs` — see
      the full endpoint table above.
- [x] The two render-graph-hook-dependent debug toggles' underlying code
      path re-exercised as thoroughly as this session's actual available
      tooling allows; the genuine automation-surface gap (no HTTP endpoint,
      no UI-click tool for this native desktop app) is documented explicitly
      above, not silently skipped — identical, honest treatment to
      PHASE13's own equivalent finding.
- [x] `PHASE16_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did **not** delete `Application.h`/`.cpp` — both remain present (heavily
  shrunk, genuinely dead/unused code, confirmed via `search_in_dir`), still
  compiling as part of `gte_editor`'s own source list, exactly as this
  phase's own "Out of Scope" section requires — Phase 17's job to actually
  delete them.
- Did **not** rename the executable target (`GreatTamanaEngine` stays
  as-is) — Phase 17's job.
- Did **not** touch `Core.h`/`Core.cpp` at all — confirmed via `git_status`
  (neither file appears in this phase's diff) — `Core` still holds nothing
  but the plain accessors + the nullable `IEditorLayer*` hook, with zero
  HTTP/automation-bridge awareness of any kind, exactly as design doc
  Section 6.1 requires.
- Did **not** build any new Bucket B capability adapter — `PHASE5`/`PHASE7`
  already confirmed `ISceneIOCapability` is the ONE real gap this campaign
  found; `EditorUiCapabilityImpl`/`EditorAssetImportCapabilityImpl`/
  `EditorGpuDrivenBatchTestCapabilityImpl` (named in this phase's own Step
  3.2) do not exist and were never needed — every one of those three
  questions is already, permanently answered by the pre-existing
  `IEditorLayer*` hook (Locked Design Decision #8), confirmed again this
  phase by the live `/activate_tab`/`/spawn_gpu_driven_test_batch` smoke
  checks succeeding through `EditorHost`'s own `m_editorLayer` unchanged.

## Files touched

- MODIFIED: `src/Editor/EditorHost.h` (added every automation-bridge member
  + `Renderer&`/`rg::RenderGraph&`/`Game&`/`EngineContext&`/
  `AtmosphereSettings&`/`AtmosphereLutRenderer&` reference members +
  `VolumeTexturePreviewRenderer` + `ISceneIOCapability*`)
- MODIFIED: `src/Editor/EditorHost.cpp` (real constructor wiring for every
  bridge/`NetworkServer`/`ISceneIOCapability`/`SetPresentImGuiRecorder`; the
  REAL `Run()` body, copied from `Application::Run()`'s own post-Phase-13
  shape)
- MODIFIED: `src/Application/Application.h` (shrunk further — every
  automation-bridge member/setter/include removed)
- MODIFIED: `src/Application/Application.cpp` (shrunk from 918 to 216 lines —
  every bridge-servicing/capture-publishing block removed, since the members
  they depended on no longer exist on this class)
- NEW: `task_manager/editor-core-separation-1/PHASE16_COMPLETION_REPORT.md`
  (this file)
