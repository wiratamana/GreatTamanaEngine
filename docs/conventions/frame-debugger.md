# Frame Debugger (scaffolding)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The `frame-debugger-2` campaign
(`task_manager/frame-debugger-2/PHASE0_MASTER_STRATEGY.md`, seven phases) gave
the Editor a new **"Frame Debugger"** window, visually modeled on Unity's own
Frame Debugger. Follow these rules whenever touching this window, its data
model, or its cross-wiring with the Pause/Resume toolbar:

- **This is GUI SCAFFOLDING ONLY - no real frame/draw-call capture logic was
  wired in anywhere in this campaign, by design.** Every widget's displayed
  value is a disabled control, a placeholder message ("No frame captured
  yet.", "No event selected."), or an inert default. This is the correct,
  final state of the `frame-debugger-2` campaign - not a bug to "fix" by
  inventing real functionality or fake/mock data. A future campaign wires
  real data into the exact seams described below.
- **`src/Editor/FrameDebuggerData.h/.cpp`** is the pure, ImGui-free data
  model - `FrameDebuggerTextureProperty`/`FrameDebuggerVectorProperty`/
  `FrameDebuggerMatrixProperty`/`FrameDebuggerEventDetails`/
  `FrameDebuggerEventNode`/`FrameDebuggerRenderTargetInfo`/
  `FrameDebuggerSnapshot`, plus `BuildPlaceholderFrameDebuggerSnapshot()`,
  `FormatFrameStepperLabel()`, `ClampSelectedEventIndex()`,
  `FindEventDetailsByIndex()`, `FormatVectorProperty()`, and
  `FormatMatrixProperty()`. Mirrors `JobsPanelData.h`/`ProfilerPanelData.h`'s
  own "small, dedicated, directly-testable reshaping module" precedent (see
  [Testability & Regression Safety](../../AGENTS.md#testability--regression-safety)).
  Tier-1-tested by `tests/Editor/FrameDebuggerDataTests.cpp`.
- **`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`** is the panel itself - a
  small STATEFUL CLASS (not a free function), following the same
  stateful-panel exception `RenderGraphPanel`/`ProfilerPanel`/
  `BoneViewerWindow` already established (see
  [Editor Module Structure](editor-module-structure.md)). It owns the
  "Enable" toggle (`m_enabled`), the left-hand event-tree pane's persisted
  splitter width (`m_leftPaneWidth`, copying `ProjectPanel.cpp`'s own
  draggable-splitter pattern verbatim), and the currently-selected leaf
  event's index (`m_selectedEventIndex`, always `-1` in practice this
  campaign). Called explicitly by name from `ImGuiEditorLayer::BuildUI()`
  (`m_frameDebuggerPanel.Build(m_ctx);`) - no `IEditorPanel` interface.
- **An ON-DEMAND FLOATING WINDOW, not part of the default dock layout, and
  NOT listed in `EditorPanelCatalog.h`.** This is a deliberate, documented
  precedent match with `BoneViewerWindow` (whose own window name is also
  absent from `kKnownEditorPanelNames`) - `EditorPanelCatalog.h` exists
  purely to keep `DockLayout.cpp`'s *permanently-docked default layout*
  names in sync with the Network layer's `GET /activate_tab`/`GET
  /list_tabs` routes (see [Networking](networking.md)); an on-demand
  floating window is a fundamentally different concept from a
  permanently-docked default-layout tab, so it deliberately gets neither a
  catalog entry nor Network `/activate_tab` support. Do not "fix" this as an
  apparent oversight - it is the same non-omission `BoneViewerWindow` has
  always had.
- **Opened/closed via a brand-new "Window" top-level menu**
  (`DockLayout.cpp`'s `BuildDockspaceAndMenuBar()`, immediately after the
  existing "File" menu), a checkable `ImGui::MenuItem("Frame Debugger",
  nullptr, &ctx.frameDebuggerWindowOpen)`. Unlike `BoneViewerWindow`'s own
  private-member-plus-`Open()`-method shape, the trigger here (`DockLayout.cpp`)
  has no reference to a `FrameDebuggerPanel` instance at all, so the toggle
  intent lives directly in `EditorContext::frameDebuggerWindowOpen` (a
  plain, shared `bool`) instead. `ImGui::Begin("Frame Debugger",
  &ctx.frameDebuggerWindowOpen)` is passed that exact same bool as its
  `p_open` parameter, so the window's own titlebar `[x]` close button stays
  in sync with the "Window > Frame Debugger" menu checkmark automatically,
  in both directions, for free.
- **The "Enable" checkbox auto-engages the existing Pause/Resume toolbar**
  (see [Time and Playback Pause](time-and-playback-pause.md)): turning
  "Enable" ON sets `ctx.playbackPaused = true` directly - the exact same
  field `PlaybackControls.cpp`'s own "Pause" button writes. Turning "Enable"
  back OFF deliberately does **not** auto-resume (`ctx.playbackPaused` is
  left exactly as the user last set it via either control) - this asymmetry
  is intentional: a user inspecting a paused frame should not be silently
  un-paused just for unchecking a debug-window toggle. See
  `FrameDebuggerPanel::BuildToolbarRow()`'s own code comment for this exact
  reasoning, so a future reader doesn't "fix" it into a symmetric toggle.
- **The "Editor" mode combo is a purely cosmetic, permanently-disabled
  stub** (`ImGui::BeginDisabled()` around a one-item combo showing only
  `"Editor"`) - this engine has no Play/Edit-mode split (see
  `frame-debugger-1`'s own Locked Design Decision #1), so there is nothing
  real for this control to switch between yet. It exists purely for visual
  parity with the reference screenshot.
- **The frame-stepper row ("N of M") is always disabled and always reads
  "0 of 0"** (`FormatFrameStepperLabel(-1, 0)`) - there is no multi-frame
  capture history to step through yet.
- **The event tree pane always shows "No frame captured yet."** and the
  event-details section always shows "No event selected." - zero fake/mock
  rows anywhere, ever (Locked Design Decision #2). The frame-level chrome
  around it (RenderTarget selector, Channels row, Levels slider,
  texture-preview box) is real, static, always-visible widget structure
  once "Enable" is checked, since those concepts belong to the *whole
  frame*, not to any one event - this split is deliberate; do not blur it by
  inventing fake per-event content to "fill up" the chrome.
- **No frozen/Pause snapshot mechanism inside `FrameDebuggerPanel` itself** -
  unlike `RenderGraphPanel`/`ProfilerPanel`'s own "Pause" checkboxes, there
  is no live, frame-to-frame-changing data yet to freeze a snapshot of.
  `BuildPlaceholderFrameDebuggerSnapshot()` is cheap and pure enough to call
  fresh every single `Build()` call.

## The exact "glue seams" a future real-capture campaign should replace

Everything below is the complete, exhaustive list of what a future campaign
needs to touch to wire real data in - nothing else under
`Panels/FrameDebuggerPanel.cpp` should need to change:

1. **`BuildPlaceholderFrameDebuggerSnapshot()`** (`FrameDebuggerData.h/.cpp`) -
   replace its body (or add a second, real builder function
   `FrameDebuggerPanel::Build()` switches to calling instead) so it returns a
   real, non-empty `FrameDebuggerSnapshot` sourced from an actual captured
   frame (e.g. `gte::rg::RenderGraph`'s own per-frame pass/draw-call
   history, `DrawStats`, `GpuTiming`).
2. **`FrameDebuggerEventNode::details`/`FindEventDetailsByIndex()`** -
   once real tree nodes exist, populate each leaf node's own `details`
   (`FrameDebuggerEventDetails`) with real shader/pass/blend/Z-state/
   stencil/texture/vector/matrix values (e.g. real shader reflection data) -
   `FindEventDetailsByIndex()`'s own recursive lookup already works
   correctly against any real tree shape without any further changes.
3. **`m_selectedEventIndex`'s own click-driven population inside
   `RenderEventNode()`** - already fully wired for real
   (`ImGui::Selectable(...)` sets `m_selectedEventIndex = node.eventIndex;`
   on click); it simply has nothing to click yet, since
   `BuildPlaceholderFrameDebuggerSnapshot()` always returns zero tree nodes.
   Once seam #1 above starts returning real leaf nodes, this seam requires
   zero further code changes to start working.

## Manual verification limitation

Actually opening the "Frame Debugger" window, checking "Enable", and
clicking into the event tree with a real mouse cannot be exercised remotely
in this project's own AI-agent-facing embedded HTTP server
([Networking](networking.md)): the window is deliberately not part of
`EditorPanelCatalog.h` (see above), so `GET /activate_tab` cannot bring it
to front, and no HTTP command endpoint exists anywhere under `src/Network/`
capable of flipping `EditorContext::frameDebuggerWindowOpen` or synthesizing
a checkbox/tree-row click (mirroring `frame-debugger-1`'s own identical,
already-documented "Interactive click-driven Pause/Step/Resume verification"
gap - see `TODO.md`). This is a documented manual-verification gap, not a
defect - the code itself is a direct, reviewed application of already-
working ImGui idioms used identically elsewhere in this codebase.
