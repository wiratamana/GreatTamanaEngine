# PHASE0 — Double-check report (2nd planning iteration)

Scope: re-read `PHASE0_MASTER_STRATEGY.md` + `PHASE1`..`PHASE7` in the
`task_manager/frame-debugger-2/` strategy folder, cross-checked every
referenced source file/class/function/line-number/precedent claim against
the actual codebase on the current `feature/frame-debugger-impl` branch,
verified every ImGui call used in a code snippet against the actual
vendored `third_party/imgui/imgui.h` (confirmed `IMGUI_VERSION "1.93.0
WIP"`, `IMGUI_VERSION_NUM 19295`), and checked cross-phase name/type
consistency. This report summarizes what was found and changed per file.

## Method

For every phase file, every concrete claim of the form "file X has Y at
line Z" or "function/struct/field W exists with this shape" was checked
directly against the real file on disk rather than trusted at face value,
including:

- `src/Editor/EditorContext.h`, `DockLayout.cpp`, `ImGuiEditorLayer.cpp`,
  `EditorPanelCatalog.h`, `PlaybackControls.cpp`, `JobsPanelData.h`,
  `BoneViewerWindow.h` (its `collisionGroupMask`/"never insert a field in
  the middle" precedent), `Panels/ProjectPanel.cpp` (splitter lines
  ~381-410), `Panels/InspectorPanel.cpp`
  (`ctx.inspectorPreviewHeight`), `Panels/RenderGraphPanel.h/.cpp`,
  `Panels/ProfilerPanel.h/.cpp`, `Panels/JobsPanel.h/.cpp`,
  `Panels/AtmospherePanel.cpp`.
- `CMakeLists.txt` (lines 566, 603-604, 622-623) and `tests/CMakeLists.txt`
  (lines 1806, 1901-1911) — every exact line-number claim in PHASE0/PHASE1/
  PHASE2 matched the real file contents exactly.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h` (the "small, dedicated,
  directly-testable reshaping module" precedent PHASE1 cites).
- `docs/conventions/editor-module-structure.md`,
  `docs/conventions/time-and-playback-pause.md`, `docs/README.md`,
  `AGENTS.md`, `README.md`, `TODO.md`, `task_manager/frame-debugger-1/`'s
  own phase/report files (the precedent campaign PHASE7 mirrors).
- Every ImGui API signature used in a code snippet
  (`BeginTabBar`/`BeginTabItem`/`EndTabItem`/`EndTabBar`, `SeparatorText`,
  `BeginDisabled`, `SliderInt`/`SliderFloat`, `Combo`, `SmallButton`,
  `TreeNodeEx`, `Selectable`) against `third_party/imgui/imgui.h` directly.

Result: this strategy folder was already extremely well-researched — every
line-number, file-path, struct/field-name, and ImGui-signature claim
checked out exactly as written. Only one real gap was found (PHASE7 vs.
PHASE0 file-inventory mismatch, detailed below) plus one factually-wrong
claim about `docs/README.md`'s ordering convention (also in PHASE7). No
other file needed changing.

## Per-file findings

### PHASE0_MASTER_STRATEGY.md — no change

Phase table, Locked Design Decisions, Non-Goals, and the full-campaign
file-change inventory were all re-checked against the actual codebase and
against every child phase file. All consistent. Notably, this file's own
Step 3.4 file-change inventory **already correctly** listed
`docs/conventions/editor-module-structure.md` as a PHASE7 modification —
it was PHASE7 itself that had fallen out of sync with that claim (see
below), not PHASE0. No edit needed here; PHASE7 was brought back into
agreement with PHASE0 instead of the other way around.

### PHASE1_FRAME_DEBUGGER_DATA_MODEL.md — no change

Every struct/function declared here (`FrameDebuggerTextureProperty`,
`FrameDebuggerVectorProperty`, `FrameDebuggerMatrixProperty`,
`FrameDebuggerEventDetails`, `FrameDebuggerEventNode`,
`FrameDebuggerRenderTargetInfo`, `FrameDebuggerSnapshot`,
`BuildPlaceholderFrameDebuggerSnapshot()`, `FormatFrameStepperLabel()`,
`ClampSelectedEventIndex()`, `FindEventDetailsByIndex()`) is used
identically in every later phase that references it. The
`JobsPanelData.h`/`ProfilerPanelData.h` precedent, the exact
`CMakeLists.txt`/`tests/CMakeLists.txt` insertion points (confirmed at
real lines 603-604 and 1909), and the `RenderGraphSnapshot.h` "reshaping
module" precedent were all confirmed verbatim-accurate. Test plan (3.3)
is precise and unambiguous. No change needed.

### PHASE2_PANEL_SHELL_AND_WINDOW_MENU_ENTRY.md — no change

Confirmed: `EditorContext.h`'s last field really is `stepOneFrameRequested`
(line 209) right before the closing `};`; `DockLayout.cpp`'s menu bar
really has exactly one `BeginMenu("File")` block (lines 128-151);
`ImGuiEditorLayer.cpp`'s `m_jobsPanel.Build(m_ctx, game);` call (line 560)
sits immediately before the `#if GTE_ENABLE_PROJECT_PANEL` block (line
561), and `JobsPanel m_jobsPanel;` (line 779) sits immediately before that
same `#if` block — both exactly as this phase describes for its own
insertion points. `EditorPanelCatalog.h`'s `kKnownEditorPanelNames` was
confirmed to genuinely exclude `BoneViewerWindow`, validating the
"on-demand floating window, not cataloged" precedent this phase's class
comment leans on. No change needed.

