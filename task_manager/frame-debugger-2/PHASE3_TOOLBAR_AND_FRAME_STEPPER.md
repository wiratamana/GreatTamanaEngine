# PHASE3 — Toolbar row (Enable + "Editor" stub) and frame-stepper row

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (`FormatFrameStepperLabel()`,
`BuildPlaceholderFrameDebuggerSnapshot()`), PHASE2 (the window shell
exists).
Touches: `src/Editor/Panels/FrameDebuggerPanel.h/.cpp` only.

## Step 1: The Goal

Replace PHASE2's "Coming soon." placeholder with the window's real top
section: an **"Enable"** checkbox (which also auto-engages the existing
Pause/Resume toolbar — Locked Design Decision #3), a cosmetic disabled
**"Editor"** combo, and a disabled frame-stepper row reading **"0 of 0"**.
Below that, when NOT enabled, show a single explanatory message; when
enabled, show an (still currently empty) content area — PHASE4/PHASE5/
PHASE6 fill that area in.

## Step 2: The Situation

- `src/Editor/PlaybackControls.cpp`'s `BuildPlaybackToolbar()` is the
  direct, already-landed precedent for "a checkbox/button that writes
  `ctx.playbackPaused` directly" (see its own body: `if
  (ImGui::Button(ctx.playbackPaused ? "Resume" : "Pause")) { ctx.playbackPaused
  = !ctx.playbackPaused; }`). This phase's "Enable" checkbox writes that
  *same* field, one-directionally (see Locked Design Decision #3 — ON
  sets it true; OFF does not touch it).
- `ImGui::BeginDisabled()`/`ImGui::EndDisabled()` is already used
  elsewhere in this codebase for a permanently-inert control (see
  `RenderGraphPanel.cpp`'s "Export DOT" button, and
  `PlaybackControls.cpp`'s own `Step` button while not paused) — copy
  that exact idiom for the "Editor" combo.
- `ImGui::Combo("##Label", &currentIndex, itemsArray, itemCount)` is Dear
  ImGui's simplest string-array combo overload — use a single-element
  `static constexpr const char* kModeItems[] = { "Editor" };` and a
  local `int modeIndex = 0;` that is never persisted anywhere (it can
  never meaningfully change while the combo stays disabled).
- `FrameDebuggerData.h`'s `FormatFrameStepperLabel(int, int)` (PHASE1)
  is the exact function to call for the "N of M" text — call it with
  `(-1, 0)` this phase (no selection, zero total) to get `"0 of 0"`,
  matching the reference screenshot's own layout (a numeric field
  followed by "of N" static text) but fully disabled since there is
  nothing to step through.

## Step 3: The Plan

### 3.1 `FrameDebuggerPanel.h` — new private members

Add, inside the `private:` section:

```cpp
private:
    void BuildToolbarRow(EditorContext& ctx);
    void BuildFrameStepperRow();

    // The panel's own "Enable" toggle (see FrameDebuggerData.h's own
    // top-of-file comment: this is a purely GUI concept this campaign -
    // toggling it does not turn on any real capture logic yet, only
    // this window's own placeholder/inert content vs. an explanatory
    // "please enable" message). False by default - a freshly-opened
    // window starts disabled, matching Unity's own Frame Debugger.
    bool m_enabled = false;
```

(`#include "../FrameDebuggerData.h"` also needs adding to the .h file,
since `BuildFrameStepperRow()`'s eventual body — and PHASE4/5/6's own
additions — depend on `FrameDebuggerSnapshot`/etc.; alternatively forward-
declare just what this phase strictly needs and let PHASE4 add the full
include if preferred — either is acceptable, but prefer including the
whole header now to avoid repeated partial-forward-declaration churn
across phases.)

### 3.2 `FrameDebuggerPanel.cpp` — toolbar row

