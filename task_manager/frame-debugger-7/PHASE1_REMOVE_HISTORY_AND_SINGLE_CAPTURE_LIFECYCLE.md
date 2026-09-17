# PHASE1 — Remove the multi-frame history ring buffer; single-capture lifecycle

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first._

## Step 1: The Goal

Unity's own Frame Debugger does not remember past frames — it shows you
"what happened in the frame you just captured", and that's it. Replace this
engine's 8-slot `FrameDebuggerHistory` ring buffer with exactly ONE captured
frame's worth of retained data, freed the instant it's no longer valid
(Disable, or Resume-while-Enabled).

## Step 2: The Situation

`src/Editor/FrameDebuggerHistory.h/.cpp` today implements a real ring buffer:
`kCapacity = 8`, `m_entries` (a `std::array<FrameDebuggerHistoryEntry, 8>`),
`m_writeState`/`m_cursor`, `StepCursor()`, `AdvanceFrameDebuggerHistoryWriteState()`,
`ClampFrameDebuggerHistoryCursor()`, `StorageIndexForLogicalIndex()`. The
panel (`Panels/FrameDebuggerPanel.h/.cpp`) exposes a "Frame History"
Prev/Next mini-toolbar (`BuildFrameHistoryToolbarRow()`), and the HTTP layer
exposes `GET /frame_debugger/step_history?direction=prev|next`
(`src/Application/FrameDebuggerCommandBridge.h/.cpp`,
`src/Network/NetworkRoutes.h/.cpp`). None of this is needed anymore; all of
it must go.

## Step 3: The Plan

### 3.1 — Collapse `FrameDebuggerHistory` to a single slot

In `src/Editor/FrameDebuggerHistory.h`:

- Remove `kCapacity`, `m_entries` (the `std::array`), `m_writeState`,
  `m_cursor`, `StorageIndexForLogicalIndex()`, `StepCursor()`,
  `CursorIndex()`, `AdvanceFrameDebuggerHistoryWriteState()`,
  `ClampFrameDebuggerHistoryCursor()`, and the free functions
  `FrameDebuggerHistoryWriteState`/`AdvanceFrameDebuggerHistoryWriteState()`/
  `ClampFrameDebuggerHistoryCursor()`.
- Replace storage with a single `std::optional<FrameDebuggerHistoryEntry> m_current;`.
- Rename the class from `FrameDebuggerHistory` to `FrameDebuggerCurrentCapture`
  (keep the file name `FrameDebuggerHistory.h/.cpp` to minimize include churn,
  but update every `#include`r to use the new class name — grep for
  `FrameDebuggerHistory` across `src/` and `tests/` to find every call site,
  do not miss any). Update the file's own top-of-file comment to explicitly
  say: "NOT a multi-frame history (frame-debugger-7 campaign removed the old
  8-slot ring buffer) — exactly ONE captured frame is ever retained, matching
  Unity's own Frame Debugger, which does not save frame history either."
- `Count()` becomes `bool HasCapture() const noexcept { return m_current.has_value(); }`
  (drop the old `int Count()` — nothing needs "how many", only "is there one").
- `CurrentEntry()` stays, now trivially `return m_current ? &*m_current : nullptr;`.
- Add `void Clear() noexcept { m_current.reset(); }` — releases the retained
  `RenderTexture`(s) via `std::optional::reset()` (RAII — see `AGENTS.md`).
- `CaptureFrame(...)` keeps its exact same signature/body logic (it already
  writes "the next slot" — now there is only ever one slot, so it simply
  does `m_current.emplace()` / rebuilds `*m_current` in place, same
  transition-copy-transition-back Vulkan sequence as before). Keep the
  `preview`/`compositedPreview` retained-copy logic exactly as-is for THIS
  phase — Phase 3/4 is where the actual set of retained images changes;
  Phase 1 is a pure structural/lifecycle change only, so it stays
  independently compileable and low-risk.
- `FrameDebuggerHistoryEntry::computePassPreviews`/
  `FrameDebuggerComputePassPreview` stay untouched for now (Phase 4 removes
  them) — do not touch that logic in this phase.

### 3.2 — Panel changes (`Panels/FrameDebuggerPanel.h/.cpp`)

- Remove `BuildFrameHistoryToolbarRow()` entirely and its call site inside
  `Build()`.
