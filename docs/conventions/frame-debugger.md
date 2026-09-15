# Frame Debugger

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The `frame-debugger-2` campaign
(`task_manager/frame-debugger-2/PHASE0_MASTER_STRATEGY.md`, seven phases) gave
the Editor a new **"Frame Debugger"** window, visually modeled on Unity's own
Frame Debugger, as GUI-only scaffolding. The `frame-debugger-3` campaign
(`task_manager/frame-debugger-3/PHASE0_MASTER_STRATEGY.md`, eight phases) then
wired REAL data into every one of that scaffolding's seams: real pass-level
frame capture, a real multi-frame history ring buffer, real shader/blend/Z/
stencil/texture/vector/matrix property reflection, a real Channels/Levels
preview-compositing pipeline, and full HTTP automation. **This is the CURRENT,
real system** — follow these rules whenever touching this window, its data
model, its capture instrumentation, or its cross-wiring with the Pause/Resume
toolbar or the embedded HTTP server:

## What is real today

- **Capture is genuinely real, but PASS-LEVEL, never per-individual-draw-call
  — this is a PERMANENT, locked design fact about this feature, not an
  unfinished gap.** One leaf event = one real `gte::rg::RenderGraphPassSnapshot`
  -backed pass relevant to the Game View (the `"GameView"` pass itself, plus
  zero-or-more upstream GPU-skinning compute passes feeding it that frame) —
  never one leaf per mesh/entity. This is a deliberate, permanent divergence
  from Unity's own per-draw-call granularity (`PHASE0_MASTER_STRATEGY.md`'s
  Locked Design Decision #1), chosen for implementation-cost reasons — do not
  mistake the coarser-than-Unity granularity for something still to be
  finished.
- **Capture is snapshot-ON-DEMAND, never continuous.** A "frame" is captured
  exactly once, on a real trigger (Enable's false→true edge, a Step while
  Enabled, or the explicit "Capture" button), and stays frozen/inspectable
  until the next trigger fires. `FrameDebuggerPanel` never rebuilds its
  displayed snapshot on every ImGui frame.