```cpp
void FrameDebuggerPanel::BuildToolbarRow(EditorContext& ctx)
{
    const bool wasEnabled = m_enabled;
    ImGui::Checkbox("Enable", &m_enabled);
    if (m_enabled && !wasEnabled) {
        // task_manager/frame-debugger-2 campaign, Locked Design Decision
        // #3 (PHASE0_MASTER_STRATEGY.md): turning Enable ON auto-engages
        // the existing Pause/Resume playback toolbar (frame-debugger-1
        // campaign) - the SAME ctx.playbackPaused field
        // PlaybackControls.cpp's own "Pause" button writes. Turning
        // Enable back OFF deliberately does NOT auto-resume - see this
        // file's own BuildToolbarRow() comment for why (a user
        // inspecting a paused frame should not be silently un-paused
        // just for closing/disabling this debug window).
        ctx.playbackPaused = true;
    }

    ImGui::SameLine();

    // Cosmetic-only stub - this engine has no Play/Edit-mode split (see
    // frame-debugger-1's own Locked Design Decision #1), so there is
    // nothing real for this control to switch between yet; it exists
    // purely for visual parity with the reference screenshot. Permanently
    // disabled - never becomes interactive by any state change in this
    // campaign.
    static constexpr const char* kModeItems[] = { "Editor" };
    int modeIndex = 0;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::BeginDisabled();
    ImGui::Combo("##FrameDebuggerMode", &modeIndex, kModeItems, 1);
    ImGui::EndDisabled();
}

void FrameDebuggerPanel::BuildFrameStepperRow()
{
    // Always "0 of 0" this campaign - see FrameDebuggerData.h's
    // FormatFrameStepperLabel() doc comment. Rendered as a disabled
    // slider (visual parity with the reference screenshot's scrubber)
    // plus the same text label a real implementation will show.
    int stepperValue = 0;
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("##FrameDebuggerStepper", &stepperValue, 0, 0, "");
    ImGui::SameLine();
    const std::string label = FormatFrameStepperLabel(-1, 0);
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDisabled();
}
```

### 3.3 `FrameDebuggerPanel::Build()` — wire the new rows in

Replace the PHASE2 placeholder body with:

```cpp
void FrameDebuggerPanel::Build(EditorContext& ctx)
{
    if (!ctx.frameDebuggerWindowOpen) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Frame Debugger", &ctx.frameDebuggerWindowOpen)) {
        ImGui::End();
        return;
    }

    BuildToolbarRow(ctx);
    ImGui::Separator();
    BuildFrameStepperRow();
    ImGui::Separator();

    if (!m_enabled) {
        ImGui::TextDisabled("Enable Frame Debugger above to inspect the current frame's render events.");
    } else {
        // PHASE4/PHASE5/PHASE6 fill this branch in (event tree pane +
        // splitter + inspector pane). Left as an explicit, clearly
        // labeled placeholder for now rather than an empty branch, so a
        // reader mid-campaign can tell this is intentionally
        // unfinished, not accidentally empty.
        ImGui::TextDisabled("(event tree + inspector - added in a later phase of this campaign)");
    }

    ImGui::End();
}
```

### 3.4 Compile check

`cmake --build build --target GreatTamanaEngine`, then a live smoke test
(`run_app_background` + `gte_send_request` `/get_swapchain`) confirming:
the toolbar row shows a clickable "Enable" checkbox and a visibly grayed-
out "Editor" combo; the stepper row shows a grayed-out slider and "0 of 0"
text; unchecked, the body shows the "Enable Frame Debugger above..."
message; checking "Enable" (if interactive input is feasible in this
environment) flips the message to the PHASE4-placeholder line AND causes
the existing Pause/Resume toolbar (top of the main Editor, not this
window) to show "(Paused)"/"Resume" — confirming the cross-feature wiring
from Locked Design Decision #3 actually fires. Then `stop_app_background`.

### 3.5 File-change inventory (this phase only)

Modified only: `src/Editor/Panels/FrameDebuggerPanel.h`,
`src/Editor/Panels/FrameDebuggerPanel.cpp`.

Write `PHASE3_COMPLETION_REPORT.md` into `task_manager/frame-debugger-2/`
when done, then `git_add`/`git_commit`.
