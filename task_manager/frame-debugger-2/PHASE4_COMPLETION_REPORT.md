# PHASE4 — Left-hand event tree pane + draggable splitter — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-2/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE4_EVENT_TREE_PANE_AND_SPLITTER.md`

## Summary

Implemented PHASE4's own "Step 3: The Plan" / "3.5 File-change inventory"
exactly as written, verbatim from the phase document's own code listings,
with no deviations. After this phase, the "Frame Debugger" window's
`m_enabled == true` branch (previously PHASE3's placeholder text,
"(event tree + inspector - added in a later phase of this campaign)") is
replaced by a real, draggable-splitter, two-pane layout:

- A left-hand child region (`"FrameDebuggerEventTree"`, persisted width
  `m_leftPaneWidth`, default 320px, clamped every frame against the
  window's current available width exactly like `ProjectPanel.cpp`'s own
  `m_leftPaneWidth`) containing `BuildEventTreePane()`. Since
  `BuildPlaceholderFrameDebuggerSnapshot()` (PHASE1) always returns an
  empty `rootNodes`, this pane always shows exactly one line — **"No
  frame captured yet."** — per `PHASE0_MASTER_STRATEGY.md`'s Locked
  Design Decision #2 (zero fake/mock rows, ever). This is the correct,
  intended, final state for this phase, not a bug or an unfinished stub.
- A thin (`kSplitterWidth = 6.0f`) `ImGui::Button("##FrameDebuggerSplitter", ...)`
  between the two panes: dragging it (`ImGui::IsItemActive()`) adds
  `ImGui::GetIO().MouseDelta.x` to `m_leftPaneWidth` every frame, and
  hovering/dragging it swaps the mouse cursor to
  `ImGuiMouseCursor_ResizeEW` — copied verbatim from
  `src/Editor/Panels/ProjectPanel.cpp`'s own splitter pattern (lines
  ~381–410), adapted only in naming (`FrameDebuggerEventTree`/
  `FrameDebuggerSplitter`/`FrameDebuggerInspector` vs. `Project...`).
- A right-hand child region (`"FrameDebuggerInspector"`) still showing
  PHASE5/PHASE6's own placeholder line for now (untouched by this
  phase — those are separate, later phases).
- A fully-written, currently-unreachable recursive tree-row renderer,
  `RenderEventNode()`: group nodes (`isDrawCall == false`) render as a
  default-open `ImGui::TreeNodeEx` header recursing into `node.children`;
  leaf nodes (`isDrawCall == true`) render as an `ImGui::Selectable` row
  that sets `m_selectedEventIndex = node.eventIndex` on click. This
  function is never actually invoked in practice this campaign — the only
  caller, `BuildEventTreePane()`, only reaches its `for` loop when
  `snapshot.rootNodes` is non-empty, which never happens — but it is
  fully correct and ready for a future real-capture campaign to exercise
  for free the moment `BuildPlaceholderFrameDebuggerSnapshot()` (or its
  eventual real replacement) starts returning actual nodes.

### Modified files (only these two, matching the phase document's §3.5
file-change inventory exactly — no new files this phase)

- `src/Editor/Panels/FrameDebuggerPanel.h`:
  - Added two new private method declarations,
    `BuildEventTreePane(const FrameDebuggerSnapshot&)` and
    `RenderEventNode(const FrameDebuggerEventNode&)`, alongside the
    existing PHASE3 methods.
  - Added two new private members: `float m_leftPaneWidth = 320.0f;` and
    `int m_selectedEventIndex = -1;`, each with the exact doc comments
    from the phase document's §3.1 listing.
- `src/Editor/Panels/FrameDebuggerPanel.cpp`:
  - Added `#include <algorithm>` (for `std::clamp`/`std::max`).
  - Added an anonymous namespace with `constexpr float kSplitterWidth = 6.0f;`.
  - Added `RenderEventNode()` and `BuildEventTreePane()`, copied verbatim
    from the phase document's §3.2 listing.
  - Replaced the PHASE3 `else` branch body inside `Build()` (the
    "(event tree + inspector - added in a later phase of this campaign)"
    placeholder) with the exact splitter-driven two-pane layout from the
    phase document's §3.3 listing — computing `totalAvailWidth`/
    `paneAreaHeight`/`maxLeftWidth`, clamping `m_leftPaneWidth`, then the
    left `BeginChild`/`BuildEventTreePane`/`EndChild`, the splitter
    `Button`/drag/cursor logic, and the right `BeginChild`/placeholder
    text/`EndChild`.

