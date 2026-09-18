# PHASE2 — Draggable Frame-Step Slider (Feature 3) — COMPLETION REPORT

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_DRAGGABLE_FRAME_STEP_SLIDER.md` exactly as specified — no material
deviations from the phase document were needed._

## What was done

Implemented exactly the plan in `PHASE2_DRAGGABLE_FRAME_STEP_SLIDER.md`, Step
3, with no material deviation:

1. **`src/Editor/Panels/FrameDebuggerPanel.h`** — added the new private
   `void SetSelectedEventIndex(int newIndex);` chokepoint declaration
   (Locked Design Decision #6, `PHASE0_MASTER_STRATEGY.md`), placed right
   after `EnsurePreviewDescriptor()` as the phase document specifies, with a
   full doc comment matching this codebase's existing density/tone,
   explicitly noting PHASE3's future extension point.

2. **`src/Editor/Panels/FrameDebuggerPanel.cpp`**:
   - Added `FrameDebuggerPanel::SetSelectedEventIndex(int newIndex)` right
     after `TriggerCapture()` — an early-return no-op when `newIndex` equals
     the current value, otherwise writes `m_selectedEventIndex` (with a
     comment marking exactly where PHASE3 will add its one extra
     `ReleaseShaderPropertyTexturePreview()` call, per the phase document's
     own Step 3.1 body).
   - `BuildFrameStepperRow()` rewritten exactly per Step 3.2: the slider is
     no longer wrapped in an unconditional `ImGui::BeginDisabled()` — it's
     now `ImGui::BeginDisabled(!hasAnyEvents)`, so it stays a real,
     interactive control whenever a frame has been captured (`0 of 0`
     remains visibly greyed-out/non-interactive when nothing has been
     captured yet, matching this window's existing "honest empty state"
     convention). Dragging the `ImGui::SliderInt()` now calls
     `SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue,
     snapshot.totalEventCount))` on every value change (which `SliderInt()`
     already reports continuously while being dragged, not just on
     release — no extra plumbing needed for "drag and see the preview
     update immediately"). A new `ImGui::IsItemFocused()` check right after
     the slider handles Left/Right arrow-key nudges (`repeat = true`,
     matching ordinary OS scrollbar/slider key-repeat behavior), one event
     at a time, routed through the same chokepoint.
   - `RenderEventNode()` — both direct `m_selectedEventIndex = node.eventIndex;`
     assignments (the group-with-children/selectable-leaf branch and the
     plain-leaf `Selectable()` branch) now call
     `SetSelectedEventIndex(node.eventIndex);` instead.
   - `TriggerCapture()` — `m_selectedEventIndex = -1;` (the existing
     reset-on-fresh-capture) now calls `SetSelectedEventIndex(-1);`.
   - `SelectEventFromCommand()` (the HTTP `select_event` route) —
     `m_selectedEventIndex = ClampSelectedEventIndex(index, totalEventCount);`
     now calls `SetSelectedEventIndex(ClampSelectedEventIndex(index,
     totalEventCount));`.
   - Left completely untouched, per the phase document's explicit
     instruction: `Build()`'s own direct READS of `m_selectedEventIndex`
     (`FindEventDetailsByIndex()`, `BuildStateSnapshotView()`'s
     `view.selectedEventIndex = m_selectedEventIndex;`), and the
     raw-preview-view-changed reset block inside `Build()`
     (`m_selectedEventIndex = -1;` guarded by
     `newRawPreviewView != previousRawPreviewView`) — that is a
     capture-invalidation reset tied to the retained texture's own
     `VkImageView` identity, not a user-facing selection change, and the
     phase document explicitly calls out that this must stay a direct
     assignment, not route through the new chokepoint.

No other file was touched. No new pure-function/Tier-1 test was added, per
the phase document's own Step 3.4 — this phase is a thin ImGui-wiring change
(the untested-by-this-codebase's-own-convention bucket, same as every other
ImGui-only wiring in this class); `ClampSelectedEventIndex()` itself already
has full existing coverage and was not modified.

## Verification evidence

1. **Targeted incremental compile check** (no full/clean rebuild):
   - `cmake --build build --target GreatTamanaEngine` — succeeded, rebuilding
     `FrameDebuggerPanel.cpp`/`ImGuiEditorLayer.cpp`, relinking
     `libgte_core.a` and `GreatTamanaEngine.exe` (4 build steps, 0
     errors/warnings related to this change).

2. **Live, HTTP-driven, screenshot-verified smoke test** — launched
   `build\GreatTamanaEngine.exe` via `run_app_background`, then drove it over
   the embedded HTTP server:
   - `GET /frame_debugger/open` → window opened.
   - `GET /frame_debugger/enable?value=true` → enabled.
   - `GET /frame_debugger/capture` → `hasCapturedFrame: true`,
     `totalEventCount: 8`.
   - `GET /frame_debugger/select_event?index=3` → `selectedEventIndex: 3` in
     the returned state — confirms `SelectEventFromCommand()` still works
     correctly end-to-end now that it routes through the new
     `SetSelectedEventIndex()` chokepoint (byte-identical externally-visible
     behavior to before this phase, per the phase document's own Step 3.4
     point 3).
   - `GET /get_swapchain` — **screenshot confirms the frame-step slider now
     visibly shows its handle at position 4/8 (matching `selectedEventIndex
     == 3`, 0-based) and the label reads "4 of 8"**, in sync with the
     left-hand tree's own highlighted row (`AtmosphereAerialPerspectiveVolumePass`)
     and the Inspector's "Event #3: Compute Dispatch" details section — all
     driven through the very same chokepoint a live mouse-drag would use.
     (HTTP automation cannot literally perform a mouse drag or send a key
     press, so this indirect confirmation — the same code path
     `SetSelectedEventIndex()` — is the phase document's own explicitly
     endorsed verification method, Step 3.4 point 3.)
   - `GET /frame_debugger/enable?value=false` to leave the engine in a clean
     state, then `stop_app_background` to close it.

## Deviations from the strategy document

None. Every deliverable in Step 3.5 of
`PHASE2_DRAGGABLE_FRAME_STEP_SLIDER.md` was produced exactly as specified,
including leaving the two explicitly-called-out "reads, not the mutation
this chokepoint governs" sites in `Build()` untouched.

## Files changed

- `src/Editor/Panels/FrameDebuggerPanel.h`
- `src/Editor/Panels/FrameDebuggerPanel.cpp`
- `task_manager/frame-debugger-9/PHASE2_COMPLETION_REPORT.md` (this file)
