# PHASE7 — Completion Report: Integration confirmation, docs, full build + regression validation

Phase file: `PHASE7_INTEGRATION_BUILD_WIRING_AND_DOCS.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`

## Summary

Implemented PHASE7's own "Step 3: The Plan" exactly as written. This is the
closeout phase of the `frame-debugger-2` campaign — no new UI, no new
capability. PHASE1-6 had already landed the complete GUI-only "Frame
Debugger" window (data model, panel shell, toolbar/stepper, event tree
pane + splitter, inspector frame-level chrome, and the event-details
section/tab bar), all confirmed already fully wired into the build system.
This phase's job was to (1) re-confirm every build-system wiring point
genuinely exists (per its own §3.1 checklist), (2) write the documentation
sweep every completed campaign in this repository makes, (3) run a full
clean build in BOTH Editor configurations, a full `ctest` regression pass,
and a live runtime smoke test, and (4) write both completion reports.

## 3.1 — Build-system confirmation

Re-opened every file PHASE7's own checklist names and confirmed each line
already existed (all landed correctly in PHASE1/PHASE2, nothing needed
fixing):

- `CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)` (inside
  `if(GTE_ENABLE_EDITOR)`) already lists `src/Editor/FrameDebuggerData.h`,
  `src/Editor/FrameDebuggerData.cpp`, `src/Editor/Panels/FrameDebuggerPanel.h`,
  `src/Editor/Panels/FrameDebuggerPanel.cpp` (confirmed via `search_in_dir`
  — lines 605-606 and 625-626).
- `tests/CMakeLists.txt`'s `list(APPEND GTE_TEST_SOURCES ...)` (inside its
  own `if(GTE_ENABLE_EDITOR)` block) already lists
  `Editor/FrameDebuggerDataTests.cpp` (line 1910).
- `src/Editor/EditorContext.h` already has `frameDebuggerWindowOpen` (a
  plain `bool`, defaulting to `false`, with a full doc comment explaining
  the "Window" menu / titlebar `[x]` two-way sync).
- `src/Editor/DockLayout.cpp` already has the "Window" top-level menu
  (immediately after "File", inside the same `BeginMenuBar()` block) with
  the checkable `ImGui::MenuItem("Frame Debugger", nullptr,
  &ctx.frameDebuggerWindowOpen)`.
- `src/Editor/ImGuiEditorLayer.cpp` already has the
  `#include "Panels/FrameDebuggerPanel.h"`, the
  `FrameDebuggerPanel m_frameDebuggerPanel;` member, and the
  `m_frameDebuggerPanel.Build(m_ctx);` call inside `BuildUI()`.

Nothing was missing — **zero build-system files needed changes this
phase**, exactly as PHASE7's own §3.9 file-change inventory allowed for
("only if Step 3.1 found something genuinely missing").

## 3.2 — New file: `docs/conventions/frame-debugger.md`

Wrote a full convention doc mirroring
`docs/conventions/time-and-playback-pause.md`'s tone/structure: the
window's purpose and GUI-only scope, its on-demand-floating-window nature
(explicitly contrasted with the permanently-docked panels and the
deliberate `EditorPanelCatalog.h` non-entry), the exact three "glue seams"
a future real-capture campaign should replace
(`BuildPlaceholderFrameDebuggerSnapshot()`,
`FrameDebuggerEventNode::details`/`FindEventDetailsByIndex()`, and
`m_selectedEventIndex`'s own click-driven population inside
`RenderEventNode()`), the Locked Design Decision #3 auto-Pause
cross-wiring with `PlaybackControls`, and the documented manual-verification
limitation (no HTTP mechanism exists to open the window/click into it
remotely).

## 3.3 — `AGENTS.md` — new section

Added the new "## Frame Debugger (scaffolding)" section immediately after
"## Time and Playback Pause" and before "## Job System", copied verbatim
from the phase document's own §3.3 listing, linking to the new
`docs/conventions/frame-debugger.md`.

