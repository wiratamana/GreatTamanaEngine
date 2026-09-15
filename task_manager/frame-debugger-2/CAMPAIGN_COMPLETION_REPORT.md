# `frame-debugger-2` — Campaign Completion Report: Editor "Frame Debugger" window (GUI scaffolding only)

Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`
Phases: 7 (all landed)

## Goal recap

Give the GreatTamanaEngine Editor a new **"Frame Debugger"** window,
visually modeled on Unity's own Frame Debugger: a closeable, on-demand
floating window (opened from a brand-new **Window** top-level menu)
containing a toolbar ("Enable" checkbox + a cosmetic disabled "Editor"
combo), a disabled frame-stepper row ("N of M"), a left-hand
draggable-splitter event tree pane, and a right-hand inspector pane
(RenderTarget/Channels/Levels/preview chrome plus an event-details
section with a Preview/ShaderProperties tab bar). **This campaign was
explicitly, deliberately GUI-ONLY** — no real frame/draw-call capture, no
real render-graph/draw-call introspection, no real shader reflection was
ever wired in anywhere. Every value shown is a disabled control, a
placeholder message ("No frame captured yet." / "No event selected."), or
an inert default, by design — the entire point of this campaign was to
build the scaffolding (the window, its widgets, and the exact seams a
future "wire up the real frame capture" campaign will plug real data
into) so that future work is a pure data-plumbing exercise that never has
to touch a single ImGui call in this window again.

## Phase-by-phase summary

### PHASE1 — Frame Debugger data model

Added a brand-new, self-contained, Tier-1-testable
`src/Editor/FrameDebuggerData.h/.cpp` — every struct the panel will ever
display (`FrameDebuggerTextureProperty`/`FrameDebuggerVectorProperty`/
`FrameDebuggerMatrixProperty`/`FrameDebuggerEventDetails`/
`FrameDebuggerEventNode`/`FrameDebuggerRenderTargetInfo`/
`FrameDebuggerSnapshot`), plus a deliberately-empty
`BuildPlaceholderFrameDebuggerSnapshot()` and three pure stepper-label/
clamp/lookup helpers (`FormatFrameStepperLabel()`,
`ClampSelectedEventIndex()`, `FindEventDetailsByIndex()`). Zero ImGui,
zero call sites into the rest of the Editor yet.
`tests/Editor/FrameDebuggerDataTests.cpp` (4 cases) covers every helper
exhaustively, including a hand-built non-empty synthetic tree for
`FindEventDetailsByIndexTest` — the only place in the whole campaign a
non-empty snapshot is ever constructed. Verified: scoped
`--gtest_filter=*FrameDebuggerData*` run, **4/4 passed**.

### PHASE2 — Panel shell, "Window" menu, and Editor wiring

Brought the window itself into existence:
`src/Editor/Panels/FrameDebuggerPanel.h/.cpp` (a small stateful class,
mirroring `RenderGraphPanel`/`ProfilerPanel`/`BoneViewerWindow`'s own
precedent), a new `EditorContext::frameDebuggerWindowOpen` bool, a new
"Window" top-level menu in `DockLayout.cpp` (right after "File") with a
checkable `MenuItem` bound directly to that bool, and the
`ImGuiEditorLayer` wiring (`m_frameDebuggerPanel` member +
`Build(m_ctx)` call). Showed only a "Coming soon." placeholder body — no
toolbar/tree/inspector yet. Deliberately **not** added to
`EditorPanelCatalog.h` (Locked Design Decision #6, mirroring
`BoneViewerWindow`'s own precedent). Verified: `GreatTamanaEngine` target
build succeeded; live smoke test confirmed the menu bar now reads
"File   Window" with no other regression.

### PHASE3 — Toolbar row (Enable + "Editor" stub) and frame-stepper row

Replaced the "Coming soon." placeholder with the real top section: a
toggleable **"Enable"** checkbox that, only on its false→true transition,
sets `ctx.playbackPaused = true` (the exact same field
`PlaybackControls.cpp`'s own "Pause" button writes) — deliberately
**not** symmetric (turning "Enable" back off never auto-resumes), per
Locked Design Decision #3, documented in-code so a future reader doesn't
"fix" it into a symmetric toggle. Added a permanently-disabled cosmetic
"Editor" mode combo, and a disabled frame-stepper row that always reads
"0 of 0". Verified: `GreatTamanaEngine` target build succeeded; live
smoke test confirmed no regression to the menu bar, Pause/Resume toolbar,
or `/list_tabs`' catalog.

### PHASE4 — Left-hand event tree pane + draggable splitter

Replaced PHASE3's placeholder body with a real, draggable-splitter,
two-pane layout: a left-hand child region (`m_leftPaneWidth`, default
320px, clamped every frame, copying `ProjectPanel.cpp`'s own splitter
pattern verbatim) showing **"No frame captured yet."** (Locked Design
Decision #2 — zero fake/mock rows, ever, since
`BuildPlaceholderFrameDebuggerSnapshot()` always returns an empty
`rootNodes`), a thin draggable splitter button, and a right-hand child
region still holding a placeholder for PHASE5/6. Also added a fully-
written, currently-unreachable recursive tree-row renderer
(`RenderEventNode()`) — group nodes as default-open tree headers, leaf
nodes as selectable rows that set `m_selectedEventIndex` on click — ready
for real data the moment a future campaign starts returning non-empty
snapshots. Verified: `GreatTamanaEngine` target build succeeded; live
smoke test confirmed no regression anywhere else in the dockspace.

### PHASE5 — Right-hand inspector pane: frame-level static chrome

Replaced the right-hand pane's PHASE4 placeholder with real, always-
visible FRAME-LEVEL chrome (independent of any event selection, per
Locked Design Decision #2): a RenderTarget selector row (name + a
disabled one-item combo), a Channels toggle row (five cosmetic
`SmallButton`s: All/R/G/B/A), a disabled Levels slider, a resolution/
format caption (always "0x0  Default"), and a bordered, centered
"No Texture" texture-preview placeholder box. Verified: `GreatTamanaEngine`
target build succeeded; live smoke test confirmed no regression.

### PHASE6 — Event-details section, tab bar, and property formatting

The heaviest single phase (per `PHASE0_MASTER_STRATEGY.md`'s own flag).
Made the bottom, event-level portion of the inspector pane fully real:
appended `eventLabel` to `FrameDebuggerEventDetails` plus
`FormatVectorProperty()`/`FormatMatrixProperty()` to
`FrameDebuggerData.h/.cpp` (2 new test cases —
`FormatVectorPropertyTest`/`FormatMatrixPropertyTest` — bringing the
Tier-1 suite to 6 cases), and implemented
`FrameDebuggerPanel::BuildEventDetailsSection()`: twelve Shader/Pass/
Blend/Z-state/Stencil property rows, a `Preview`/`ShaderProperties` tab
bar (this engine's first use of Dear ImGui's tab-bar API anywhere),
and conditionally-shown Textures/Vectors/Matrices subsections — all
built against a real `FrameDebuggerEventDetails` value, but gated behind
`m_selectedEventIndex`/`FindEventDetailsByIndex()`, which **always**
resolve to `std::nullopt` this campaign, so this section always renders
a single **"No event selected."** line in practice — the correct,
intended final state. One environment issue (a near-full local disk
causing a transient link failure) was encountered and resolved (clearing
the Recycle Bin), unrelated to any code change. Verified: scoped
`--gtest_filter=*FrameDebuggerData*` run, **6/6 passed**; full
`GreatTamanaEngine`/`GreatTamanaEngineTests` targets relinked
successfully; live smoke test confirmed no regression.

### PHASE7 — Integration confirmation, docs, full build + regression validation (this phase)

Closed out the campaign. Re-confirmed every build-system wiring point
named in its own §3.1 checklist already existed correctly (nothing was
missing — zero build-system files needed changes). Wrote the full
documentation sweep every completed campaign in this repository makes: a
new `docs/conventions/frame-debugger.md` (mirroring
`docs/conventions/time-and-playback-pause.md`'s tone/structure, including
the exact three "glue seams" a future real-capture campaign should
replace), a new "Frame Debugger (scaffolding)" section in `AGENTS.md`
(placed after "Time and Playback Pause", before "Job System"), an
extended stateful-panel precedent bullet in
`docs/conventions/editor-module-structure.md` (naming `FrameDebuggerPanel`
as a third precedent alongside `BoneViewerWindow`/`ProfilerPanel`), a new
index entry in `docs/README.md` (positioned to match `AGENTS.md`'s own
section order, not alphabetically), a new top-of-"Status" bullet in
`README.md`, and a new "Frame Debugger (scaffolding)" / "Deferred from the
`frame-debugger-2` campaign" section in `TODO.md` listing every Non-Goal
verbatim. Then ran the full validation: a **full clean build** for both
`GTE_ENABLE_EDITOR=ON` (`build/`, 431 steps) and `=OFF`
(`build-editor-off/`, freshly configured, 364 steps), both with **zero
errors** — confirming this campaign's new files are correctly excluded
entirely from the Editor-OFF configuration; a **full `ctest` regression
pass** — **100% of 1345 tests passed** (the same single pre-existing,
machine-gated `PmxLoaderRealModelSmokeTest` skip this repository has
always had); and a live runtime smoke test confirming the menu bar shows
"File" and "Window", the Pause/Resume toolbar and default dock layout
render with no regression, and `GET /list_tabs` still lists exactly the
same ten pre-existing panels with no "Frame Debugger" entry (Locked
Design Decision #6 holds through the very last phase). See
`PHASE7_COMPLETION_REPORT.md` for the full detail.

## Final architecture (as landed)

```
DockLayout.cpp (BuildDockspaceAndMenuBar)
  "Window" menu -> ImGui::MenuItem("Frame Debugger", nullptr,
                                    &ctx.frameDebuggerWindowOpen)
                                     |
                                     v
