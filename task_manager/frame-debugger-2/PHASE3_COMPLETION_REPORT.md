# PHASE3 — Toolbar row (Enable + "Editor" stub) and frame-stepper row — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE3_TOOLBAR_AND_FRAME_STEPPER.md`

## Summary

Implemented PHASE3's own "Step 3: The Plan" / "3.5 File-change inventory"
exactly as written, verbatim from the phase document's own code listings,
with no deviations. After this phase, the "Frame Debugger" window's
PHASE2 "Coming soon." placeholder is replaced by its real top section: a
toggleable **"Enable"** checkbox (one-directionally auto-engaging the
existing Pause/Resume playback toolbar per Locked Design Decision #3), a
cosmetic, permanently-disabled **"Editor"** mode combo, and a disabled
frame-stepper row that always reads **"0 of 0"**. Below that, the body
still shows an explanatory disabled message either way (unchecked:
"Enable Frame Debugger above..."; checked: the explicit "(event tree +
inspector - added in a later phase of this campaign)" placeholder) — this
is the correct, intended, final state for this phase, not a bug or an
unfinished stub — real frame/draw-call capture logic is never wired in
anywhere in this whole campaign (see `PHASE0_MASTER_STRATEGY.md`'s
"Locked Design Decisions"/"Non-Goals").

### Modified files (only these two, matching the phase document's §3.5
file-change inventory exactly — no new files this phase)