Every line matches the phase document's own code listings verbatim — no
line-number drift, no adaptation needed beyond what the phase document
itself already specified (the `<algorithm>` include).

## Deviations from the phase document

None in the implementation itself. The phase document's §3.4 asks the
smoke test to confirm "with 'Enable' checked, the body now shows a left
pane reading 'No frame captured yet.' and a draggable splitter ... that
actually resizes the left pane" — this specific interactive check
(actually clicking "Window > Frame Debugger" to open the window, then
checking "Enable", then dragging the splitter with a real mouse) remains
**not feasible** from this environment, for the exact same underlying
reason already documented in `PHASE2_COMPLETION_REPORT.md` and
`PHASE3_COMPLETION_REPORT.md`: the Frame Debugger window is deliberately
not part of `EditorPanelCatalog.h` (Locked Design Decision #6), so
`GET /activate_tab` cannot bring it to front, and confirmed by re-checking
during this phase that no HTTP command endpoint exists anywhere in
`src/Network/` capable of flipping an arbitrary `EditorContext` bool
(`ctx.frameDebuggerWindowOpen`) or synthesizing a mouse click/drag — the
only command-mutating endpoints are ECS-scoped
(`instantiate_primitive`/`delete_entity`/`set_entity_trs`/
`instantiate_light`) or the catalog-gated `activate_tab`. This is a
pre-existing, expected limitation of the remote-smoke-test tooling in
this environment, not a defect introduced by this phase. The
`BuildEventTreePane()`/`RenderEventNode()`/splitter code itself is a
direct, reviewed, verbatim copy of the phase document's own listing and
the pre-existing `ProjectPanel.cpp` splitter idiom it deliberately
mirrors byte-for-byte (only identifier names differ), so it is expected
to behave identically to that already-shipped, already-working splitter
once triggered by a human via a real click-and-drag.

## Compile check (fast, per this phase's own §3.4 instructions — not a
full clean rebuild/regression)

Ran exactly the command PHASE4's own "3.4 Compile check" section
specifies:

```
cmake --build build --target GreatTamanaEngine
```

Result: **succeeded** — compiled the modified
`src/Editor/Panels/FrameDebuggerPanel.cpp` and the transitively-affected
`src/Editor/ImGuiEditorLayer.cpp`, relinked `libgte_core.a`, and relinked
`GreatTamanaEngine.exe` — no warnings or errors from any of the new/
modified code.

### Live runtime smoke test

1. `run_app_background` launched the freshly-built
   `build/GreatTamanaEngine.exe` (PID 13592).
2. `gte_send_request` `GET /get_swapchain` — returned a `200 image/png`
   frame; visually confirmed the main Editor menu bar still reads
   "File   Window", the existing Pause/Resume toolbar renders exactly as
   before, and the full default dock layout (Hierarchy/Scene/Game/
   Inspector/Memory/Profiler/Render Graph/Atmosphere/Jobs/Project) still
   renders correctly with real scene content — no visual regression
   anywhere from this phase's changes (the Frame Debugger window itself
   is closed by default, `EditorContext::frameDebuggerWindowOpen` starting
   `false`, so nothing new is visible on this particular screenshot —
   expected, matching PHASE2/PHASE3's own identical observation).
3. `gte_send_request` `GET /list_tabs` — confirmed the response still
   lists exactly the same ten pre-existing panels with **no** "Frame
   Debugger" entry — re-confirming Locked Design Decision #6 still holds
   after this phase's changes.
4. `stop_app_background` cleanly terminated the process.

As with PHASE2/PHASE3, actually opening the "Frame Debugger" window,
checking "Enable", and dragging the splitter with a real mouse could not
be exercised remotely in this environment (see "Deviations" above) — the
code itself is a verbatim, reviewed copy of the phase document's own
listing and the pre-existing, already-shipped `ProjectPanel.cpp` splitter
idiom it deliberately mirrors.

## Next step

PHASE5 (`PHASE5_INSPECTOR_PANE_FRAME_LEVEL_CHROME.md`) — the right-hand
pane's frame-level static chrome (RenderTarget selector, Channels toggle
row, Levels slider, texture-preview placeholder box). Not started as part
of this phase.