EditorContext::frameDebuggerWindowOpen (bool, shared)
                                     |
                                     v
ImGuiEditorLayer::BuildUI() -> m_frameDebuggerPanel.Build(m_ctx)
                                     |
                                     v
FrameDebuggerPanel::Build(EditorContext& ctx)
  if (!ctx.frameDebuggerWindowOpen) return;
  ImGui::Begin("Frame Debugger", &ctx.frameDebuggerWindowOpen);
    BuildToolbarRow(ctx)          // "Enable" checkbox + disabled "Editor" combo
    BuildFrameStepperRow()        // "0 of 0", disabled
    if (!m_enabled) { "Enable Frame Debugger above..." message; }
    else {
        snapshot = BuildPlaceholderFrameDebuggerSnapshot();  // always empty
        [ event tree pane | draggable splitter | inspector pane ]
          BuildEventTreePane(snapshot)     -> "No frame captured yet."
          BuildInspectorPane(snapshot)
            RenderTarget/Channels/Levels/Preview chrome (always visible)
            BuildEventDetailsSection(details)
              details = FindEventDetailsByIndex(snapshot, m_selectedEventIndex)
              -> always std::nullopt -> "No event selected."
    }
  ImGui::End();
```

`FrameDebuggerData.h/.cpp` is the pure, ImGui-free data model — the exact,
single seam a future real-capture campaign needs to replace/extend. No
other file needs to change again once that future campaign lands, other
than swapping which function produces the `FrameDebuggerSnapshot` and
populating `m_selectedEventIndex` from real tree-row clicks (both already
wired for real — they are just never reachable with non-default data yet,
because nothing ever produces a non-empty snapshot this campaign).

## File-change inventory (final, as actually landed across all 7 phases)

New files:
- `src/Editor/FrameDebuggerData.h`, `src/Editor/FrameDebuggerData.cpp`
  (PHASE1, extended in PHASE6)
- `src/Editor/Panels/FrameDebuggerPanel.h`,
  `src/Editor/Panels/FrameDebuggerPanel.cpp` (PHASE2, extended in
  PHASE3/4/5/6)
- `tests/Editor/FrameDebuggerDataTests.cpp` (PHASE1, extended in PHASE6)
- `docs/conventions/frame-debugger.md` (PHASE7)
- `task_manager/frame-debugger-2/PHASE1_COMPLETION_REPORT.md` ..
  `PHASE7_COMPLETION_REPORT.md`
- `task_manager/frame-debugger-2/CAMPAIGN_COMPLETION_REPORT.md` (this file)

Modified files:
- `src/Editor/EditorContext.h` (PHASE2 — one new bool field)
- `src/Editor/DockLayout.cpp` (PHASE2 — new "Window" menu)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE2 — new member + `BuildUI()`
  call)
- `CMakeLists.txt` (PHASE1 + PHASE2 — new source files)
- `tests/CMakeLists.txt` (PHASE1 — new test file)
- `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`,
  `docs/conventions/editor-module-structure.md` (all PHASE7)

`src/Editor/EditorPanelCatalog.h`/`tests/Editor/EditorPanelCatalogTests.cpp`
were **never** touched by any phase of this campaign — the deliberate,
documented non-omission per Locked Design Decision #6 (mirroring
`BoneViewerWindow`'s own identical precedent).

## Final verification evidence (PHASE7)

- **Full clean build, `GTE_ENABLE_EDITOR=ON`**:
  `cmake --build build --clean-first` — 431/431 steps, zero errors.
- **Full clean build, `GTE_ENABLE_EDITOR=OFF`**:
  `cmake --build build-editor-off --clean-first` (after a fresh configure
  with `-DGTE_ENABLE_EDITOR=OFF`, needing zero network access since every
  dependency was already fetched on disk) — 364/364 steps, zero errors;
  confirmed neither `FrameDebuggerData.cpp` nor `FrameDebuggerPanel.cpp`
  (nor their test file) were even attempted for compilation.
- **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure`
  (from `build/`) — **100% tests passed, out of 1345** (1 pre-existing
  machine-gated skip), 134.76 sec total — including all 6
  `FrameDebuggerDataTest` cases.
