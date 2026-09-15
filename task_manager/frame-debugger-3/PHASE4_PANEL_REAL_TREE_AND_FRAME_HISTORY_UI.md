# PHASE4 — Panel wiring: real tree, real selection, and the new Frame-History UI

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decision #3, #4, #5).
## Depends on: `PHASE1`..`PHASE3` (already landed).

## Step 1: The Goal

Make the Frame Debugger window ACTUALLY SHOW something real for the first time in this campaign:
swap the event tree/inspector chrome over from `BuildPlaceholderFrameDebuggerSnapshot()` to
PHASE3's `FrameDebuggerHistory`'s currently-viewed entry, add the new Frame-History mini-toolbar
(Prev/Next captured frame), and display the real retained preview texture in the RenderTarget
preview box instead of the "No Texture" placeholder.

## Step 2: The Situation

- `FrameDebuggerPanel::BuildEventTreePane()`/`RenderEventNode()`
  (`src/Editor/Panels/FrameDebuggerPanel.cpp`) are ALREADY fully written and correct against ANY
  real, non-empty `FrameDebuggerSnapshot` tree shape — `frame-debugger-2`'s own PHASE4 built this
  as "a fully-written (currently unreachable) recursive tree-row renderer... ready for real data
  the moment a future campaign starts returning non-empty snapshots." This phase is the moment.
  Do NOT rewrite `RenderEventNode()` — if it doesn't already do the right thing against a real
  tree, that is a genuine, reportable bug in `frame-debugger-2` to fix minimally, not a signal to
  redesign it.
- `FrameDebuggerPanel::BuildInspectorPane()`'s RenderTarget selector/Channels row/Levels
  slider/preview box are ALSO already fully built (`frame-debugger-2`'s own PHASE5) — this phase
  only needs to swap the preview box's content source (still a "No Texture" placeholder whenever
  `FrameDebuggerHistory::CurrentEntry()->preview` is `std::nullopt`, e.g. before the very first
  real capture ever happens).
- **How does an existing panel display a live `RenderTexture` inside `ImGui::Image()`?** Read
  `Panels/GamePanel.cpp`'s own existing implementation FIRST (it already solves exactly this
  problem for the live Game View) and copy its exact descriptor-set-wrapping mechanism
  (`ImGui_ImplVulkan_AddTexture` or whatever this codebase's own real call is) rather than
  reinventing it — the only difference here is the source texture is PHASE3's retained HISTORICAL
  copy, not the live, currently-rendering Game View.
- **Do not confuse the two different "stepper" concepts** (Locked Design Decision #4): the
  EXISTING frame-stepper row (`BuildFrameStepperRow()`, `FormatFrameStepperLabel()`) means
  "which EVENT, within the currently-viewed captured frame, is selected" — it should now show real
  numbers (e.g. `"1 of 2"` when a captured frame has the `"GameView"` leaf plus one GPU-skinning
  leaf). The NEW Frame-History mini-toolbar this phase adds is a COMPLETELY SEPARATE control for
  "which CAPTURED FRAME (of up to 8) is being viewed" — do not merge these two into one widget.

## Step 3: The Plan

### 3.1 Swap the snapshot source

Wherever `FrameDebuggerPanel::Build()` currently calls `BuildPlaceholderFrameDebuggerSnapshot()`
(inside the `if (m_enabled) { ... }` branch), change it to read
`m_history.CurrentEntry()`: if `nullptr` (no capture has ever happened yet, e.g. the window was
just opened but "Enable"/"Capture" hasn't run yet this session), fall back to displaying
`BuildPlaceholderFrameDebuggerSnapshot()`'s own empty-tree behavior UNCHANGED (the "No frame
captured yet." message is still exactly correct and honest in that specific state — do not delete
that code path, it is now a real, reachable state again: "enabled, but not yet captured"); once a
real entry exists, use its own real `snapshot` for the rest of `Build()`'s existing logic
unchanged.

### 3.2 New Frame-History mini-toolbar

A new small row (placed between the existing toolbar row and the existing frame-stepper row, or
wherever reads best visually — implementer's call, but keep it visually distinct from the
existing "N of M" event-stepper row so a user never confuses the two) with: a "<" (Prev) button
calling `m_history.StepCursor(-1)`, a label formatted via a NEW small pure helper (e.g.
`FormatFrameHistoryLabel(int cursorIndex, int count)` -> `"Frame 3 of 8"`, added to
`FrameDebuggerData.h/.cpp` alongside the campaign's other pure formatters, Tier-1-tested the same
way `FormatFrameStepperLabel()` already is), and a ">" (Next) button calling
`m_history.StepCursor(+1)`. Both buttons disabled (via `ImGui::BeginDisabled()`) whenever
`m_history.Count() == 0`, or whenever the cursor is already at that respective end (mirror
`ProjectPanel.cpp`'s own existing pattern for disabling an already-at-the-edge navigation button,
if one exists there — otherwise a plain `cursorIndex <= 0`/`cursorIndex >= count - 1` check is
sufficient).

### 3.3 RenderTarget preview box

Inside `BuildInspectorPane()`'s existing preview-box code: if `m_history.CurrentEntry()` is
non-null AND its `preview` has a value, display it via `ImGui::Image()` (per Step 2's own
`GamePanel.cpp` precedent) at whatever size the existing placeholder box already reserves; keep
the exact-existing "No Texture" bordered placeholder box for every other case (no entry yet, or a
GPU-skinning leaf selected whose event legitimately has no texture — see PHASE2's Step 3.1). The
resolution/format caption row (currently always `"0x0  Default"`) becomes real: read the real
`FrameDebuggerRenderTargetInfo` from the current entry's `snapshot.renderTarget` (already
populated for real by PHASE2).

### 3.4 What this phase explicitly does NOT do

- Does not touch `BuildEventDetailsSection()` beyond whatever is strictly required for it to
  compile against the now-sometimes-real `details` (PHASE5's job to verify/polish it against real
  data end-to-end).
- Does not implement Channels/Levels processing (PHASE6) — the preview box shown here is always
  the RAW retained texture, unprocessed.
- Does not add any HTTP endpoint (PHASE7).

### 3.5 Compile check + live smoke test

Fast compile check (`GreatTamanaEngine` target). Because this phase is the first one to produce a
VISIBLE change, also do a live runtime smoke test: launch the engine, use the existing
`Window > Frame Debugger` menu (mouse-driven, or `GET /activate_tab`-style manual verification is
NOT available for this on-demand window per `frame-debugger-2`'s own documented limitation — PHASE7
finally fixes that), check "Enable", click "Capture" if needed, and use `GET /get_swapchain` (once
the window happens to already be inside the main viewport, or after manually dragging it there)
to visually confirm the tree now shows real rows and the preview box shows a real image. If this
manual step is impractical in this environment, defer full visual confirmation to PHASE8's
automation-driven pass, but still confirm a clean, crash-free build/launch here.

### 3.6 File-change inventory

Modified: `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (snapshot source swap, new Frame-History
toolbar, real preview image display, real resolution/format caption), `src/Editor/FrameDebuggerData.h`/
`.cpp` (new `FormatFrameHistoryLabel()` pure helper + its test), `tests/Editor/*` (new test case(s)
for the new formatter).

Write `PHASE4_COMPLETION_REPORT.md` once done.