- **Scope is Game View ONLY, permanently.** `"SceneView"`/`"Present"` passes
  are deliberately excluded from the Frame Debugger's own captured event tree,
  even though they exist in the same underlying `RenderGraphSnapshot` the
  "Render Graph" panel already displays in full. Relaxing this filter to cover
  Scene View/Present is a clean, well-isolated future extension point (a
  single line in the snapshot builder's filter), not attempted by this
  campaign.
- **A REAL multi-frame history ring buffer exists**
  (`FrameDebuggerHistory`/`FrameDebuggerHistoryEntry`,
  `src/Editor/FrameDebuggerHistory.h/.cpp`), holding the last
  `kFrameDebuggerHistoryCapacity` (8) captured frames, each a full,
  independent, already-resolved `FrameDebuggerSnapshot` PLUS its own retained
  GPU copy texture of that historical frame's real Game View output. A
  "Frame History" Prev/Next mini-toolbar
  (`FrameDebuggerPanel::BuildFrameHistoryToolbarRow()`) scrubs across this ring
  buffer — a SEPARATE axis from the pre-existing "N of M" event-stepper row
  (position within the CURRENTLY-VIEWED captured frame's own event list); do
  not conflate the two.
- **Shader/pass-state "reflection" is real, but PASS-scoped, aggregated
  across every real draw call that pass issued that frame — never
  per-individual-mesh.** For the `"GameView"` leaf: `shaderName` lists every
  DISTINCT real `Pipeline` debug name actually used that frame (see
  `Pipeline::DebugName()`/`GpuResourceFactory::CreatePipeline()`'s cosmetic
  `debugName` parameter), `textures` lists every DISTINCT real bound
  `MaterialTexture` debug name, `vectors` includes the real clear color and
  real aggregate `DrawStats` (draw-call count / triangle count, sourced from
  the render graph's own snapshot, never re-derived), `matrices` includes the
  real view-projection matrix that pass actually rendered with that frame,
  and blend/Z/stencil rows report this engine's real, single, constant
  `Pipeline` configuration
  (`DescribeStandardPipelineState()`, `src/Editor/FrameDebuggerCapture.h/.cpp`)
  — this engine has exactly ONE Pipeline configuration today
  (`Pipeline.cpp`: no blend, `VK_COMPARE_OP_LESS` depth test, no stencil
  test anywhere), so there is nothing to fabricate per-mesh here; a genuine
  future per-material blend/Z/stencil VARIATION would need its own follow-up
  campaign, not just a data-plumbing change.
- **Preview reconstruction is a real, cheap "copy right after the one real
  image-producing event finishes"** — not a full mid-pass draw-call replay.
  Because scope is Game-View-only AND granularity is pass-level, the ONLY
  event in the whole captured tree that ever produces a color image is the
  terminal `"GameView"` pass itself; selecting it shows the REAL retained
  texture copy taken right when that pass finished for that historical frame;
  selecting any GPU-skinning leaf shows "No Texture" (real, honest — a
  compute pass has no color image output) alongside real numeric/textual
  details for that compute dispatch (its own real GPU timing sample).
- **Channels (All/R/G/B/A) and Levels are functionally real**, driven by a
  dedicated preview-compositing module
  (`src/Editor/FrameDebuggerPreviewProcessing.h/.cpp`, mirrored by
  `Shaders/FrameDebuggerPreview.comp`): a pure, Tier-1-tested CPU oracle
  (`ApplyFrameDebuggerPreviewTransform()`) applies the Levels remap first,
  then Channel isolation (replicated grayscale, alpha forced opaque), and the
  GPU compute shader mirrors it exactly for the actual on-screen preview,
  dispatched only when the current Channel/Levels state is non-neutral and
  only when something actually changed since the last dispatch (never every
  ImGui frame). This deliberately does **NOT** reuse `/get_texture`'s existing
  `channel=color|depth` query parameter — a different, unrelated meaning (see
  [Networking](networking.md)).
- **The whole feature is drivable end-to-end over the embedded HTTP server,
  with the window forced onto the MAIN ImGui viewport whenever opened this
  way** — closing the "manual-verification limitation" both `frame-debugger-1`
  and `frame-debugger-2` had to accept as a documented gap. See "HTTP
  automation" below.

## The data model and panel (structure, largely unchanged in shape from
`frame-debugger-2`)

- **`src/Editor/FrameDebuggerData.h/.cpp`** is the pure, ImGui-free data
  model - `FrameDebuggerTextureProperty`/`FrameDebuggerVectorProperty`/
  `FrameDebuggerMatrixProperty`/`FrameDebuggerEventDetails`/
  `FrameDebuggerEventNode`/`FrameDebuggerRenderTargetInfo`/
  `FrameDebuggerSnapshot`, plus `BuildRealFrameDebuggerSnapshot()` (the real
  builder, reshaping `gte::rg::RenderGraphSnapshot` +
  `FrameDebuggerCaptureContext` into a real, non-empty snapshot filtered to
  Game-View-only passes — `BuildPlaceholderFrameDebuggerSnapshot()` still
  exists and is still used as the honest empty-tree fallback when nothing has
  been captured yet this session), `FormatFrameStepperLabel()`,
  `FormatFrameHistoryLabel()`, `ClampSelectedEventIndex()`,
  `FindEventDetailsByIndex()`, `FormatVectorProperty()`, and
  `FormatMatrixProperty()`. Mirrors `JobsPanelData.h`/`ProfilerPanelData.h`'s
  own "small, dedicated, directly-testable reshaping module" precedent (see
  [Testability & Regression Safety](../../AGENTS.md#testability--regression-safety)).
  Tier-1-tested by `tests/Editor/FrameDebuggerDataTests.cpp` and
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`.
- **`src/Editor/FrameDebuggerCapture.h/.cpp`** — `FrameDebuggerCaptureContext`,
  a plain, dumb, per-frame recorder threaded through
  `Renderer::Submit()`/`RenderSystem::Draw()` as a defaulted, LAST,
  `nullptr`-by-default parameter (zero overhead whenever disarmed — no
  string formatting, no vector work happens on the overwhelmingly common
  "not currently capturing" frame), aggregating every DISTINCT real
  `Pipeline`/`MaterialTexture` debug name and the last real view-projection
  matrix used this frame. Also home to `DescribeStandardPipelineState()` (see
  above). Tier-1-tested by `tests/Editor/FrameDebuggerCaptureTests.cpp`.
- **`src/Editor/FrameDebuggerHistory.h/.cpp`** — the real ring buffer (see
  above). Tier-1-tested by `tests/Editor/FrameDebuggerHistoryTests.cpp`
  (the pure write-state/cursor-clamp arithmetic).
- **`src/Editor/FrameDebuggerPreviewProcessing.h/.cpp`** — the real
  Channels/Levels compositing (see above). Tier-1-tested by
  `tests/Editor/FrameDebuggerPreviewProcessingTests.cpp`.
- **`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`** is the panel itself — a
  small STATEFUL CLASS (not a free function), following the same
  stateful-panel exception `RenderGraphPanel`/`ProfilerPanel`/
  `BoneViewerWindow` already established (see
  [Editor Module Structure](editor-module-structure.md)). It owns the
  "Enable" toggle (`m_enabled`), the left-hand event-tree pane's persisted
  splitter width, the currently-selected leaf event's index
  (`m_selectedEventIndex`, now genuinely reachable via real tree-row clicks
  OR via the HTTP `select_event` command), the real capture context
  (`m_captureContext`), the real history ring buffer (`m_history`), the real
  Channels/Levels state (`m_channel`/`m_levelsBlack`/`m_levelsWhite`), its own
  preview-texture ImGui descriptor (self-owned, mirroring
  `BoneViewerWindow`'s own "owns its own `ImGui_ImplVulkan_AddTexture()`
  descriptor" precedent), and its own `FrameDebuggerPreviewRenderer`
  instance. Called explicitly by name from `ImGuiEditorLayer::BuildUI()` — no
  `IEditorPanel` interface.
- **An ON-DEMAND FLOATING WINDOW, not part of the default dock layout, and
  NOT listed in `EditorPanelCatalog.h`.** Still deliberate (mirroring
  `BoneViewerWindow`'s own precedent) — `EditorPanelCatalog.h` exists purely
  to keep `DockLayout.cpp`'s *permanently-docked default layout* names in
  sync with `GET /activate_tab`/`GET /list_tabs`; this window's own HTTP
  automation goes through its OWN dedicated `/frame_debugger/*` route family
  instead (see below), never through `/activate_tab`.
- **Opened/closed via the "Window" top-level menu** (hand-driven) OR via
  `GET /frame_debugger/open` (HTTP-driven) — both ultimately flip the same
  shared `EditorContext::frameDebuggerWindowOpen` bool; only the HTTP path
  also arms the one-shot main-viewport pin (see "Main-viewport pinning"
  below).
- **The "Enable" checkbox auto-engages the existing Pause/Resume toolbar AND
  triggers the first real capture** (see
  [Time and Playback Pause](time-and-playback-pause.md)): turning "Enable" ON
  sets `ctx.playbackPaused = true` (the same field `PlaybackControls.cpp`'s
  own "Pause" button writes) and calls `TriggerCapture()`, both via a shared
  `ApplyEnabledEdge()` helper used identically by the hand-driven checkbox and
  the HTTP `SetEnabledFromCommand()` entry point, so the two paths can never
  silently diverge. Turning "Enable" back OFF still deliberately does **not**
  auto-resume playback, unchanged from `frame-debugger-2`.
- **The "Editor" mode combo remains a purely cosmetic, permanently-disabled
  stub** — this engine still has no Play/Edit-mode split, so there is nothing
  real for it to switch between.
- **The event tree pane shows a real, non-empty tree the moment a real
  capture has happened**, falling back to "No frame captured yet." only in
  the honest "enabled, but nothing captured this session yet" state. The
  event-details section shows real Shader/Pass/Blend/Z-state/Stencil/
  Textures/Vectors/Matrices data once a real leaf is selected, falling back
  to "No event selected." only when nothing is selected.

## HTTP automation (`frame-debugger-3` PHASE7)

A brand-new, fully independent sibling of `EditorUiCommandBridge`,
`FrameDebuggerCommandBridge` (`src/Application/FrameDebuggerCommandBridge.h/.cpp`
— never an extension of `EditorUiCommandKind`, per `AGENTS.md`'s own rule that
a genuinely new KIND of request gets its own bridge), backs eight new routes
under `src/Network/NetworkRoutes.h/.cpp`/`NetworkServer.cpp`:

- `GET /frame_debugger/open` — opens the window (a `false -> true`
  PROGRAMMATIC transition also arms a genuine ONE-SHOT main-viewport pin, see
  below; never affects the manual "Window > Frame Debugger" menu item's own
  drag-anywhere freedom).
- `GET /frame_debugger/enable?value=true|false` — flips "Enable", exactly
  mirroring the checkbox's own `ApplyEnabledEdge()` behavior.
- `GET /frame_debugger/capture` — fires an explicit new capture (equivalent
  to clicking "Capture").
- `GET /frame_debugger/select_event?index=<n>` — selects a leaf event by
  index (`-1` deselects).
- `GET /frame_debugger/step_history?direction=prev|next` — scrubs the
  Frame-History ring-buffer cursor.
- `GET /frame_debugger/set_channel?value=all|r|g|b|a` — sets the Channels
  isolation (completely separate name/value space from `/get_texture`'s own
  `channel=color|depth`, per the Locked Design Decision above).
- `GET /frame_debugger/set_levels?black=<f>&white=<f>` — sets the Levels
  remap range.
- `GET /frame_debugger/state` — a flat, no-`"success"`-wrapper JSON dump of
  every piece of live state (`enabled`/`historyCount`/`historyCursor`/
  `selectedEventIndex`/`totalEventCount`/`channel`/`levelsBlack`/
  `levelsWhite`/`windowOpen`). Every OTHER route's response ALSO includes this
  same `"state"` object (nested under `success`), so a caller can assert
  state without a second round-trip after every command.

Every route goes through the SAME bridge/pump mechanism (mutex + condition
variable, `SubmitAndWait()` from the network thread,
`TryPeekPendingCommandRequest()`/`FulfillCommand()` pumped once per frame from
`Application::Run()`, at the same point `EditorUiCommandBridge`'s own pump
runs) — including `/state`, which could theoretically read directly, but this
codebase's own "never touch engine/Editor-owned mutable state from the
network thread except through a reviewed bridge" rule (see
[Networking](networking.md)) is deliberately kept with zero exceptions.

### Main-viewport pinning

`ImGuiEditorLayer.cpp` enables `ImGuiConfigFlags_ViewportsEnable` for the whole
Editor, meaning any floating, undocked window (including this one) can
normally be dragged into its own independent OS-level platform window — which
`GET /get_swapchain` categorically cannot see, since it only ever reads back
the MAIN window's own swapchain image. `FrameDebuggerPanel::RequestOpenWindow()`
sets a one-shot `m_pinToMainViewportNextOpen` flag ONLY on a genuine
programmatic `false -> true` open (never on the manual "Window > Frame
Debugger" menu item, which keeps its normal drag-anywhere freedom); `Build()`
then calls `ImGui::SetNextWindowViewport(...)` +
`ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always)` +
`ImGui::SetNextWindowSize(...)` (clamped to the main viewport's own work area)
before `ImGui::Begin()`, then clears the flag — guaranteeing the window is
visible to `GET /get_swapchain` the very first captured frame after an
HTTP-driven open.

## Testing this feature

Every layer above (`FrameDebuggerCaptureContext`, the real snapshot builder,
the history ring buffer's pure write-state/cursor arithmetic, the preview
compositing CPU oracle, the command bridge, and every new HTTP query
parser/response builder) has dedicated Tier-1 test coverage — see
`tests/Editor/FrameDebugger*Tests.cpp`, `tests/Application/
FrameDebuggerCommandBridgeTests.cpp`, and the `ParseFrameDebugger*`/
`BuildFrameDebugger*` cases in `tests/Network/NetworkRoutesTests.cpp`. A full,
genuine, HTTP-driven, screenshot-verified end-to-end smoke test (open →
enable → auto-capture → select event → explicit capture → step history →
set channel → set levels, each step visually confirmed via
`GET /get_swapchain`) is part of this feature's own closing verification —
see `task_manager/frame-debugger-3/PHASE8_COMPLETION_REPORT.md` and
`CAMPAIGN_COMPLETION_REPORT.md` for the full evidence. This finally closes the
manual-verification gap `frame-debugger-1`/`frame-debugger-2` both had to
accept — there is no longer any manual-verification limitation for this
feature.