### PHASE3_TOOLBAR_AND_FRAME_STEPPER.md — no change

`PlaybackControls.cpp`'s actual body was read directly and matches this
phase's description of it exactly (the `Pause`/`Resume` toggle button,
the `BeginDisabled(!ctx.playbackPaused)`-gated `Step` button, the
`"(Paused)"` indicator). Every `ImGui::` call in this phase's snippets
(`Checkbox`, `Combo` with the 4-arg string-array overload,
`SetNextItemWidth`, `BeginDisabled`/`EndDisabled`, `SliderInt` with an
empty format string) matches the real vendored `imgui.h` signatures. No
change needed.

### PHASE4_EVENT_TREE_PANE_AND_SPLITTER.md — no change

`Panels/ProjectPanel.cpp`'s splitter (confirmed at real lines 381-410) is
quoted essentially verbatim; `BoneViewerWindow.cpp`'s
`RenderBoneTreeNode()`/`TreeNodeEx` pattern was confirmed present as
described. `FrameDebuggerEventNode`'s fields are used correctly. No change
needed.

### PHASE5_INSPECTOR_PANE_FRAME_LEVEL_CHROME.md — no change

All widget calls (`SmallButton`, `SliderFloat`, `CalcTextSize`,
`SetCursorPos`, `BeginChild`/`EndChild`) match the real ImGui API.
`FrameDebuggerRenderTargetInfo`'s default values (`"<No name>"`,
`0`/`0`, `"Default"`) are read and used consistently with PHASE1's
struct definition. (One cosmetic-only redundancy was noticed — the
Channels row calls `ImGui::SameLine()` once right after the "Channels"
label and then again as the very first statement inside the toggle-button
loop, which is harmless but superfluous; left as-is since it doesn't
create any ambiguity or incorrect behavior for an implementer, and the
instructions are otherwise to avoid changing a file "just to have changed
something.")

### PHASE6_EVENT_DETAILS_SECTION_AND_PROPERTY_FORMATTING.md — no change

Per the task's own instruction, this file's current on-disk content was
treated as authoritative (its timestamp is the most recent of the eight,
consistent with a prior narrow double-check pass having already run
against it alone). Independently re-verified anyway: the new
`eventLabel` field is appended at the very end of
`FrameDebuggerEventDetails` (matching the "never insert a field in the
middle" precedent, whose real citation — `BoneViewerWindow.h`'s
`collisionGroupMask` comment — was confirmed to exist verbatim);
`BeginTabBar`/`BeginTabItem`/`EndTabItem`/`EndTabBar` and `SeparatorText`
signatures were checked directly against `third_party/imgui/imgui.h` and
match exactly (this is indeed this codebase's first tab bar — no other
`BeginTabBar` call exists anywhere under `src/`, confirmed by search). No
change needed.

### PHASE7_INTEGRATION_BUILD_WIRING_AND_DOCS.md — CHANGED

Two real issues found and fixed:

1. **Missing file-change inventory item.** `PHASE0_MASTER_STRATEGY.md`'s
   own "File-change inventory" (Step 3.4) has always listed
   `docs/conventions/editor-module-structure.md` as a file PHASE7
   modifies. However, PHASE7's own Step 3 plan never actually described
   any edit to that file, and PHASE7's own Step 3.8 file-change inventory
   (now 3.9) never listed it either — a genuine inconsistency between the
   parent's stated scope and the child phase's actual instructions. Root
   cause: `docs/conventions/editor-module-structure.md`'s real, existing
   "a future panel that genuinely needs its own persistent state across
   frames" bullet currently names only two concrete precedents
   (`BoneViewerWindow`, `ProfilerPanel`) — confirmed by reading the file
   directly — even though `PHASE0_MASTER_STRATEGY.md`'s own Step 2 calls
   `FrameDebuggerPanel` "the fourth precedent for the stateful-class
   exception" (after `BoneViewerWindow`, `ProfilerPanel`, and
   `RenderGraphPanel`, all three of which are real, already-shipped
   classes). **Fix:** added a new Step 3.4 ("`docs/conventions/
   editor-module-structure.md` — extend the stateful-panel precedent
   bullet") giving precise, unambiguous instructions to append
   `FrameDebuggerPanel` to that bullet's list of named examples, while
   explicitly telling the implementer NOT to also fix that bullet's
   pre-existing, unrelated omission of `RenderGraphPanel` (out of scope,
   predates this campaign — scope creep to avoid). Updated this phase's
   own header "Touches" line and its file-change inventory (now Step 3.9)
   to include this file. Subsequent steps were renumbered 3.4→3.5 through
   3.8→3.9 to keep sequential numbering.
2. **Factually incorrect instruction about `docs/README.md`'s ordering.**
   The original Step 3.4 ("`docs/README.md` — index link") told the
   implementer to add the new entry "in the same alphabetized/grouped
   position that file's own existing list convention uses." This is
   simply false: `docs/README.md`'s "Conventions" list was read directly
   and its entries are **not** alphabetical at all — they appear in
   exactly the same order their matching sections appear in `AGENTS.md`
   (Profiling, then Time and Playback Pause, then Job System, then
   Networking, ...). Left as originally worded, an LLM implementer could
   easily have inserted "Frame Debugger" at its alphabetically-sorted spot
   (between "Entity-Component-System" and "GPU Vertex Skinning"), which
   would NOT match the position of the new `AGENTS.md` section this same
   phase inserts (right after "Time and Playback Pause") — a real,
   avoidable ambiguity per this task's own review checklist ("Each
   phase's own Step 3 instructions are precise enough that an LLM
   programmer could implement it with zero ambiguity"). **Fix:** rewrote
   that step (now Step 3.5) to state the real, confirmed ordering rule and
   explicitly direct the new bullet to the same relative position as the
   new `AGENTS.md` section (immediately after "Time and Playback Pause",
   before "Job System").

No other part of PHASE7 needed changing — the build-system confirmation
list (3.1), the full validation commands (3.8), and the
`EditorPanelCatalog.h` non-inclusion documentation instructions were all
already accurate and precedented correctly.

## Conclusion

Seven of the eight files (`PHASE0`, `PHASE1`-`PHASE6`) required no
changes — they were already accurate, internally consistent, and
precise enough for zero-ambiguity implementation, verified directly
against the real codebase and the real vendored ImGui API rather than
taken on faith. Only `PHASE7_INTEGRATION_BUILD_WIRING_AND_DOCS.md` was
updated, to close a real parent/child file-inventory mismatch and correct
a factually wrong claim about an existing doc file's ordering convention
that could otherwise have misled its own implementer. No new `.md` files
were created other than this report.
