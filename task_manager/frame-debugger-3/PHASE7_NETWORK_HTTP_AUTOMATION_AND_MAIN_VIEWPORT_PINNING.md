# PHASE7 — HTTP automation + pinning the window to the main viewport

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decision #9; this is the
SECOND highest-risk phase in the campaign per PHASE0's Step 3.5 — take the extra care that flag
implies).
## Depends on: `PHASE1`..`PHASE6` (already landed).

## Step 1: The Goal

Make the ENTIRE Frame Debugger feature drivable end-to-end over the existing embedded HTTP server,
with NO mouse/keyboard involved at all: open the window, enable it, capture/step through frame
history, select an event, toggle channels/levels — AND guarantee the window is always visible to
`GET /get_swapchain` while doing so, permanently closing the "manual verification limitation" both
`frame-debugger-1` and `frame-debugger-2` had to accept.

## Step 2: The Situation

- **The "escapes the main viewport" trap is real and must be fixed as this phase's FIRST sub-task**
  (see PHASE0's Step 2): `ImGuiConfigFlags_ViewportsEnable` is on for the whole Editor
  (`ImGuiEditorLayer.cpp`), so any floating window CAN become an independent OS-level platform
  window — invisible to `GET /get_swapchain`, which only ever reads back the MAIN window's own
  swapchain image (`SwapchainCaptureService.h`). `DockLayout.cpp`'s own
  `ImGui::SetNextWindowPos(viewport->WorkPos)` call (used for the main dockspace host window) is
  the exact, already-proven-working precedent for forcing a window's placement — this phase copies
  that same technique, PLUS an explicit `ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID)`
  call (the one piece `DockLayout.cpp`'s own precedent doesn't need, since the dockspace host is
  already the main-viewport window by construction) to prevent the Frame Debugger window from ever
  being classified as a separate platform window in the first place.
- **`EditorUiCommandBridge` is the exact SHAPE to copy, but must NOT be extended directly**
  (`AGENTS.md`'s own explicit rule, and `EditorUiCommandBridge.h`'s own header comment: "a genuinely
  new KIND of request gets its own bridge, never a new enum value bolted onto an existing bridge
  built for an unrelated purpose"). Frame Debugger control is unambiguously a NEW kind of request —
  create `src/Application/FrameDebuggerCommandBridge.h`/`.cpp` as a full, independent sibling,
  copying `EditorUiCommandBridge`'s mutex+condition-variable+`SubmitAndWait()`+
  `TryPeekPendingCommandRequest()`+`FulfillCommand()` shape field-for-field.
- **Where does the network route handler reach the Panel's own state?** Never directly (route
  handlers run on a background thread — `AGENTS.md`, "Networking") — only through this new bridge,
  pumped once per frame from `Application::Run()`, exactly where `EditorUiCommandBridge`'s own
  pending-command pump already lives today (find that exact call site and add a parallel one for
  the new bridge, not a divergent mechanism).
- Existing precedent for "how does NetworkRoutes.h stay pure/httplib-free": `ParsedGetTextureQuery`/
  `ParseGetTextureQuery()`'s own shape (`NetworkRoutes.h`) is the pattern every new
  `/frame_debugger/*` route's own query-parsing/response-building pure functions must copy —
  `NetworkServer.cpp` is the only file allowed to touch `httplib::Server`/the actual bridge
  directly, per this codebase's existing, consistent layering.

## Step 3: The Plan

### 3.1 New bridge: `FrameDebuggerCommandBridge`

```cpp
enum class FrameDebuggerCommandKind {
    OpenWindow,       // sets ctx.frameDebuggerWindowOpen = true, pins to main viewport (see 3.4)
    SetEnabled,       // bool payload - mirrors the "Enable" checkbox's own false->true/true->false edges
    CaptureNow,       // forces PHASE3's TriggerCapture() this frame
    SelectEvent,      // int payload - sets m_selectedEventIndex (clamped via ClampSelectedEventIndex())
    StepFrameHistory, // int delta payload (+1/-1) - calls FrameDebuggerHistory::StepCursor()
    SetChannel,       // string payload ("all"|"r"|"g"|"b"|"a") - sets m_channel (PHASE6)
    SetLevels,        // two float payload (black, white) - sets m_levelsBlack/m_levelsWhite (PHASE6)
};
// One request/result struct pair per kind, exactly mirroring EditorUiCommandRequest/Result's own
// tagged-struct convention (never std::variant) - see EditorUiCommandBridge.h for the field-by-
// field shape to replicate.
```

`SubmitAndWait()` (network thread) / `TryPeekPendingCommandRequest()`+`FulfillCommand()` (main
thread) copy `EditorUiCommandBridge`'s own exact implementation shape.

### 3.2 New routes (`NetworkRoutes.h`/`.cpp` pure helpers, `NetworkServer.cpp` registration)

- `GET /frame_debugger/open` -> `OpenWindow`.
- `GET /frame_debugger/enable?value=true|false` -> `SetEnabled`.
- `GET /frame_debugger/capture` -> `CaptureNow`.
- `GET /frame_debugger/select_event?index=<int>` -> `SelectEvent`.
- `GET /frame_debugger/step_history?direction=prev|next` -> `StepFrameHistory`.
- `GET /frame_debugger/set_channel?value=all|r|g|b|a` -> `SetChannel` (note the deliberately
  DIFFERENT parameter name/values from `/get_texture`'s own `channel=color|depth` — see PHASE0's
  Step 2 and PHASE6's Step 2 for exactly why these must never collide).
- `GET /frame_debugger/set_levels?black=<float>&white=<float>` -> `SetLevels`.
- `GET /frame_debugger/state` -> a READ-ONLY JSON status endpoint (`{"enabled":bool,
  "windowOpen":bool,"historyCount":int,"historyCursor":int,"totalEventCount":int,
  "selectedEventIndex":int,"channel":"all","levelsBlack":0.0,"levelsWhite":1.0}`) — this one does
  NOT need the full bridge round-trip if the relevant state can be read more cheaply/directly
  (confirm at implementation time whether a lighter-weight read-only path is safe, mirroring how
  `GET /list_tabs` is a simpler read than `GET /activate_tab`'s own full command round-trip) so an
  AI verifier can assert state WITHOUT needing a screenshot after every single step.

Every route validates its own query parameters with the SAME rigor `ParseGetTextureQuery()`
already established (missing/invalid parameter -> 400 with a clear `errorMessage`, never a silent
best-guess default for anything that could be a caller mistake).

### 3.3 `Application::Run()` wiring

Construct `FrameDebuggerCommandBridge` alongside the existing `m_captureBridge`/`m_commandBridge`/
`m_editorUiCommandBridge` (BEFORE `NetworkServer`, so its address can be passed into
`NetworkServer`'s constructor — mirror `EditorUiCommandBridge`'s own exact construction-order
precedent), then pump its pending command once per frame (a `TryPeekPendingCommandRequest()` +
dispatch-by-kind + `FulfillCommand()` block) at the same point in the loop
`EditorUiCommandBridge`'s own pump already runs.

### 3.4 The main-viewport pin (do this as this phase's very first code change, verify it in
isolation before building anything else in this phase)

Inside `FrameDebuggerPanel::Build()`, track a new small bool (e.g. `m_pinToMainViewportNextOpen`),
set to `true` exactly when `OpenWindow`/any other command from the new bridge causes
`ctx.frameDebuggerWindowOpen` to transition `false -> true` PROGRAMMATICALLY (never when a human
toggles the "Window > Frame Debugger" menu item by hand — that path should stay exactly as free to
drag around as `BoneViewerWindow` already is, per `frame-debugger-2`'s own precedent, which this
phase must not regress for ordinary human/manual use). When that bool is true, for that one
`Build()` call: call `ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID)`, then
`ImGui::SetNextWindowPos(...)`/`ImGui::SetNextWindowSize(...)` with a fixed, generous size (e.g.
900x600) anchored inside the main viewport's own work area (`ImGui::GetMainViewport()->WorkPos`/
`WorkSize`, mirroring `DockLayout.cpp`'s own exact accessor), THEN call `ImGui::Begin(...)` as
usual, then clear the bool back to `false` (a one-shot pin, not a permanent lock — a human should
still be able to drag it away afterward if they want to, exactly matching every other floating
window's existing freedom). Verify this specific behavior FIRST, alone, with a manual test before
building the rest of this phase's HTTP plumbing on top of it: open the window via a temporary,
throwaway debug code path (or via `OpenWindow` once the bridge exists) and confirm `GET
/get_swapchain` genuinely shows it on the FIRST frame it opens.

### 3.5 What this phase explicitly does NOT do

- Does not change `EditorUiCommandBridge`/`EditorUiCommandKind` in any way (Locked Design
  Decision #9 — a brand new bridge only).
- Does not add "Frame Debugger" to `EditorPanelCatalog.h` or `GET /activate_tab`/`GET /list_tabs` —
  those remain scoped to the permanently-docked default layout, per `frame-debugger-2`'s own
  unchanged, still-correct Locked Design Decision #6 from that campaign. The new
  `/frame_debugger/*` routes are this window's OWN, separate, purpose-built automation surface.
- Does not publish the preview texture into `RenderGraphDebugTextureRegistry`/`GET /get_texture`
  unless implementation time reveals it is genuinely free/trivial to do alongside everything else
  here — it is explicitly OPTIONAL, not required for this phase to be considered complete.

### 3.6 Compile check

Fast compile check (`GreatTamanaEngine` + `GreatTamanaEngineTests`). A live runtime smoke test
specifically exercising the main-viewport pin (3.4) plus at least `open`/`enable`/`capture`/`state`
end-to-end via `gte_send_request`-equivalent HTTP calls, confirmed via `/get_swapchain`, is required
before this phase is considered done (this is the phase that makes automated verification possible
at all — prove it actually works here, do not defer proving it entirely to PHASE8).

### 3.7 File-change inventory

New: `src/Application/FrameDebuggerCommandBridge.h`, `src/Application/FrameDebuggerCommandBridge.cpp`.
Modified: `src/Network/NetworkRoutes.h`/`.cpp`, `src/Network/NetworkServer.h`/`.cpp`,
`src/Application/Application.h`/`.cpp`, `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (viewport
pin + reading the new bridge's pumped commands), `CMakeLists.txt`.

Write `PHASE7_COMPLETION_REPORT.md` once done, including the exact HTTP call sequence used for the
live smoke test and what `/get_swapchain` showed at each step.