- `src/Editor/Panels/FrameDebuggerPanel.h`:
  - Added `#include "../FrameDebuggerData.h"` (the full header, per the
    phase document's own preference to avoid repeated partial-forward-
    declaration churn across phases) so `BuildFrameStepperRow()`'s body
    (and PHASE4/5/6's future additions) can use `FrameDebuggerSnapshot`/
    `FormatFrameStepperLabel()` etc.
  - Added a `private:` section to `FrameDebuggerPanel` with two new
    method declarations, `BuildToolbarRow(EditorContext&)` and
    `BuildFrameStepperRow()`, plus the new `bool m_enabled = false;`
    member and its doc comment — copied verbatim from the phase
    document's §3.1 listing.
- `src/Editor/Panels/FrameDebuggerPanel.cpp`:
  - Added `BuildToolbarRow(EditorContext& ctx)`: an `ImGui::Checkbox("Enable",
    &m_enabled)` that, only on the false→true transition this exact
    frame, sets `ctx.playbackPaused = true` (the same field
    `PlaybackControls.cpp`'s own "Pause" button writes) — with the
    in-code comment explicitly documenting the intentional ON-only,
    non-symmetric behavior (per Locked Design Decision #3) so a future
    reader doesn't "fix" it into a symmetric toggle. Followed by a
    `ImGui::SameLine()` and a permanently `ImGui::BeginDisabled()`-wrapped
    single-item `ImGui::Combo("##FrameDebuggerMode", ...)` showing only
    `"Editor"`.
  - Added `BuildFrameStepperRow()`: a disabled `ImGui::SliderInt("##FrameDebuggerStepper",
    &stepperValue, 0, 0, "")` followed by `FormatFrameStepperLabel(-1, 0)`'s
    text (`"0 of 0"`), both inside one `ImGui::BeginDisabled()`/`EndDisabled()`
    pair.
  - Replaced `Build()`'s PHASE2 `ImGui::TextDisabled("Coming soon.");`
    body with: `BuildToolbarRow(ctx)`, a `Separator()`, `BuildFrameStepperRow()`,
    another `Separator()`, then the `if (!m_enabled) { ... } else { ... }`
    branch exactly as the phase document's §3.3 listing specifies.

Every line matches the phase document's own code listings verbatim — no
line-number drift, no adaptation needed.

## Deviations from the phase document

None in the implementation itself. One deviation in the smoke test's
scope, identical in nature to the one already noted and accepted in
`PHASE2_COMPLETION_REPORT.md`: the phase document's §3.4 says to confirm
the cross-feature wiring "if interactive input is feasible in this
environment" by checking "Enable" and observing the main Pause/Resume
toolbar flip to "(Paused)"/"Resume". This remains **not feasible** from
this environment for the same underlying reason PHASE2 already
documented — the Frame Debugger window is deliberately not part of
`EditorPanelCatalog.h` (Locked Design Decision #6), so `GET /activate_tab`
cannot bring it to front, and the embedded HTTP server (`gte_send_request`,
the only remote-control surface available here) has no endpoint capable of
clicking an arbitrary menu item or an in-window checkbox — there is no way
to flip `EditorContext::frameDebuggerWindowOpen` (let alone the panel's
own private `m_enabled` member) without a real mouse click. This is a
pre-existing, expected limitation of the remote-smoke-test tooling in this
environment (already surfaced verbatim in PHASE2), not a defect introduced
by this phase. The `BuildToolbarRow()` code path itself is a direct,
verbatim copy of the exact idiom `PlaybackControls.cpp`'s own "Pause"
button already uses for writing `ctx.playbackPaused` (see
`docs/conventions/time-and-playback-pause.md`), so it is expected to
behave identically once triggered by a human via a real click. No code
change was made to work around this; it is simply noted here as the phase
document itself anticipated ("if feasible").

## Compile check (fast, per this phase's own §3.4 instructions — not a
full clean rebuild/regression)

Ran exactly the command PHASE3's own "3.4 Compile check" section
specifies:

```
cmake --build build --target GreatTamanaEngine
```

Result: **succeeded** — compiled the modified
`src/Editor/Panels/FrameDebuggerPanel.cpp` and the transitively-affected
`src/Editor/ImGuiEditorLayer.cpp` (rebuilt because it includes
`FrameDebuggerPanel.h`, which now pulls in `FrameDebuggerData.h`),
relinked `libgte_core.a`, and relinked `GreatTamanaEngine.exe` — no
warnings or errors from any of the new/modified files.

### Live runtime smoke test

1. `run_app_background` launched the freshly-built
   `build/GreatTamanaEngine.exe` (PID 20076).
2. `gte_send_request` `GET /get_swapchain` — returned a `200 image/png`
   frame; visually confirmed the main Editor menu bar still reads
   "File   Window" and the existing Pause/Resume toolbar ("Pause"/"Step"
   buttons, top-left) render exactly as before — no visual regression
   from this phase's changes (the Frame Debugger window itself is closed
   by default, `EditorContext::frameDebuggerWindowOpen` starting `false`,
   so nothing new is visible on this particular screenshot — expected).
3. `gte_send_request` `GET /list_tabs` — confirmed the response still
   lists exactly the same ten pre-existing panels
   (`Hierarchy`/`Inspector`/`Scene`/`Game`/`Memory`/`Profiler`/
   `Render Graph`/`Jobs`/`Atmosphere`/`Project`) with **no** "Frame
   Debugger" entry — re-confirming Locked Design Decision #6 (this
   campaign never touches `EditorPanelCatalog.h`) still holds after this
   phase's changes.
4. `gte_send_request` `GET /activate_tab?name=Game` followed by another
   `GET /get_swapchain` — confirmed the full default dock layout
   (Hierarchy/Scene/Game/Inspector/Memory/Profiler/Render Graph/
   Atmosphere/Jobs/Project) still renders correctly with real scene
   content (sky gradient) in both the Scene and Game panels, and the
   Pause/Resume toolbar still shows its normal "Pause"/"Step" state — no
   cross-panel regression anywhere else in the Editor from this phase's
   `FrameDebuggerPanel.h`/`.cpp` changes.
5. `stop_app_background` cleanly terminated the process.

As with PHASE2, actually opening the "Frame Debugger" window and clicking
its new "Enable" checkbox to observe the live cross-feature wiring to
`ctx.playbackPaused` could not be exercised remotely in this environment
(see "Deviations" above) — the code itself is a verbatim, reviewed copy
of the phase document's own listing and the pre-existing
`PlaybackControls.cpp` idiom it deliberately mirrors.

## Next step

PHASE4 (`PHASE4_EVENT_TREE_PANE_AND_SPLITTER.md`) — the left-hand
draggable-splitter event tree pane, showing "No frame captured yet." per
Locked Design Decision #2, plus a fully-written (currently unreachable)
recursive tree-row renderer ready for real data. Not started as part of
this phase.
