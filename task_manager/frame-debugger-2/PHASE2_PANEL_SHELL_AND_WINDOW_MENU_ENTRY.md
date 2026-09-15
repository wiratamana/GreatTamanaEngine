# PHASE2 — Panel shell, "Window" menu, and Editor wiring

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (`FrameDebuggerData.h` exists, but is not yet used here
- this phase's window body is still just a placeholder message).
Touches: new `src/Editor/Panels/FrameDebuggerPanel.h/.cpp`; modifies
`src/Editor/EditorContext.h`, `src/Editor/DockLayout.cpp`,
`src/Editor/ImGuiEditorLayer.cpp`, `CMakeLists.txt`.

## Step 1: The Goal

Make the "Frame Debugger" window **exist and be openable/closeable** —
nothing more. After this phase: a brand-new **Window** top-level menu
appears in the Editor's menu bar with one checkable item, "Frame
Debugger"; clicking it opens a floating window titled "Frame Debugger"
showing only a "Coming soon" placeholder line; clicking the item again
(or the window's own `[x]`) closes it; reopening it remembers nothing
special (no persisted content yet - that starts in PHASE3).

## Step 2: The Situation

- See `PHASE0_MASTER_STRATEGY.md`, Step 2, for the full precedent
  analysis (`BoneViewerWindow`'s "no-op unless open" shape vs. why THIS
  window's open/close bool must live in `EditorContext` instead of a
  private member — because the trigger, a `DockLayout.cpp` menu item, is
  a free function with no reference to a `FrameDebuggerPanel` instance).
- `src/Editor/EditorContext.h` — the struct's very last field today is
  `stepOneFrameRequested` (see its file, near the end, right before the
  closing `};`). Add the new field right after it, with a doc comment in
  the same style as every other field there (see e.g.
  `playbackPaused`'s own comment immediately above it for the expected
  tone/format).
- `src/Editor/DockLayout.cpp`'s `BuildDockspaceAndMenuBar()` — the
  `if (ImGui::BeginMenuBar())` block currently contains exactly one
  `if (ImGui::BeginMenu("File")) { ... ImGui::EndMenu(); }`, immediately
  followed by `ImGui::EndMenuBar();`. Insert a second
  `if (ImGui::BeginMenu("Window")) { ... ImGui::EndMenu(); }` block
  between those two lines.
- `src/Editor/ImGuiEditorLayer.cpp` — `#include "Panels/JobsPanel.h"` sits
  alongside the other `Panels/*.h` includes near the top; `JobsPanel
  m_jobsPanel;` is a private member near the bottom of the class; and
  `m_jobsPanel.Build(m_ctx, game);` is called inside `BuildUI()`,
  immediately before the `#if GTE_ENABLE_PROJECT_PANEL` block. Mirror all
  three insertion points exactly for `FrameDebuggerPanel`.
- `CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)` — `src/Editor/
  Panels/JobsPanel.h`/`.cpp` sit right before `src/Editor/Panels/
  AtmospherePanel.h`/`.cpp`, which sit right before
  `src/Editor/ImGuiEditorLayer.cpp` itself (the last line of that
  `target_sources` call). Insert the two new panel files anywhere in that
  list — right after the `AtmospherePanel.*` pair, immediately before
  `src/Editor/ImGuiEditorLayer.cpp`, is the most natural spot (newest
  panel last).
- **Do not** touch `src/Editor/EditorPanelCatalog.h` in this phase (or
  any phase) — see `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision
  #6 and PHASE7's explicit documentation of why.

## Step 3: The Plan

### 3.1 New file: `src/Editor/Panels/FrameDebuggerPanel.h`

```cpp
#pragma once

namespace gte {

struct EditorContext;

// task_manager/frame-debugger-2 campaign (PHASE2) - the Editor's
// "Frame Debugger" window: a Unity-Frame-Debugger-style tool for
// inspecting one captured frame's draw-call/render-pass hierarchy. An
// ON-DEMAND FLOATING WINDOW (like BoneViewerWindow.h), NOT part of the
// default dock layout and NOT listed in EditorPanelCatalog.h (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6) - opened/closed
// via a checkable "Window > Frame Debugger" menu item
// (DockLayout.cpp's BuildDockspaceAndMenuBar()) that flips
// EditorContext::frameDebuggerWindowOpen directly; Build() below is a
// complete no-op whenever that bool is false, mirroring
// BoneViewerWindow::Build()'s own "no-op unless open" shape exactly,
// just gated on a shared EditorContext bool instead of a private member
// (see this class's own .cpp file for why: the thing that opens this
// window, DockLayout.cpp, has no reference to a FrameDebuggerPanel
// instance to call an Open()-style method on).
//
// THIS CAMPAIGN IS GUI-ONLY (see PHASE0_MASTER_STRATEGY.md's Non-Goals) -
// every value this window displays is a disabled control, a placeholder
// message, or an inert default; see FrameDebuggerData.h for the pure
// data model this class reads from.
//
// A small STATEFUL CLASS, not a stateless free function - mirrors
// RenderGraphPanel/ProfilerPanel/BoneViewerWindow's own precedent
// (AGENTS.md, "Editor Module Structure" pre-approves this exception):
// this panel owns its own "Enable" toggle, currently-selected event
// index, and splitter width across frames. Still called explicitly BY
// NAME from ImGuiEditorLayer::BuildUI() - no IEditorPanel interface
// introduced.
class FrameDebuggerPanel {
public:
    void Build(EditorContext& ctx);
};

} // namespace gte
```

(`m_enabled`/`m_selectedEventIndex`/`m_leftPaneWidth` private members are
added in PHASE3/PHASE4 — this phase's class genuinely has no state of its
own yet beyond what `EditorContext` already holds, so keep the class
body this minimal for now; do not pre-add members this phase doesn't use.)

### 3.2 New file: `src/Editor/Panels/FrameDebuggerPanel.cpp`

```cpp
#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

namespace gte {

void FrameDebuggerPanel::Build(EditorContext& ctx)
{
    if (!ctx.frameDebuggerWindowOpen) {
        return;
    }

    // Passing &ctx.frameDebuggerWindowOpen as p_open keeps the window's
    // own titlebar [x] close button in sync with the SAME bool the
    // "Window > Frame Debugger" menu item flips (DockLayout.cpp) -
    // clicking either one closes/reopens the same shared state, with no
    // extra code needed on either side.
    ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Frame Debugger", &ctx.frameDebuggerWindowOpen)) {
        // Collapsed - still need End() (Dear ImGui's own Begin()/End()
        // pairing contract requires End() even when Begin() returns
        // false), but nothing worth drawing.
        ImGui::End();
        return;
    }

    // PHASE3 replaces this placeholder with the real toolbar/stepper/
    // tree/inspector content.
    ImGui::TextDisabled("Coming soon.");

    ImGui::End();
}

} // namespace gte
```

### 3.3 `src/Editor/EditorContext.h` — new field

Add immediately after `stepOneFrameRequested`'s own field + comment
block, before the closing `};`:

```cpp
    // task_manager/frame-debugger-2 campaign (PHASE2) - true whenever
    // the Editor's on-demand "Frame Debugger" floating window
    // (Panels/FrameDebuggerPanel.h) is currently open. Flipped directly
    // by DockLayout.cpp's checkable "Window > Frame Debugger" menu item
    // (ImGui::MenuItem's own 3-argument overload writes straight through
    // a bool*), and passed as ImGui::Begin()'s own p_open parameter by
    // FrameDebuggerPanel::Build() - so the window's titlebar [x] close
    // button keeps this exact same bool in sync automatically, in both
    // directions. False by default (closed on a fresh session) -
    // mirrors BoneViewerWindow's own default-closed-until-opened
    // behavior, just via a shared EditorContext bool instead of a
    // private member (see FrameDebuggerPanel.h's own class comment for
    // why).
    bool frameDebuggerWindowOpen = false;