- Every place that called `m_history.Count()`/`m_history.CursorIndex()`/
  `m_history.StepCursor()` must be updated for the new API
  (`HasCapture()`/`Clear()`). Rename the member `m_history` to
  `m_currentCapture` (matches the renamed class) — again, grep thoroughly.
- Remove `StepFrameHistoryFromCommand(int delta)` from the public API (both
  declaration and definition) — nothing calls into a "which frame" cursor
  anymore.
- **New clearing rule (LOCKED, per `PHASE0`'s Step 1):**
  - `ApplyEnabledEdge()`'s true→false branch (Enable unticked) must call
    `m_currentCapture.Clear()` (in addition to whatever it already does).
  - Add a small "did we just resume while still Enabled" check inside
    `Build()`: keep a new member `bool m_wasPlaybackPaused = false;`. At the
    TOP of `Build()` (before anything else runs), if `m_enabled &&
    m_wasPlaybackPaused && !ctx.playbackPaused` (i.e. paused→running
    transition happened, most likely via the "Resume" button in
    `PlaybackControls.cpp`, which this class has no direct hook into — it
    only shares `EditorContext`), call `m_currentCapture.Clear()`. Then
    unconditionally refresh `m_wasPlaybackPaused = ctx.playbackPaused;`
    at the end of that same check, every `Build()` call.
  - Do NOT force `m_enabled` back to `false` when this clearing happens —
    only the captured DATA disappears (matches the user's own exact
    wording: "the frame info got removed from memory"); the checkbox
    itself stays wherever the user left it.
- `BuildEventTreePane()`/`BuildInspectorPane()` etc. keep working exactly as
  before, just reading through `m_currentCapture.CurrentEntry()` instead of
  a history cursor — this should be a small, mechanical rename in most
  places.

### 3.3 — HTTP surface (`src/Application/FrameDebuggerCommandBridge.h/.cpp`,
`src/Application/Application.h/.cpp`, `src/Network/NetworkRoutes.h/.cpp`,
`src/Network/NetworkServer.cpp`, `src/Editor/EditorLayer.h`,
`src/Editor/ImGuiEditorLayer.cpp`, `src/Editor/NullEditorLayer.cpp`)

**Confirmed against the real current source this revision — do not skip
`Application.cpp`/`Application.h`, an earlier draft of this file's own list
omitted them even though they genuinely reference every identifier below:**

- `src/Application/FrameDebuggerCommandBridge.h` — remove
  `FrameDebuggerCommandKind::StepFrameHistory`, the
  `FrameDebuggerStepFrameHistoryCommand` struct, and
  `FrameDebuggerCommandRequest::stepFrameHistory`. Replace
  `FrameDebuggerStateOutcome::historyCount`/`historyCursor` with a single
  `bool hasCapturedFrame;` (mirrors the `EditorLayer.h` change below —
  keep both independent types' shapes in sync by hand, same as they already
  are today).
- `src/Application/Application.cpp` — inside `Application::Run()`'s
  `FrameDebuggerCommandBridge` pump (the `switch (fdRequest->kind)` block,
  currently around line 396-426): delete the
  `case FrameDebuggerCommandKind::StepFrameHistory:` arm entirely (it
  currently calls `m_editorLayer->FrameDebuggerStepHistory(fdRequest->stepFrameHistory.delta)`).
  Immediately below that switch, replace the two lines
  `fdResult.state.historyCount = stateView.historyCount;` /
  `fdResult.state.historyCursor = stateView.historyCursor;` with a single
  `fdResult.state.hasCapturedFrame = stateView.hasCapturedFrame;`.
- Delete the `GET /frame_debugger/step_history` route registration
  (`src/Network/NetworkServer.cpp`, currently
  `server.Get("/frame_debugger/step_history", ...)`), its request parser
  (`src/Network/NetworkRoutes.h/.cpp` — the real names are
  `ParsedFrameDebuggerStepHistoryQuery` (struct) and
  `ParseFrameDebuggerStepHistoryQuery()` (function), confirmed against the
  current source), and every bit of handler plumbing end-to-end. This is a
  genuine breaking change to the HTTP surface — document it plainly in this
  phase's completion report (mirrors how `frame-debugger-5` documented its
  own breaking removal of the old GPU-Skinning name-list parameter).
