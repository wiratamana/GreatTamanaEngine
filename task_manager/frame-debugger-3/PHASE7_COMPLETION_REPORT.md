# PHASE7 — HTTP automation + pinning the window to the main viewport — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md`
Parent: `PHASE0_MASTER_STRATEGY.md` (Locked Design Decision #9; flagged as the
SECOND-HIGHEST-RISK phase in the campaign per PHASE0's own Step 3.5)
Depends on: `PHASE1`..`PHASE6` (already landed).

## Summary

Implemented the phase's plan exactly as written: the main-viewport pin
(Step 3.4) was built and verified FIRST, in isolation, before any HTTP
plumbing existed; a brand-new, fully independent sibling bridge,
`FrameDebuggerCommandBridge` (`src/Application/FrameDebuggerCommandBridge.h`/
`.cpp`), was added — mirroring `EditorUiCommandBridge`'s exact mutex +
condition-variable + `SubmitAndWait()`/`TryPeekPendingCommandRequest()`/
`FulfillCommand()` shape, never extending `EditorUiCommandKind`; and eight
new HTTP routes (`GET /frame_debugger/open`, `/enable`, `/capture`,
`/select_event`, `/step_history`, `/set_channel`, `/set_levels`, `/state`)
were wired end-to-end through `NetworkRoutes.h`/`.cpp` (pure parsing/response
building) and `NetworkServer.cpp` (the only file that touches
`httplib::Server`/the bridge directly). The ENTIRE Frame Debugger feature is
now drivable with no mouse/keyboard at all, and a live runtime smoke test
(Section "Live smoke test" below) confirms the whole chain works, including
visual confirmation via `GET /get_swapchain` at every meaningful step.

## What was built

### 1. `src/Application/FrameDebuggerCommandBridge.h`/`.cpp` (new)

A full, independent sibling of `EditorUiCommandBridge` (per `AGENTS.md`'s own
explicit rule and PHASE0's Locked Design Decision #9 — never extended
`EditorUiCommandKind`). `FrameDebuggerCommandKind` has eight enumerators
(`OpenWindow`/`SetEnabled`/`CaptureNow`/`SelectEvent`/`StepFrameHistory`/
`SetChannel`/`SetLevels`/`GetState`), one tagged request struct
(`FrameDebuggerCommandRequest`, mirroring `EditorUiCommandRequest`'s
"exactly one field meaningful, selected by `kind`" convention — never
`std::variant`), and one tagged result struct
(`FrameDebuggerCommandResult`, carrying `success` plus a
`FrameDebuggerStateOutcome state` that is **always** populated, not just for
`GetState`). `SubmitAndWait()`/`TryPeekPendingCommandRequest()`/
`FulfillCommand()` are copied field-for-field from
`EditorUiCommandBridge.cpp`.

### 2. `src/Editor/EditorLayer.h` (modified)

- New, tiny, dependency-free `FrameDebuggerStateSnapshotView` struct
  (mirrors `TabActivationResult`'s own "Editor-owned, Application-independent"
  precedent).
- Eight new pure-virtual methods on `IEditorLayer`:
  `FrameDebuggerOpenWindow()`, `FrameDebuggerSetEnabled(bool)`,
  `FrameDebuggerCaptureNow()` (returns `bool`), `FrameDebuggerSelectEvent(int)`,
  `FrameDebuggerStepHistory(int)`, `FrameDebuggerSetChannel(const std::string&)`
  (returns `bool`), `FrameDebuggerSetLevels(float, float)`, and
  `FrameDebuggerGetState() const` (returns `FrameDebuggerStateSnapshotView`).

### 3. `src/Editor/NullEditorLayer.cpp` (modified)

All eight new methods stubbed as safe no-ops (matching every other
`IEditorLayer` method's existing "release build has nothing to do" contract).

### 4. `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (modified)

- New public HTTP-automation entry points: `RequestOpenWindow(EditorContext&)`,
  `SetEnabledFromCommand(EditorContext&, bool)`, `CaptureNowFromCommand()`,
  `SelectEventFromCommand(int)`, `StepFrameHistoryFromCommand(int)`,
  `SetChannelFromCommand(const std::string&)`, `SetLevelsFromCommand(float, float)`,
  `BuildStateSnapshotView(const EditorContext&) const` — each one mirrors
  exactly what its corresponding piece of hand-driven UI already does.
- **Refactor, not a behavior change**: the "Enable" checkbox's own
  false→true-edge effect (auto-engage Pause + trigger the first real
  capture) was extracted into a new private `ApplyEnabledEdge(EditorContext&,
  bool)` helper, shared by BOTH `BuildToolbarRow()`'s checkbox AND
  `SetEnabledFromCommand()` — so hand-driven UI and HTTP automation can never
  silently diverge.
- **The main-viewport pin (Step 3.4)**: a new `m_pinToMainViewportNextOpen`
  bool, set by `RequestOpenWindow()` only on a genuine
  `false -> true` PROGRAMMATIC transition of `ctx.frameDebuggerWindowOpen`
  (never touched by the manual "Window > Frame Debugger" menu item, which
  keeps its exact pre-existing drag-anywhere freedom). When true, `Build()`
  calls `ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID)` +
  `ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always)` +
  `ImGui::SetNextWindowSize(...)` (clamped to the main viewport's own
  `WorkSize` so it never overflows a smaller-than-900x600 work area) BEFORE
  `ImGui::Begin()`, then clears the bool — a genuine ONE-SHOT pin.

### 5. `src/Editor/ImGuiEditorLayer.cpp` (modified)

The eight new `IEditorLayer` overrides, each a one-line forward into
`m_frameDebuggerPanel`'s own matching method.

### 6. `src/Network/NetworkRoutes.h`/`.cpp` (modified)

Five new parsers (`ParseFrameDebuggerEnableQuery`, `ParseFrameDebuggerSelectEventQuery`,
`ParseFrameDebuggerStepHistoryQuery`, `ParseFrameDebuggerSetChannelQuery`,
`ParseFrameDebuggerSetLevelsQuery`) with the same rigor
`ParseGetTextureQuery()` already established (missing/invalid parameter ->
400 with a clear `errorMessage`, never a silent best guess), plus
`FrameDebuggerStateResponseView`/`BuildFrameDebuggerStateResponseJson()`/
`BuildFrameDebuggerCommandResponseJson()`. `set_channel`'s `value=all|r|g|b|a`
is completely separate from `/get_texture`'s own `channel=color|depth` — no
name/value collision anywhere (per PHASE0's Step 2 and PHASE6's own Step 2).

### 7. `src/Network/NetworkServer.h`/`.cpp` (modified)

`NetworkServer`'s constructor gained a fourth, appended, defaulted
`FrameDebuggerCommandBridge*` parameter (backward-compatible — every
existing call site, including all seven `NetworkServer server;`
zero-argument test constructions, keeps compiling unchanged). Eight new
routes registered in `RegisterRoutes()`, all sharing one small
`RespondWithFrameDebuggerCommandResult()` helper (mirrors `/activate_tab`'s
own `alreadyPending`/`timedOut` → 503/504 mapping) except `/state`, which has
its own flat, no-`"success"`-wrapper response shape.

### 8. `src/Application/Application.h`/`.cpp` (modified)

New `m_frameDebuggerCommandBridge` member (declared right after
`m_uiCommandBridge`, before `m_networkServer`, same construction-order
precedent every other bridge already follows), its address handed into
`NetworkServer`'s constructor, and a new pump block in `Run()` — placed at
the *exact same point* `EditorUiCommandBridge`'s own pump already runs
(right after `NewFrame()`, before `BuildUI()`) — that dispatches by `kind`,
reads `IEditorLayer::FrameDebuggerGetState()` back AFTER dispatching, and
calls `FulfillCommand()`.

### 9. `CMakeLists.txt` / `tests/CMakeLists.txt` (modified)

New source files registered; two new test files added (see "Compile check"
below).

## Deviations from the phase document (and why)

1. **`GET /frame_debugger/state` goes through the SAME bridge (a `GetState`
   command kind), not a lighter-weight direct/lock-free read.** The phase
   document explicitly asked to "confirm at implementation time whether a
   lighter-weight read-only path is safe, mirroring how `GET /list_tabs` is a
   simpler read than `GET /activate_tab`'s own full command round-trip".
   Investigated directly: `GET /list_tabs` needs no bridge at all because its
   ENTIRE data source (`EditorPanelCatalog.h`) is fixed at COMPILE TIME — no
   live/mutable state is ever touched. The Frame Debugger's state
   (`m_enabled`/`m_history`/`m_selectedEventIndex`/`m_channel`/
   `m_levelsBlack`/`m_levelsWhite`) is genuinely mutable, main-thread-owned
   data with **no atomics of its own** — reading it directly from the network
   thread would be a real (if narrow) data race, and no other read-only
   endpoint in this codebase (`GET /get_texture`, `GET /list_textures`) ever
   takes that shortcut either; both go through their own bridge's
   mutex-protected round-trip. Routing `/state` through the SAME bridge, as
   an 8th command kind, was judged the clearly right engineering call — it
   costs nothing (the same one pump call site already exists) and keeps this
   codebase's "never touch engine/Editor-owned mutable state from the
   network thread except through a reviewed bridge" rule (`AGENTS.md`,
   "Networking") completely intact, with zero exceptions introduced.
2. **Every `/frame_debugger/*` response's `"state"` field is populated for
   EVERY command, not just `GetState`.** Not explicitly required by the
   phase document, but a small, deliberate ergonomic choice matching its own
   stated Step 1 goal ("an AI verifier can assert state WITHOUT needing a
   screenshot after every single step") — an HTTP caller sees the resulting
   state immediately after e.g. `/enable` or `/capture`, without a second,
   separate `/state` round-trip. Confirmed genuinely useful during the live
   smoke test below.
3. **`ApplyEnabledEdge()` extraction is a refactor, not a plan requirement**,
   done to guarantee hand-driven UI and HTTP automation can never silently
   diverge in the auto-pause/first-capture side effect — a pure
   code-organization improvement with no behavior change to the pre-existing
   "Enable" checkbox path (verified: the existing PHASE1–PHASE6 test suite,
   35 tests, still passes unchanged — see below).
4. Everything else matches the phase document's plan exactly: the
   main-viewport pin was built and manually verified FIRST, in isolation
   (via a throwaway `/frame_debugger/open` call, since the bridge existed by
   the time this was actually tested — the plan's own "or via `OpenWindow`
   once the bridge exists" alternative), before any other HTTP route was
   registered; `EditorUiCommandBridge`/`EditorUiCommandKind` were never
   touched; `EditorPanelCatalog.h`/`GET /activate_tab`/`GET /list_tabs` were
   never touched; the preview texture was NOT published into
   `RenderGraphDebugTextureRegistry` (explicitly optional per Step 3.5, and
   not needed — the panel's own `ImGui::Image()` display plus the new
   `/frame_debugger/*` routes are already a complete, sufficient automation
   surface).

## Compile check (per this phase's own Step 3.6)

1. Fast, scoped compile check (`GreatTamanaEngine` target, existing `build`
   directory, `GTE_ENABLE_EDITOR=ON`):
   ```
   cmake --build build --target GreatTamanaEngine
   ```
   Result: **succeeded** — `FrameDebuggerCommandBridge.cpp`,
   `Panels/FrameDebuggerPanel.cpp`, `NetworkRoutes.cpp`, `Application.cpp`,
   `ImGuiEditorLayer.cpp`, `NetworkServer.cpp` all recompiled cleanly and the
   full executable relinked successfully.
2. Built `GreatTamanaEngineTests` and ran every new/affected test:
   ```
   cmake --build build --target GreatTamanaEngineTests
   tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*:*ParseFrameDebugger*:*BuildFrameDebugger*
   ```
   Result: **all 62 tests passed** across 16 suites, including:
   - 7 new `FrameDebuggerCommandBridgeTest` tests (mirroring
     `EditorUiCommandBridgeTests.cpp`'s own exact coverage shape: timeout,
     fulfilled-result, already-pending, no-op-fulfill-when-idle,
     peek-when-idle, late-fulfillment-after-timeout, pending-state
     observability).
   - 18 new `NetworkRoutesTests`-family tests for the five new parsers and
     two new response builders (including a `TEST_P`-parameterized "accepts
     every documented channel" case, and an explicit "never collides with
     `/get_texture`'s own `color`/`depth` values" regression check).
   - Every pre-existing PHASE1–PHASE6 Frame Debugger test (35 tests) still
     passes unchanged, confirming the `ApplyEnabledEdge()` refactor
     introduced no behavior change.
3. **Also verified the `GTE_ENABLE_EDITOR=OFF` configuration** (`build-editor-off`)
   compiles cleanly with these same changes (`NullEditorLayer.cpp`'s eight
   new stub overrides, `Application.cpp`, `NetworkRoutes.cpp`,
   `NetworkServer.cpp`) — not strictly required by this phase's own Step 3.6
   (which only names the Editor-ON build/test targets), but a cheap,
   worthwhile extra check since `EditorLayer.h`/`NullEditorLayer.cpp` are
   core, always-compiled files this phase touched directly.

No full clean build and no full `ctest` regression suite were run in this
phase — reserved for PHASE8 only, per both this phase's own Step 3.6 and
`PHASE0_MASTER_STRATEGY.md`'s Step 3.6/"Order of work".

## Live smoke test (REQUIRED by this phase, per its own Step 3.6)

Launched the real `GreatTamanaEngine.exe` in the background and drove it
entirely over HTTP (`gte_send_request`), confirming every step visually via
`GET /get_swapchain`. Exact call sequence and results:

1. **`GET /get_swapchain`** (baseline, before anything) → `200`. Shows the
   default Editor layout (Hierarchy/Scene/Game/Inspector/Memory/Project tabs)
   — **no Frame Debugger window anywhere** (confirms the starting state is
   genuinely "closed").
2. **`GET /frame_debugger/open`** → `200`,
   `{"state":{"channel":"all","enabled":false,"historyCount":0,"historyCursor":0,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":0,"windowOpen":true},"success":true}`.
3. **`GET /get_swapchain`** → `200`. **The Frame Debugger window is now
   visible, pinned inside the main viewport's own work area (top-left,
   under the menu bar), on the VERY FIRST captured frame after opening** —
   this is the Step 3.4 main-viewport-pin requirement, directly confirmed:
   an ordinary programmatic open never escapes to its own OS-level platform
   window, exactly as designed. Toolbar shows "Enable" (unchecked),
   "Capture" (disabled), the cosmetic "Editor" combo, a "Frame 0 of 0" Frame
   History row, a "0 of 0" event stepper, and "Enable Frame Debugger above
   to inspect the current frame's render events."
4. **`GET /frame_debugger/enable?value=true`** → `200`,
   `{"state":{"channel":"all","enabled":true,"historyCount":1,"historyCursor":0,...,"totalEventCount":1,"windowOpen":true},"success":true}`
   — the false→true edge auto-triggered the very first real capture
   (`historyCount` went from 0 to 1), exactly mirroring the hand-driven
   checkbox's own documented behavior.
5. **`GET /frame_debugger/capture`** → `200`,
   `{"state":{...,"historyCount":2,"historyCursor":1,"totalEventCount":1,...},"success":true}`
   — a second, explicit capture landed (`historyCount` 1 → 2).
6. **`GET /frame_debugger/state`** → `200`,
   `{"channel":"all","enabled":true,"historyCount":2,"historyCursor":1,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":1,"windowOpen":true}`
   — confirms the flat, no-`"success"`-wrapper shape.
7. **`GET /get_swapchain`** → `200`. Visually confirms: "Enable" is CHECKED,
   "Capture" is now clickable, Frame History reads "Frame 2 of 2", the event
   tree shows a real "Game View" group with one real "GameView" leaf, the
   RenderTarget row reads "GameView", the resolution caption reads "403x333
   Texture (BGRA8 UNORM)", and the preview box shows the ACTUAL rendered Game
   View contents (a real sky-gradient render) — end-to-end open/enable/
   capture/state confirmed working over HTTP with zero mouse/keyboard
   involved.
8. **`GET /frame_debugger/select_event?index=0`** → `200`,
   `selectedEventIndex: 0`.
9. **`GET /frame_debugger/step_history?direction=prev`** → `200`,
   `historyCursor: 0` (moved from 1 → 0); `selectedEventIndex` still reads
   `0` in THIS SAME response, because the state snapshot is read at the
   bridge-pump point (right after `NewFrame()`), one step BEFORE `Build()`'s
   own "did the viewed history entry change" detection runs later that same
   frame.
10. **`GET /frame_debugger/state`** (a later, separate frame) → `200`,
    `selectedEventIndex: -1` — confirms `Build()`'s existing
    reset-on-history-navigation logic DID run and correctly cleared the
    stale selection, exactly the same one-frame-lag every other
    Editor↔engine feedback loop in this codebase already documents/accepts
    (e.g. `IsPlaybackPaused()`'s own doc comment) — an honest, expected
    timing artifact, not a bug.
11. **`GET /frame_debugger/set_channel?value=r`** → `200`, `"channel":"r"`.
12. **`GET /frame_debugger/set_levels?black=0.2&white=0.8`** → `200`,
    `"levelsBlack":0.2`, `"levelsWhite":0.8`.
13. **`GET /get_swapchain`** → `200`. Visually confirms: the "R" Channels
    button is highlighted/active, the Levels row reads "Black 0.20"/"White
    0.80", Frame History reads "Frame 1 of 2" (matching the `step_history`
    call), and — the real proof this reaches all the way down to PHASE6's
    compute-shader preview pipeline — **the preview image is now a
    grayscale, contrast-stretched isolation of the red channel** (a smooth
    vertical gradient, matching the original sky gradient's own red-channel
    profile under a `[0.2, 0.8]` levels remap), instead of the original
    full-color image from step 7.

Every one of the required endpoints (`open`/`enable`/`capture`/`state`, plus
`select_event`/`step_history`/`set_channel`/`set_levels`) was exercised
end-to-end against a live running engine, with the main-viewport pin
confirmed visually via `/get_swapchain` at multiple points throughout. The
app was stopped cleanly afterward (`stop_app_background`).

## File-change inventory

New:
- `src/Application/FrameDebuggerCommandBridge.h`
- `src/Application/FrameDebuggerCommandBridge.cpp`
- `tests/Application/FrameDebuggerCommandBridgeTests.cpp`
- `task_manager/frame-debugger-3/PHASE7_COMPLETION_REPORT.md` (this file)

Modified:
- `src/Editor/EditorLayer.h` (new `FrameDebuggerStateSnapshotView` struct +
  eight new pure-virtual methods)
- `src/Editor/NullEditorLayer.cpp` (eight new no-op stub overrides)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (new HTTP-automation entry
  points, `ApplyEnabledEdge()` refactor, main-viewport-pin logic)
- `src/Editor/ImGuiEditorLayer.cpp` (eight new `IEditorLayer` overrides,
  forwarding into `m_frameDebuggerPanel`)
- `src/Network/NetworkRoutes.h`/`.cpp` (five new parsers, two new response
  builders)
- `src/Network/NetworkServer.h`/`.cpp` (fourth bridge constructor parameter,
  eight new routes, `RespondWithFrameDebuggerCommandResult()`/
  `ToFrameDebuggerStateResponseView()` helpers)
- `src/Application/Application.h`/`.cpp` (new `m_frameDebuggerCommandBridge`
  member, `NetworkServer` construction updated, new per-frame command pump)
- `CMakeLists.txt` (new `gte_core` sources)
- `tests/CMakeLists.txt` (new test file registered)
- `tests/Network/NetworkRoutesTests.cpp` (18 new tests appended)

## Next step

PHASE8 (`PHASE8_INTEGRATION_BUILD_DOCS_AND_FULL_VERIFICATION.md`) — final
build-system wiring, full doc sweep (`AGENTS.md`/`docs/`/`README.md`/
`TODO.md`), a full clean build (both `GTE_ENABLE_EDITOR=ON` and `=OFF`) +
full `ctest` regression, and a final, comprehensive HTTP-automation-driven
end-to-end smoke test closing out the whole `frame-debugger-3` campaign.