- **Live runtime smoke test**: `GreatTamanaEngine.exe` launched,
  `/get_swapchain` confirmed the menu bar reads "File   Window", the
  Pause/Resume toolbar and full default dock layout render identically
  to every prior phase with real scene content, `/list_tabs` confirmed
  no "Frame Debugger" entry exists, process cleanly stopped.

## Manual-verification limitation (consistent across every phase)

Actually opening the "Frame Debugger" window via a real mouse click on
"Window > Frame Debugger", checking "Enable", clicking into the (always
empty) event tree, and visually confirming the exact pixel layout of
every widget described in `PHASE0_MASTER_STRATEGY.md` could not be
exercised remotely in this environment, in any of the seven phases: the
window is deliberately not part of `EditorPanelCatalog.h` (Locked Design
Decision #6), so `GET /activate_tab` cannot bring it to front, and no
HTTP command endpoint exists anywhere in `src/Network/` capable of
flipping `EditorContext::frameDebuggerWindowOpen` or synthesizing a
menu-click/checkbox-click/tree-row-click of any kind — mirroring
`frame-debugger-1`'s own identical, already-documented gap for its
Pause/Step toolbar. This is a documented, accepted manual-verification
gap, not a defect — every line of ImGui code in this campaign is a
direct, reviewed application of already-working, already-shipped idioms
used identically elsewhere in this same codebase (`ProjectPanel.cpp`'s
splitter, `PlaybackControls.cpp`'s Pause-button field write,
`BoneViewerWindow`'s open/close-bool pattern), so it is expected to
render and behave exactly as each phase document's own description states
once triggered by a human via a real click. Recorded in `TODO.md`'s new
"Frame Debugger (scaffolding)" section for future reference.

## Outstanding / deferred (see `TODO.md` for the full list)

- Any real frame/draw-call/render-pass capture logic whatsoever.
- Any real shader/material property introspection.
- A frame-history ring buffer / scrubbing through multiple past frames.
- Any Network/HTTP endpoint for the Frame Debugger.
- Adding "Frame Debugger" to `EditorPanelCatalog.h`/the default dock
  layout.
- A `RenderGraphPanel`/`ProfilerPanel`-style Pause/frozen-snapshot control
  on this panel.
- Interactive click-driven Enable/tree-row-selection verification via
  automation (no mouse-control tool available in this environment).

## Conclusion

All seven phases of `frame-debugger-2` landed exactly per
`PHASE0_MASTER_STRATEGY.md`'s plan, with no unresolved deviations. The
Editor now has a complete, GUI-only "Frame Debugger" window — toolbar,
frame stepper, a draggable-splitter event tree, and a fully-built
inspector pane (frame-level chrome plus an event-details section with a
tab bar) — every value deliberately a disabled control, a placeholder
message, or an inert default, with three clearly documented "glue seams"
ready for a future real-capture campaign to plug real data into without
touching a single ImGui call in this window again. Verified by a full
clean build (both Editor configurations), a full `ctest` regression pass
(100% of 1345 tests), and a live runtime smoke test.