- `IEditorLayer::FrameDebuggerStepHistory(int delta)` (`src/Editor/EditorLayer.h`)
  — remove the virtual declaration entirely, plus its
  `ImGuiEditorLayer`/`NullEditorLayer` overrides (`ImGuiEditorLayer.cpp`/
  `NullEditorLayer.cpp`), plus `FrameDebuggerPanel::StepFrameHistoryFromCommand(int delta)`
  (both declaration in `Panels/FrameDebuggerPanel.h` and definition in
  `Panels/FrameDebuggerPanel.cpp` — already called out in Step 3.2 above,
  cross-referenced here so the HTTP-side removal and the panel-side removal
  are done together, not left half-done from either direction).
- `FrameDebuggerStateSnapshotView` (`src/Editor/EditorLayer.h`) currently
  exposes `historyCount`/`historyCursor` — replace with a single
  `bool hasCapturedFrame;` (or similar) reflecting the new
  `HasCapture()` — update `FrameDebuggerPanel::BuildStateSnapshotView()`
  accordingly (currently sets `view.historyCount = m_history.Count();` /
  `view.historyCursor = m_history.CursorIndex();` — replace both lines with
  a single `view.hasCapturedFrame = m_currentCapture.HasCapture();`), and
  every JSON-building code in `NetworkRoutes.cpp`/`NetworkServer.cpp` that
  serializes this view: `NetworkRoutes.h`'s own, completely independent
  `FrameDebuggerStateResponseView` struct has a parallel `int historyCount`/
  `historyCursor` pair too (currently around line 698) — update it the same
  way, and its `NetworkRoutes.cpp` JSON-body serializer (the
  `FrameDebuggerStateToJson()` free function, currently
  `body["historyCount"] = state.historyCount;` and its `historyCursor`
  sibling line), plus `NetworkServer.cpp`'s own
  `ToFrameDebuggerStateResponseView()` conversion function, which currently
  has its own `view.historyCount = outcome.historyCount;`/`historyCursor`
  copy pair.

### 3.4 — Tests

- `tests/Editor/FrameDebuggerHistoryTests.cpp` — delete every test covering
  `AdvanceFrameDebuggerHistoryWriteState()`/`ClampFrameDebuggerHistoryCursor()`
  (those functions no longer exist). Add/keep a small test proving: a fresh
  `FrameDebuggerCurrentCapture` has `HasCapture() == false`; after a (mocked/
  minimal) capture it becomes `true`; `Clear()` brings it back to `false`.
  Since `CaptureFrame()` itself needs a live `Renderer`/`RenderGraph`
  (Tier 2, untestable directly — same as today, see the file's own existing
  doc comments), keep testing scope to whatever is genuinely pure (the
  `HasCapture()`/`Clear()` state machine itself, if it can be isolated;
  otherwise note plainly in the test file why this part stays Tier 2-only,
  exactly like the existing file already does for `CaptureFrame()`'s Vulkan
  body).
- `tests/Application/FrameDebuggerCommandBridgeTests.cpp` /
  `tests/Network/NetworkRoutesTests.cpp` — remove every `step_history`-
  related test case.

### 3.5 — Compile check only

Per `AGENTS.md`/project workflow: run a fast compile check
(`cmake --build build` targeting just the affected object files/library is
fine, or a full `gte_core`/Editor target build if faster to reason about —
implementer's judgement) — do **not** run the full regression suite or a
full rebuild-everything pass yet (that is Phase 7's job).

## Step 4: Definition of Done

- `FrameDebuggerHistory.h/.cpp` (class renamed `FrameDebuggerCurrentCapture`)
  compiles with no ring-buffer code left anywhere in it.
- No "Frame History" Prev/Next control exists in the panel UI anymore.
- `GET /frame_debugger/step_history` no longer exists.
- Turning "Enable" off, or resuming playback while Enabled, provably frees
  the captured frame's GPU textures (verified at minimum by code review —
  full live proof happens in Phase 7).
- A short `PHASE1_COMPLETION_REPORT.md` is written in this folder describing
  every renamed symbol (so Phase 2+ authors can find them), and the change
  is committed to git.

If, while implementing this, you find a genuine design ambiguity not
answered above (e.g. an unexpected extra caller of a removed API this
document didn't anticipate), use `ask_questions` to ask a human rather than
guessing — and if this phase itself ever delegates further sub-work, tell
that delegated task to use `ask_questions` too, the same way.