```

### 3.4 `src/Editor/DockLayout.cpp` — new "Window" menu

Inside `BuildDockspaceAndMenuBar()`'s `if (ImGui::BeginMenuBar())` block,
immediately after the existing `if (ImGui::BeginMenu("File")) { ...
ImGui::EndMenu(); }` and before `ImGui::EndMenuBar();`, insert:

```cpp
        if (ImGui::BeginMenu("Window")) {
            // task_manager/frame-debugger-2 campaign (PHASE2) - the
            // Frame Debugger is an on-demand floating window (like
            // BoneViewerWindow), not part of the default dock layout,
            // so it needs its own explicit way to be (re)opened once
            // closed - a checkable menu item bound directly to
            // ctx.frameDebuggerWindowOpen (see EditorContext.h). The
            // checkmark reflects the window's actual current open state
            // even if it was closed via its own titlebar [x] rather than
            // this menu.
            ImGui::MenuItem("Frame Debugger", nullptr, &ctx.frameDebuggerWindowOpen);
            ImGui::EndMenu();
        }
```

### 3.5 `src/Editor/ImGuiEditorLayer.cpp` — wiring

1. Add `#include "Panels/FrameDebuggerPanel.h"` alongside the other
   `Panels/*.h` includes near the top of the file (any position among
   them is fine — they are not order-sensitive; placing it near
   `Panels/JobsPanel.h`'s own include keeps related "docked-alongside-
   Memory" debug panels visually grouped, though this one is not
   actually docked there — purely a readability choice).
