# PHASE2 — Draggable Frame-Step Slider (Feature 3)

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first, especially Locked Design
Decisions #5 and #6, which this phase implements. Independent of Phase 1 - can be
implemented before or after it, but this document assumes Phase 1's file state as a
baseline since the campaign's own phase table orders it first._

## Step 1: The Goal

The horizontal "frame step" slider (the one showing `N of M` next to it, directly under
the toolbar row) must become a REAL, interactive control: dragging it with the mouse, or
pressing Left/Right arrow keys while it has focus, changes which event/leaf is selected
— exactly as if the user had clicked that event's row in the left-hand tree — and the
Inspector pane's preview/details update to match, live.

## Step 2: The Situation

`src/Editor/Panels/FrameDebuggerPanel.cpp`:

```cpp
void FrameDebuggerPanel::BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot)
{
    int stepperValue = m_selectedEventIndex < 0 ? 0 : m_selectedEventIndex;
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt(
        "##FrameDebuggerStepper", &stepperValue, 0, std::max(0, snapshot.totalEventCount - 1), "");
    ImGui::SameLine();
    const std::string label = FormatFrameStepperLabel(m_selectedEventIndex, snapshot.totalEventCount);
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDisabled();
}
```

The whole row is wrapped in `ImGui::BeginDisabled()`/`EndDisabled()` — the slider is
drawn purely for visual parity with the reference screenshot, and never actually
accepts input. `m_selectedEventIndex` (the field that actually drives everything else —
tree-row highlighting, `BuildEventDetailsSection()`, `EnsurePreviewDescriptor()`) is
currently mutated from exactly two places: `RenderEventNode()` (tree-row click) and
`SelectEventFromCommand()` (the HTTP `select_event` route). `ClampSelectedEventIndex()`
(`FrameDebuggerData.h`, already implemented and already Tier-1-tested) is the existing,
correct clamp rule ("`-1` whenever `totalEventCount <= 0`; otherwise clamp into `[0,
totalEventCount - 1]`") — this phase reuses it, never reimplements it.

`snapshot.totalEventCount` is exactly the number of real, selectable leaves this
captured frame produced (every Pre-GameView compute leaf + the `GameView` leaf itself +
its per-entity/sky children + every Post-GameView compute leaf, in true chronological
order) — the slider's integer value already lines up 1:1 with `m_selectedEventIndex`
with zero remapping needed.

`ImGuiKey_*` + `ImGui::IsKeyPressed(key, /*repeat=*/true)` are already used elsewhere in
this codebase (`src/Editor/DockLayout.cpp`, for `Ctrl+S`/`Ctrl+O`) — confirming this
exact API is available and safe to use here too.

## Step 3: The Plan

### 3.1 — New chokepoint: `SetSelectedEventIndex()`

Add to `FrameDebuggerPanel.h`'s private section (near `EnsurePreviewDescriptor()`):

```cpp
// task_manager/frame-debugger-9 campaign, PHASE2 - the ONE place
// m_selectedEventIndex is ever assigned from now on: a tree-row click
// (RenderEventNode()), the new draggable/arrow-key-nudgeable frame-step slider
// (BuildFrameStepperRow(), this same phase), TriggerCapture()'s own existing
// reset-to- -1 on a fresh capture, and SelectEventFromCommand()'s HTTP path all
// route through this one method - always already-clamped via
// ClampSelectedEventIndex() by the CALLER (this method itself does not
// re-derive totalEventCount, keeping it a trivial, dependency-free setter).
// PHASE3 (task_manager/frame-debugger-9) extends this same method with one
// extra call: whenever the value actually CHANGES, it also releases any
// currently-displayed shader-property one-shot texture preview (see that
// phase's own doc comment on ReleaseShaderPropertyTexturePreview()) - "the
// user went elsewhere" per that feature's own locked lifecycle rule. This
// phase (PHASE2) only introduces the chokepoint itself and the "did it
// actually change" comparison it will need either way - PHASE3 supplies the
// actual release call, gated behind a forward-compatible no-op stub call site
// left ready for it (see this method's own PHASE2-era body below).
void SetSelectedEventIndex(int newIndex);
```

`.cpp` body (PHASE2's own initial version — PHASE3 will add one line inside this same
function, not change its shape):

```cpp
void FrameDebuggerPanel::SetSelectedEventIndex(int newIndex)
{
    if (newIndex == m_selectedEventIndex) {
        return; // No real change - nothing to release/refresh.
    }
    m_selectedEventIndex = newIndex;
    // PHASE3 (task_manager/frame-debugger-9) adds: ReleaseShaderPropertyTexturePreview();
    // here - "the user went elsewhere" (Locked Design Decision #2,
    // PHASE0_MASTER_STRATEGY.md) - left as this phase's own explicit extension
    // point rather than speculatively adding an empty method call now.
}
```

Now migrate every existing direct `m_selectedEventIndex = ...` assignment to call this
instead:

- `RenderEventNode()` (both the group-with-children branch and the leaf branch): replace
  `m_selectedEventIndex = node.eventIndex;` with `SetSelectedEventIndex(node.eventIndex);`.
- `TriggerCapture()`: replace `m_selectedEventIndex = -1;` with
  `SetSelectedEventIndex(-1);`.
- `SelectEventFromCommand()`: replace
  `m_selectedEventIndex = ClampSelectedEventIndex(index, totalEventCount);` with
  `SetSelectedEventIndex(ClampSelectedEventIndex(index, totalEventCount));`.

Do NOT change `Build()`'s own direct reads of `m_selectedEventIndex` (e.g.
`FindEventDetailsByIndex(snapshot, m_selectedEventIndex)`, the raw-preview-view-changed
reset block) — those are reads, not the mutation this chokepoint governs; leave them
exactly as-is.

### 3.2 — Make the slider real

Replace `BuildFrameStepperRow()`'s body:

```cpp
void FrameDebuggerPanel::BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot)
{
    int stepperValue = m_selectedEventIndex < 0 ? 0 : m_selectedEventIndex;
    const bool hasAnyEvents = snapshot.totalEventCount > 0;

    ImGui::BeginDisabled(!hasAnyEvents);
    ImGui::SetNextItemWidth(200.0f);
    // task_manager/frame-debugger-9 campaign, PHASE2 - REAL, interactive control
    // (was a purely cosmetic, always-disabled slider before this campaign).
    // ImGui::SliderInt() returns true on EVERY value change while being
    // actively dragged (not just on mouse release), so this already satisfies
    // "drag and see the preview update immediately" with no extra plumbing.
    if (ImGui::SliderInt(
            "##FrameDebuggerStepper", &stepperValue, 0, std::max(0, snapshot.totalEventCount - 1), "")) {
        SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue, snapshot.totalEventCount));
    }

    // task_manager/frame-debugger-9 campaign, PHASE2 - Left/Right arrow-key
    // nudge, one event at a time, while this slider itself has keyboard focus
    // (ImGui::IsItemFocused() refers to the SliderInt just drawn above - must
    // be checked immediately after it, before any other widget steals focus
    // tracking). `repeat = true` mirrors holding the key down to keep
    // stepping, matching ordinary OS scrollbar/slider key-repeat behavior.
    if (hasAnyEvents && ImGui::IsItemFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, /*repeat=*/true)) {
            SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue - 1, snapshot.totalEventCount));
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, /*repeat=*/true)) {
            SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue + 1, snapshot.totalEventCount));
        }
    }

    ImGui::SameLine();
    const std::string label = FormatFrameStepperLabel(m_selectedEventIndex, snapshot.totalEventCount);
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDisabled();
}
```

Notes on this exact shape:
- `stepperValue` used by the arrow-key branch is the value BEFORE this frame's slider
  edit was applied to `m_selectedEventIndex` — using `stepperValue - 1`/`stepperValue +
  1` (rather than re-reading `m_selectedEventIndex`, which `SetSelectedEventIndex()`
  from the SliderInt branch above may have just changed THIS SAME CALL) is intentional:
  only one of the two branches (`SliderInt` drag OR arrow key) can meaningfully fire in
  a single ImGui frame in practice (a slider being actively dragged does not also have
  "just pressed left/right arrow" true in the same frame), so this ordering is safe
  either way, but reading `stepperValue` keeps the arrow-key math simple/local rather
  than re-deriving `m_selectedEventIndex`'s freshest value.
- `ImGui::BeginDisabled(!hasAnyEvents)` replaces the old unconditional
  `ImGui::BeginDisabled()` — a fresh/never-captured session (`totalEventCount == 0`)
  still shows a visibly greyed-out, non-interactive `0 of 0` slider, matching this
  window's existing "honest empty state" convention elsewhere (never silently hide the
  control, never let it be interactively meaningless either).
- No change needed to `FormatFrameStepperLabel()` itself — already correct.

### 3.3 — Accepted, pre-existing latency note (do not "fix" this in this phase)

`EnsurePreviewDescriptor()` already runs, once per `Build()` call, BEFORE
`BuildFrameStepperRow()`/`BuildEventTreePane()` (the tree-click path) get a chance to
run this same frame — meaning a selection change (via slider, arrow key, OR the
pre-existing tree click) only visibly updates the preview image on the FOLLOWING ImGui
frame (one ImGui frame later, well under 16 ms at any reasonable frame rate). This is
already true for tree clicks today and is imperceptible to a human dragging/clicking —
do NOT reorder `Build()`'s call sequence to "fix" this; it is not a regression this
phase introduces, and reordering risks disturbing other carefully-sequenced state
(`m_lastKnownRawPreviewView` selection-reset detection, `m_pinToMainViewportNextOpen`,
etc.) for no user-visible benefit.

### 3.4 — Verification for this phase

1. Incremental compile check only.
2. No new pure-function test is introduced by this phase (`ClampSelectedEventIndex()`
   already has full existing coverage; this phase is a thin ImGui-wiring change,
   Tier-2/untested-by-this-codebase's-own-convention, same bucket as every other
   ImGui-only wiring in this class).
3. OPTIONAL but encouraged: `run_app_background` + drive `GET /frame_debugger/enable`
   + `/capture`, then manually verify (or, since HTTP automation cannot literally drag
   a slider, at minimum confirm via `GET /frame_debugger/select_event?index=N` still
   working through the new `SetSelectedEventIndex()` chokepoint with byte-identical
   behavior to before this phase) that selection still works correctly end to end.

### 3.5 — Deliverables

- `src/Editor/Panels/FrameDebuggerPanel.h` — new private `SetSelectedEventIndex()`
  declaration.
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — `SetSelectedEventIndex()` body,
  `BuildFrameStepperRow()` rewritten, `RenderEventNode()`/`TriggerCapture()`/
  `SelectEventFromCommand()` migrated to call it instead of assigning
  `m_selectedEventIndex` directly.
- `task_manager/frame-debugger-9/PHASE2_COMPLETION_REPORT.md` — written once the above
  compiles; commit together via `git_add`/`git_commit` on `feature/frame-debugger-impl`.
