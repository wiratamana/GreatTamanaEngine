# PHASE2 — Panel shell, "Window" menu, and Editor wiring — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE2_PANEL_SHELL_AND_WINDOW_MENU_ENTRY.md`

## Summary

Implemented PHASE2's own "Step 3: The Plan" / "3.8 File-change inventory"
exactly as written, verbatim from the phase document's own code listings,
with no deviations. After this phase, the "Frame Debugger" window exists
and is openable/closeable via a brand-new "Window" top-level menu, showing
only a "Coming soon." placeholder body — exactly the intended, correct
state for this phase (no real frame-capture logic is wired in anywhere,
per PHASE0_MASTER_STRATEGY.md's Non-Goals).

### New files

- `src/Editor/Panels/FrameDebuggerPanel.h` — the panel class declaration
  (`class FrameDebuggerPanel { public: void Build(EditorContext& ctx); };`),
  copied verbatim from the phase document's §3.1 listing, including its
  full doc comment explaining the on-demand-floating-window precedent
  (`BoneViewerWindow`), the `EditorContext`-owned open/close bool rationale,
  and the "no `IEditorPanel` interface" convention.
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — `Build()`'s implementation:
  early-returns if `!ctx.frameDebuggerWindowOpen`; otherwise calls
  `ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver)`,
  `ImGui::Begin("Frame Debugger", &ctx.frameDebuggerWindowOpen)` (handling
  the collapsed/`false`-return case with the required `ImGui::End()`
  still called), then `ImGui::TextDisabled("Coming soon.");`, then
  `ImGui::End()`. Copied verbatim from the phase document's §3.2 listing.

### Modified files

- `src/Editor/EditorContext.h` — added `bool frameDebuggerWindowOpen = false;`
  immediately after `stepOneFrameRequested`'s own field + doc comment,
  before the struct's closing `};`, with the exact doc comment from the
  phase document's §3.3 listing.
- `src/Editor/DockLayout.cpp` — inside `BuildDockspaceAndMenuBar()`'s
  `if (ImGui::BeginMenuBar())` block, inserted a second top-level menu,
  `if (ImGui::BeginMenu("Window")) { ... ImGui::EndMenu(); }`, immediately
  after the existing `"File"` menu's own `ImGui::EndMenu();` and before
  `ImGui::EndMenuBar();`. Contains one checkable
  `ImGui::MenuItem("Frame Debugger", nullptr, &ctx.frameDebuggerWindowOpen);`,
  copied verbatim from the phase document's §3.4 listing.
- `src/Editor/ImGuiEditorLayer.cpp` — three insertions, all mirroring
  `JobsPanel`'s own precedent exactly, per the phase document's §3.5:
  1. `#include "Panels/FrameDebuggerPanel.h"` added alongside the other
     `Panels/*.h` includes near the top of the file (placed immediately
     after `Panels/AtmospherePanel.h`, alphabetically adjacent).
  2. `FrameDebuggerPanel m_frameDebuggerPanel;` added as a new private
     member, immediately after `JobsPanel m_jobsPanel;`, with the doc
     comment from the phase document's own listing explaining it is NOT
     docked alongside "Memory" (unlike the panels above it) but is a
     closeable floating window opened via the "Window" menu.
  3. `m_frameDebuggerPanel.Build(m_ctx);` added inside `BuildUI()`,
     immediately after `m_jobsPanel.Build(m_ctx, game);` and before the
     `#if GTE_ENABLE_PROJECT_PANEL` block, with a short comment noting
     `Build()` is a no-op unless the window is open.
- `CMakeLists.txt` — inside the `if(GTE_ENABLE_EDITOR)`
  `target_sources(gte_core PRIVATE ...)` call, added
  `src/Editor/Panels/FrameDebuggerPanel.h` and
  `src/Editor/Panels/FrameDebuggerPanel.cpp` immediately after
  `src/Editor/Panels/AtmospherePanel.cpp` and immediately before
  `src/Editor/ImGuiEditorLayer.cpp`, exactly as the phase document's
  §3.6 specified.

`src/Editor/EditorPanelCatalog.h` was **not** touched, per
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #6 — the Frame
Debugger is an on-demand floating window, not part of the default dock
layout, and is not surfaced through Network's `GET /activate_tab`/
`GET /list_tabs` (same precedent as `BoneViewerWindow`).

`tests/CMakeLists.txt` was **not** touched this phase — PHASE2 adds no new
Tier-1-testable pure logic (the panel class only calls into ImGui and the
already-tested `EditorContext` field), matching the phase document's own
file-change inventory (which lists no test file for this phase).

## Deviations from the phase document

None. Every file/insertion matched the phase document's own code listings
and described search anchors (`stepOneFrameRequested`'s field, the `"File"`
menu's `EndMenu()`/`EndMenuBar()` pair, `m_jobsPanel`'s three call sites,
`AtmospherePanel.cpp`'s position in `CMakeLists.txt`) on the first try, with
no line-number drift.

## Compile check (fast, per this phase's own instructions — not a full
clean rebuild/regression)

Ran exactly the command PHASE2's own "3.7 Compile check" section
specifies (this phase touches `ImGuiEditorLayer.cpp`/`DockLayout.cpp`,
which `GreatTamanaEngineTests` does not link against, so the app target
itself must be built to actually exercise this phase's changes):

```
cmake --build build --target GreatTamanaEngine
```

Result: **succeeded** — re-ran CMake configure automatically to pick up
the `CMakeLists.txt` change, then compiled
`src/Editor/Panels/FrameDebuggerPanel.cpp`, `src/Editor/DockLayout.cpp`,
and `src/Editor/ImGuiEditorLayer.cpp`, relinked `gte_core`, and relinked
`GreatTamanaEngine.exe` — no warnings or errors from any of the new/
modified files. (The only stderr output was CMake's own pre-existing,
unrelated `third_party/ktx` "No names found, cannot describe anything" /
"Falling back to 0.0.0-noversion" git-describe warning, seen on every
build of this repo regardless of this campaign.)

### Live runtime smoke test

1. `run_app_background` launched the freshly-built
   `build/GreatTamanaEngine.exe` (PID 30768).
2. `gte_send_request` `GET /get_swapchain` (Window menu never opened) —
   returned a `200 image/png` frame. Visually confirmed the Editor's menu
   bar now reads **"File   Window"** (a genuine, new, second top-level
   menu item next to the pre-existing "File" one), and every other panel
   (Hierarchy/Scene/Game/Inspector/Memory/Profiler/Render Graph/
   Atmosphere/Jobs/Project) still renders exactly as before — no visual
   regression anywhere else in the dockspace.
3. `stop_app_background` cleanly terminated the process.

**Deviation/limitation on the smoke test's second half:** the phase
document's own §3.7 says "then (if feasible from this environment) open
the 'Frame Debugger' item and capture again". This turned out **not to be
feasible** from this environment: the engine's embedded HTTP server (the
only remote-control surface available here, `gte_send_request`) exposes no
endpoint capable of clicking an arbitrary menu item — `GET /activate_tab`
only recognizes the fixed, always-docked panel names in
`EditorPanelCatalog.h` (Hierarchy/Inspector/Scene/Game/Memory/Profiler/
Render Graph/Jobs/Atmosphere/Project), and "Frame Debugger" is
deliberately **not** in that catalog (Locked Design Decision #6 — same
precedent as `BoneViewerWindow`), so there is no remote way to toggle
`ctx.frameDebuggerWindowOpen` without a real mouse/keyboard click on the
menu item itself. This is a pre-existing, expected limitation of the
remote-smoke-test tooling available in this environment, not a defect in
this phase's implementation — the code path itself
(`ImGui::Begin("Frame Debugger", ...)` behind
`if (!ctx.frameDebuggerWindowOpen) { return; }`) is identical in shape and
compiles/links successfully alongside every other panel that already uses
this exact pattern (`BoneViewerWindow`), so it is expected to behave
identically once opened by a human via the menu. No code change was made
to work around this; it is simply noted here as the phase document itself
anticipated ("if feasible").

## Next step

PHASE3 (`PHASE3_TOOLBAR_AND_FRAME_STEPPER.md`) — the "Enable" checkbox
(auto-engaging the existing Pause/Resume toolbar), the cosmetic disabled
"Editor" combo, and the disabled frame-stepper row, built on top of this
phase's window shell and PHASE1's pure helpers.