## 3.4 — `docs/conventions/editor-module-structure.md` — extended precedent bullet

Edited the existing "A future panel that genuinely needs its own persistent
state across frames" bullet: "Two real precedents" became "Three real
precedents", and `FrameDebuggerPanel` was added as the third named example
(with its own parenthetical: `Panels/FrameDebuggerPanel.h` -
`task_manager/frame-debugger-2/PHASE0_MASTER_STRATEGY.md`, holding its
"Enable" toggle, selected-event index, and splitter width across frames).
The closing sentence was updated to "All three are still called explicitly
by name (...)". `RenderGraphPanel`'s own pre-existing omission from this
same bullet (predating this campaign) was deliberately left untouched, per
the phase document's own explicit instruction not to fix an out-of-scope
gap here.

## 3.5 — `docs/README.md` — index link

Added the new `frame-debugger.md` bullet to the "Conventions" list,
positioned immediately after the existing "Time and Playback Pause" entry
and before "Job System" — matching `AGENTS.md`'s own section ordering
exactly, per the phase document's own explicit non-alphabetical-ordering
instruction. (One editing hiccup during this step: an `edit_line` call
initially duplicated the preceding "Time and Playback Pause" bullet's text;
caught immediately by re-reading the file and fixed with a follow-up
`edit_line` call before moving on — no lasting effect, confirmed by a final
read-back of the whole file.)

## 3.6 — `README.md` — status bullet

Added a new bullet at the very top of the "Status" section (above the
existing `frame-debugger-1` entry), mirroring that entry's own voice/format:
a bold one-line summary, the exact new files/classes involved, the
Locked-Design-Decision auto-Pause cross-wiring, the "every value is a
disabled control or placeholder message, by design" framing, and a pointer
to `task_manager/frame-debugger-2/PHASE0_MASTER_STRATEGY.md`, closing with
the verification summary (full clean build both configs, full `ctest`
pass, live runtime smoke test).

## 3.7 — `TODO.md` — deferred items