2. Add a new private member, near `JobsPanel m_jobsPanel;`:
   ```cpp
   // task_manager/frame-debugger-2 campaign (PHASE2) - the Editor's
   // on-demand "Frame Debugger" floating window (Panels/
   // FrameDebuggerPanel.h). Unlike m_jobsPanel/m_profilerPanel/
   // m_renderGraphPanel above, this one is NOT docked alongside
   // "Memory" - it is a closeable floating window, opened via the
   // "Window" menu (DockLayout.cpp) - but it is still owned here and
   // called explicitly by name from BuildUI() below, exactly like every
   // other stateful panel.
   FrameDebuggerPanel m_frameDebuggerPanel;
   ```
3. Add the call inside `BuildUI()`, immediately after
   `m_jobsPanel.Build(m_ctx, game);` and before the
   `#if GTE_ENABLE_PROJECT_PANEL` block:
   ```cpp
   m_frameDebuggerPanel.Build(m_ctx);
   ```

### 3.6 `CMakeLists.txt` — new source files

Inside the `if(GTE_ENABLE_EDITOR)` `target_sources(gte_core PRIVATE ...)`
call, add (immediately after `src/Editor/Panels/AtmospherePanel.cpp`,
immediately before `src/Editor/ImGuiEditorLayer.cpp`):

```
src/Editor/Panels/FrameDebuggerPanel.h
src/Editor/Panels/FrameDebuggerPanel.cpp
```

### 3.7 Compile check

Full incremental build of the `GreatTamanaEngine` app target (this phase
touches `ImGuiEditorLayer.cpp`/`DockLayout.cpp`, which the test binary
does not link against — a `GreatTamanaEngineTests`-only build would NOT
catch a mistake here; see `frame-debugger-1`'s own PHASE3 completion
report for this exact build-hygiene gotcha already having bitten this
codebase once):
`cmake --build build --target GreatTamanaEngine`. Then a live runtime
smoke test: `run_app_background` the built `.exe`, `gte_send_request`
`/get_swapchain` once with the Window menu never opened (menu bar should
show "File" and "Window"), then (if feasible from this environment) open
the "Frame Debugger" item and capture again to confirm the floating
window appears with its "Coming soon." placeholder text, then
`stop_app_background`.

### 3.8 File-change inventory (this phase only)

New: `src/Editor/Panels/FrameDebuggerPanel.h`,
`src/Editor/Panels/FrameDebuggerPanel.cpp`.
Modified: `src/Editor/EditorContext.h`, `src/Editor/DockLayout.cpp`,
`src/Editor/ImGuiEditorLayer.cpp`, `CMakeLists.txt`.

Write `PHASE2_COMPLETION_REPORT.md` into `task_manager/frame-debugger-2/`
when done, then `git_add`/`git_commit`.
