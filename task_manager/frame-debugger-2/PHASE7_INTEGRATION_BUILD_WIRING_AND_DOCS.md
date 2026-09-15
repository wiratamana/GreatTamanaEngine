# PHASE7 — Integration confirmation, docs, full build + regression validation

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1-6 (the whole feature is functionally complete as of
PHASE6 — this phase is closeout, not new UI).
Touches: `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`,
`docs/conventions/editor-module-structure.md`; verifies (does not
necessarily need to change) `CMakeLists.txt`/`tests/CMakeLists.txt`;
writes `CAMPAIGN_COMPLETION_REPORT.md`.

## Step 1: The Goal

Close out the campaign: confirm every build-system file is complete and
correct, explicitly document the one deliberate non-obvious omission
(`EditorPanelCatalog.h`), extend this codebase's documentation the same
way every prior campaign in this repository does (`AGENTS.md`/`docs/
conventions/*.md`/`README.md`/`TODO.md`), run a full clean build in BOTH
Editor configurations, run the full `ctest` regression suite, perform a
live runtime smoke test comparing the finished window against the
original reference screenshot, and write the campaign's own
`CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation

- Precedent for the exact documentation shape expected: read
  `task_manager/frame-debugger-1/PHASE5_VALIDATION_DOCS_AND_REGRESSION_SAFETY.md`
  and its own `PHASE5_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md`
  — this phase should follow that exact same shape (a new short section
  in `AGENTS.md` with a "Full convention: ..." link, a new
  `docs/conventions/*.md` file linked from `docs/README.md`'s index, a
  `README.md` status bullet, and a `TODO.md` section listing every
  Non-Goal from `PHASE0_MASTER_STRATEGY.md` as a concrete deferred item
  for a future campaign).
- `AGENTS.md`'s "Editor Module Structure" section is the most relevant
  existing section to extend — add a short paragraph (mirroring the
  "Time and Playback Pause" section's own concise style) describing the
  Frame Debugger window's existence, that it is GUI-only scaffolding, and
  linking to a new `docs/conventions/frame-debugger.md` file for full
  detail.
- **Explicitly confirm and document**: `src/Editor/EditorPanelCatalog.h`'s
  `kKnownEditorPanelNames` array does **not** get a "Frame Debugger"
  entry, and `tests/Editor/EditorPanelCatalogTests.cpp` is **not**
  modified — this mirrors the already-existing, already-shipped
  precedent that `BoneViewerWindow`'s own window name is likewise absent
  from that same catalog (an on-demand floating window is a fundamentally
  different concept from a permanently-docked default-layout tab, which
  is all that catalog is for — see `EditorPanelCatalog.h`'s own top-of-
  file comment). State this explicitly in the new
  `docs/conventions/frame-debugger.md` file so a future contributor
  doesn't "fix" this as an apparent oversight.
- Full clean build commands (mirroring `frame-debugger-1`'s own PHASE5):
  `cmake --build build --clean-first` (GTE_ENABLE_EDITOR=ON config) and
  `cmake --build build-editor-off --clean-first` (GTE_ENABLE_EDITOR=OFF
  config, confirming this campaign's new files are correctly excluded
  from that build entirely, per every one of them living under the
  `if(GTE_ENABLE_EDITOR)` guard).
- Full regression command: `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Live runtime smoke test: `run_app_background` the built
  `GreatTamanaEngine.exe`, then `gte_send_request` against
  `/get_swapchain` (or `/get_game_view` if more appropriate) to visually
  confirm: (a) the menu bar shows "File" and "Window"; (b) opening
  "Window > Frame Debugger" shows the floating window with its toolbar
  ("Enable" checkbox + grayed "Editor" combo), its stepper row ("0 of 0",
  grayed), and (unchecked) the "Enable Frame Debugger above..." message;
  (c) checking "Enable" reveals the split tree/inspector layout ("No
  frame captured yet." on the left; RenderTarget/Channels/Levels/preview-
  box chrome plus "No event selected." on the right) AND causes the
  existing Pause/Resume toolbar to show "(Paused)"/"Resume" (confirming
  Locked Design Decision #3's cross-feature wiring survived every
  intermediate phase); (d) closing the window via its own `[x]`
  un-checks the "Window > Frame Debugger" menu item. Always
  `stop_app_background` afterward.

## Step 3: The Plan

### 3.1 Build-system confirmation (verify, fix only if actually wrong)

Re-open `CMakeLists.txt` and `tests/CMakeLists.txt` and confirm every one
of these lines genuinely exists (search for each literal string; if any
is missing, add it now rather than assuming an earlier phase already
did):

- `CMakeLists.txt`, inside `if(GTE_ENABLE_EDITOR)`'s `target_sources(gte_core
  PRIVATE ...)`: `src/Editor/FrameDebuggerData.h`,
  `src/Editor/FrameDebuggerData.cpp`,
  `src/Editor/Panels/FrameDebuggerPanel.h`,
  `src/Editor/Panels/FrameDebuggerPanel.cpp`.
- `tests/CMakeLists.txt`, inside its own `if(GTE_ENABLE_EDITOR)`
  `list(APPEND GTE_TEST_SOURCES ...)`: `Editor/FrameDebuggerDataTests.cpp`.
- `src/Editor/EditorContext.h` has `frameDebuggerWindowOpen`.
- `src/Editor/DockLayout.cpp` has the "Window" menu with the "Frame
  Debugger" checkable item.
- `src/Editor/ImGuiEditorLayer.cpp` has the `#include`, the
  `FrameDebuggerPanel m_frameDebuggerPanel;` member, and the
  `m_frameDebuggerPanel.Build(m_ctx);` call.

### 3.2 New file: `docs/conventions/frame-debugger.md`

Write a full convention doc, mirroring
`docs/conventions/time-and-playback-pause.md`'s own tone/structure:
summarize the window's purpose, its on-demand-floating-window nature
(explicitly contrasted with the permanently-docked panels, and
explicitly noting the deliberate `EditorPanelCatalog.h` non-entry - see
Step 2 above), the GUI-only/no-real-capture-data scope of this campaign,
the exact three "glue seams" a future real-capture campaign should
replace (`BuildPlaceholderFrameDebuggerSnapshot()`,
`FrameDebuggerEventNode::details`/`FindEventDetailsByIndex()`, and
`m_selectedEventIndex`'s own click-driven population inside
`RenderEventNode()`), and the Locked Design Decision #3 auto-Pause
cross-wiring with `PlaybackControls`.

### 3.3 `AGENTS.md` — new short section

Add a new section (after "Time and Playback Pause", before "Job System",
matching that section's own concise "what it is, where it lives, link to
full doc" shape):

```markdown
## Frame Debugger (scaffolding)

`src/Editor/FrameDebuggerData.h/.cpp` (pure data model) and
`src/Editor/Panels/FrameDebuggerPanel.h/.cpp` (the on-demand floating
"Frame Debugger" window, opened via Window > Frame Debugger) are the
GUI-only scaffolding for a future Unity-Frame-Debugger-style tool - an
"Enable" checkbox (which auto-engages the existing Pause/Resume
toolbar), a disabled mode combo, a disabled frame stepper, a
draggable-splitter event tree (always "No frame captured yet." this
campaign), and an inspector pane (RenderTarget/Channels/Levels/preview
chrome plus an always-"No event selected." event-details section). No
real frame/draw-call capture exists yet - every value is a disabled
control or placeholder message, by design, until a future campaign
wires real data into the documented seams.

Full convention: [docs/conventions/frame-debugger.md](docs/conventions/frame-debugger.md).
```

### 3.4 `docs/README.md` — index link

Add the new `frame-debugger.md` entry to the conventions index, in the
same alphabetized/grouped position that file's own existing list
convention uses.

### 3.5 `README.md` — status bullet

Add one bullet under the project's own "Status"/feature-list section
(mirror `frame-debugger-1`'s own added bullet's exact phrasing style)
noting the new Frame Debugger window exists as GUI-only scaffolding.

### 3.6 `TODO.md` — deferred items

Add a new "Frame Debugger (scaffolding) - deferred to a future campaign"
section listing every one of `PHASE0_MASTER_STRATEGY.md`'s Non-Goals
verbatim (real capture logic, real shader/material introspection,
frame-history scrubbing, any Network endpoint, `EditorPanelCatalog.h`/
dock-layout entry, a Pause/frozen-snapshot control on this panel) as
concrete, individually-actionable future work items - mirroring
`frame-debugger-1`'s own `TODO.md` section's exact structure.

### 3.7 Full validation

1. `cmake --build build --clean-first` (GTE_ENABLE_EDITOR=ON) - expect
   zero errors.
2. `cmake --build build-editor-off --clean-first` (GTE_ENABLE_EDITOR=OFF)
   - expect zero errors, and confirm (via the build log / a quick
   `search_in_dir` over the build log if captured) that none of this
   campaign's new files were even attempted for compilation in this
   configuration.
3. `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest
   -C Debug --output-on-failure` - expect 100% pass (same pre-existing
   single machine-gated skip this repository has always had; zero NEW
   failures/skips).
4. Live runtime smoke test exactly as described in Step 2's last bullet
   above - `run_app_background`, `gte_send_request`, verify, then
   `stop_app_background`.

### 3.8 File-change inventory (this phase only)

New: `docs/conventions/frame-debugger.md`,
`task_manager/frame-debugger-2/CAMPAIGN_COMPLETION_REPORT.md`.
Modified (docs only, plus build-system confirmation touch-ups if
anything was actually missing): `AGENTS.md`, `README.md`, `TODO.md`,
`docs/README.md`, and (only if Step 3.1 found something genuinely
missing) `CMakeLists.txt`/`tests/CMakeLists.txt`.

Write `PHASE7_COMPLETION_REPORT.md` AND
`CAMPAIGN_COMPLETION_REPORT.md` (mirroring `frame-debugger-1`'s own two-
report convention - one per-phase, one whole-campaign summary) into
`task_manager/frame-debugger-2/` when done, then `git_add`/`git_commit`
everything (code, docs, and both reports) in one final commit for this
campaign.