Added a new "## Frame Debugger (scaffolding)" section (placed immediately
after "## Time and Playback Pause" and before "## Engine Roadmap (not yet
started)") with a "### Deferred from the `frame-debugger-2` campaign"
subsection listing every one of `PHASE0_MASTER_STRATEGY.md`'s Non-Goals
verbatim as concrete, individually-actionable future-work bullets (real
capture logic, real shader/material introspection, frame-history
scrubbing, any Network endpoint, the `EditorPanelCatalog.h`/dock-layout
non-entry, the missing Pause/frozen-snapshot control on this panel), plus
one additional PHASE7-specific item mirroring `frame-debugger-1`'s own
precedent: the interactive click-driven Enable/tree-row-selection
verification gap in this environment (no mouse-control tool, and the
window is deliberately absent from `GET /activate_tab`'s catalog) —
explicitly framed as an accepted manual-verification gap, not a defect.

## 3.8 — Full validation

1. **`cmake --build build --clean-first`** (`GTE_ENABLE_EDITOR=ON`) —
   succeeded, **431/431 steps, zero errors**, producing both
   `GreatTamanaEngine.exe` and `tests\GreatTamanaEngineTests.exe`.
2. **`cmake --build build-editor-off --clean-first`**
   (`GTE_ENABLE_EDITOR=OFF`) — this directory did not exist yet at the
   start of this phase, so it was first configured fresh
   (`cmake -S . -B build-editor-off -G Ninja -DGTE_ENABLE_EDITOR=OFF`,
   which reused every already-fetched third-party dependency on disk with
   zero network access needed) then built — succeeded, **364/364 steps,
   zero errors**. Confirmed by inspecting the build log directly that
   neither `Editor/FrameDebuggerData.cpp` nor
   `Editor/Panels/FrameDebuggerPanel.cpp` (nor their test file) were even
   attempted for compilation in this configuration — only
   `Editor/NullEditorLayer.cpp` and the always-compiled
   `Editor/EditorPanelCatalogTests.cpp` appear under `Editor/` in this
   build's object list, exactly as `if(GTE_ENABLE_EDITOR)` guarantees.
3. **`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest
   -C Debug --output-on-failure`** — **100% tests passed, out of 1345**
   (140.76 sec... total test time 134.76 sec). The only test that did not
   run is the same pre-existing, machine-gated
   `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` skip
   this repository has always had — zero new failures, zero new skips.
   This includes all 6 `FrameDebuggerDataTest` cases (the 4 from PHASE1
   plus the 2 `FormatVectorPropertyTest`/`FormatMatrixPropertyTest` cases
   from PHASE6).
4. **Live runtime smoke test**:
   - `run_app_background` launched the freshly-built
     `build\GreatTamanaEngine.exe` (PID 31092).
   - `GET /get_swapchain` confirmed: the menu bar shows exactly "File" and
     "Window" (per Locked Design Decision #1's placement); the Pause/Step
     toolbar from `frame-debugger-1` renders directly beneath it exactly
     as before ("Pause" clickable, "Step" grayed-out); the default dock
     layout (Hierarchy/Scene/Game/Inspector, Memory/Profiler/Render
     Graph/Atmosphere/Jobs/Project) renders correctly with real scene
     content (sky gradient, "Entity 0 (Camera)", "TestScene.gtscene") —
     confirming **no visual regression anywhere** from this phase's
     changes. The Frame Debugger window itself is closed by default
     (`EditorContext::frameDebuggerWindowOpen` starts `false`), so nothing
     new is visible on this screenshot — expected, matching every one of
     PHASE2-PHASE6's own identical prior observations.
   - `GET /list_tabs` confirmed the response still lists exactly the same
     ten pre-existing panels (`Hierarchy`/`Inspector`/`Scene`/`Game`/
     `Memory`/`Profiler`/`Render Graph`/`Jobs`/`Atmosphere`/`Project`) with
     **no** "Frame Debugger" entry — re-confirming Locked Design Decision
     #6 holds after every phase of this campaign, including this final one.
   - `stop_app_background` cleanly terminated the process.

   As with every prior phase's completion report, actually opening the
   "Frame Debugger" window via "Window > Frame Debugger", checking
   "Enable" with a real mouse click, and visually confirming the toolbar/
   stepper row/tree pane/inspector pane/Pause-Resume cross-wiring described
   in the task instructions **could not be exercised remotely in this
   environment**: the window is deliberately not part of
   `EditorPanelCatalog.h` (Locked Design Decision #6), so `GET
   /activate_tab` cannot bring it to front, and no HTTP command endpoint
   exists anywhere in `src/Network/` capable of flipping
   `EditorContext::frameDebuggerWindowOpen` or synthesizing a menu-click/
   checkbox-click of any kind (mirroring `frame-debugger-1`'s own
   identical, already-documented gap for Pause/Step). This is the exact
   same pre-existing, expected, already-repeatedly-documented limitation
   every one of PHASE2-PHASE6's own completion reports records — not a
   defect introduced by, or unique to, this phase. What WAS verified
   directly and successfully: the build wiring is complete and correct in
   both configurations, the full regression suite passes at 100%, the
   engine boots and renders identically to every prior phase with no
   regression, and the documented Locked Design Decisions (#1 "Window"
   menu placement, #6 no catalog entry) both hold exactly as intended.

## Deviations from the phase document

None in substance. One transient self-corrected editing mistake is noted
in §3.5 above (an `edit_line` call briefly duplicated a line in
`docs/README.md`, caught and fixed within the same editing pass before any
build/test step ran) — this is a normal part of using the line-based
editing tool, not a deviation from the phase's actual plan, and the final
file content matches the phase document's own instructions exactly.

## Next step

None — PHASE7 is the final phase of the `frame-debugger-2` campaign. See
`CAMPAIGN_COMPLETION_REPORT.md` (written alongside this file) for the full
seven-phase narrative.
