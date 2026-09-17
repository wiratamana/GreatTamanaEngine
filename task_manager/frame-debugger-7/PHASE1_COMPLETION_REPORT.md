# PHASE1 — Completion Report

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md`._

## Summary

The old 8-slot `FrameDebuggerHistory` multi-frame ring buffer is gone. Exactly
ONE captured frame is now ever held in memory at a time
(`FrameDebuggerCurrentCapture`), matching Unity's own Frame Debugger. The
"Frame History" Prev/Next toolbar and `GET /frame_debugger/step_history` are
removed end-to-end (panel UI, `IEditorLayer` interface, both editor-layer
implementations, the cross-thread bridge, the HTTP route + its request
parser + its JSON serialization). A new lifecycle rule clears the captured
data (never the "Enable" checkbox itself) whenever Enable is unticked, or
whenever playback resumes while still Enabled.

## Renamed / new identifiers future phases need to know about

- **Class rename**: `gte::FrameDebuggerHistory` → `gte::FrameDebuggerCurrentCapture`
  (file names kept as `src/Editor/FrameDebuggerHistory.h/.cpp` on purpose, per
  the phase doc, to minimize include churn).
  - `Count()` (returned `int`) → `bool HasCapture() const noexcept`.
  - `CursorIndex()` / `StepCursor(int)` → **removed entirely** (no cursor
    concept left — there is only ever one slot).
  - `CurrentEntry()` — **kept**, same signature/contract (`nullptr` when
    nothing captured).
  - **New**: `void Clear() noexcept` — releases the retained GPU textures via
    `std::optional::reset()` (RAII).
  - `CaptureFrame(...)` — **same signature and body logic**, now writes via
    `m_current.emplace()` instead of indexing into a `std::array` ring buffer.
  - Removed: `kCapacity`, `m_entries` (`std::array<FrameDebuggerHistoryEntry, 8>`),
    `m_writeState`/`m_cursor`, `StorageIndexForLogicalIndex()`, the free
    struct `FrameDebuggerHistoryWriteState`, and the two free functions
    `AdvanceFrameDebuggerHistoryWriteState()` / `ClampFrameDebuggerHistoryCursor()`.
  - `FrameDebuggerHistoryEntry` (the per-capture payload struct: `snapshot`,
    `preview`, `compositedPreview`, `computePassPreviews`) — **name kept
    as-is** (the phase doc explicitly says not to touch this part); it is now
    simply the payload of the single `std::optional<FrameDebuggerHistoryEntry>
    m_current` member instead of one slot of an 8-element array.
  - Debug names of retained `RenderTexture`s inside `CaptureFrame()` no
    longer embed a ring-buffer slot index (there is no index anymore) — they
    are now fixed strings like `"FrameDebuggerCurrentCapturePreview"`,
    `"FrameDebuggerCurrentCapturePreviewComposited"`, and
    `"FrameDebuggerCurrentCaptureCompute<passName>"` (previously
    `"FrameDebuggerHistorySlot<N>..."`). This is a pure debug-name cosmetic
    change with zero behavioral effect — nothing reads these strings back
    programmatically.

- **`FrameDebuggerPanel` (`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`)**:
  - Member `m_history` (type `FrameDebuggerHistory`) → `m_currentCapture`
    (type `FrameDebuggerCurrentCapture`).
  - **Removed**: `BuildFrameHistoryToolbarRow()` (both declaration and
    definition) and its call site inside `Build()` — the "Frame History"
    Prev/Next UI row no longer exists at all.
  - **Removed**: `StepFrameHistoryFromCommand(int delta)` (both declaration
    and definition).
  - **New member**: `bool m_wasPlaybackPaused = false;` — tracks
    `ctx.playbackPaused` as of the end of the previous `Build()` call, so the
    next call can detect a paused→running transition.
  - **New lifecycle rule**, implemented in two places:
    1. `ApplyEnabledEdge()` — the **true→false** edge (Enable unticked) now
       also calls `m_currentCapture.Clear()` (added an `else if (!m_enabled
       && wasEnabled)` branch after the existing false→true branch).
    2. `Build()` — at the very TOP of the function, before anything else
       runs: `if (m_enabled && m_wasPlaybackPaused && !ctx.playbackPaused) {
       m_currentCapture.Clear(); }`, followed unconditionally by
       `m_wasPlaybackPaused = ctx.playbackPaused;`. This is the
       "Resume-while-Enabled" half of the rule. In neither case is `m_enabled`
       itself forced back to `false` — only the captured DATA disappears,
       matching the user's own exact wording ("the frame info got removed
       from memory").
  - Every other `m_history.*` call site (`CurrentEntry()`,
    `BuildStateSnapshotView()`, `SelectEventFromCommand()`,
    `EnsurePreviewDescriptor()`, `Build()`'s own snapshot/selection-reset
    logic) was mechanically renamed to `m_currentCapture.*` with no logic
    changes.

- **`IEditorLayer` (`src/Editor/EditorLayer.h`)**:
  - `FrameDebuggerStateSnapshotView::historyCount`/`historyCursor` (both
    `int`) → single `bool hasCapturedFrame`.
  - **Removed**: `virtual void FrameDebuggerStepHistory(int delta) = 0;`
    (and its `ImGuiEditorLayer`/`NullEditorLayer` overrides in
    `ImGuiEditorLayer.cpp`/`NullEditorLayer.cpp`).
  - `FrameDebuggerPanel::BuildStateSnapshotView()` now sets
    `view.hasCapturedFrame = m_currentCapture.HasCapture();` instead of the
    old two-line `historyCount`/`historyCursor` copy.

- **`src/Application/FrameDebuggerCommandBridge.h`**:
  - `FrameDebuggerCommandKind::StepFrameHistory` — **removed** from the enum.
  - `struct FrameDebuggerStepFrameHistoryCommand` — **removed** entirely.
  - `FrameDebuggerCommandRequest::stepFrameHistory` field — **removed**.
  - `FrameDebuggerStateOutcome::historyCount`/`historyCursor` (both `int`) →
    single `bool hasCapturedFrame`.

- **`src/Application/Application.cpp`** (`Application::Run()`'s
  `FrameDebuggerCommandBridge` pump switch statement): the
  `case FrameDebuggerCommandKind::StepFrameHistory:` arm was deleted
  entirely (it used to call
  `m_editorLayer->FrameDebuggerStepHistory(fdRequest->stepFrameHistory.delta)`).
  The two-line
  `fdResult.state.historyCount = stateView.historyCount;` /
  `fdResult.state.historyCursor = stateView.historyCursor;` copy was replaced
  with a single `fdResult.state.hasCapturedFrame = stateView.hasCapturedFrame;`.

- **HTTP surface** (breaking change — documented here exactly like
  `frame-debugger-5`'s own breaking removal was):
  - `GET /frame_debugger/step_history?direction=prev|next` **no longer
    exists** — removed from `src/Network/NetworkServer.cpp`'s route table
    entirely (the `server.Get("/frame_debugger/step_history", ...)`
    registration, its whole lambda body, and its doc-comment mentions in the
    surrounding route-table comment block).
  - `src/Network/NetworkRoutes.h`: `struct ParsedFrameDebuggerStepHistoryQuery`
    and `ParseFrameDebuggerStepHistoryQuery()` — **removed** (declaration and
    the matching definition in `NetworkRoutes.cpp`).
  - `FrameDebuggerStateResponseView::historyCount`/`historyCursor` (both
    `int`) → single `bool hasCapturedFrame` (`NetworkRoutes.h`).
  - `NetworkRoutes.cpp`'s `FrameDebuggerStateToJson()`: the two
    `body["historyCount"] = ...` / `body["historyCursor"] = ...` lines were
    replaced with a single `body["hasCapturedFrame"] = state.hasCapturedFrame;`
    — so `GET /frame_debugger/state` (and every other `/frame_debugger/*`
    response's `"state"` field) now reports `"hasCapturedFrame":bool` instead
    of `"historyCount":int`/`"historyCursor":int`.
  - `NetworkServer.cpp`'s `ToFrameDebuggerStateResponseView()`: the matching
    two-line copy was likewise collapsed to one `view.hasCapturedFrame =
    outcome.hasCapturedFrame;` line.

## Tests updated

- `tests/Editor/FrameDebuggerHistoryTests.cpp` (file name kept) — the old
  ring-buffer arithmetic tests (`AdvanceFrameDebuggerHistoryWriteState()`,
  `ClampFrameDebuggerHistoryCursor()`, and the old `FrameDebuggerHistory`
  count/cursor tests) were deleted and replaced with two small tests against
  the new `FrameDebuggerCurrentCapture` type:
  `FreshCaptureHasNoEntry` (a fresh instance has `HasCapture() == false` and
  `CurrentEntry() == nullptr`) and `ClearOnAFreshCaptureIsASafeNoOp`.
  `CaptureFrame()` itself still needs a live `Renderer`/`RenderGraph` (Tier 2,
  same as before this phase — noted explicitly in the file's own top comment)
  so the `HasCapture() == true` path is not exercised here.
- `tests/Network/NetworkRoutesTests.cpp` — removed the `using
  gte::Network::ParseFrameDebuggerStepHistoryQuery;` /
  `ParsedFrameDebuggerStepHistoryQuery` aliases and both
  `ParseFrameDebuggerStepHistoryQueryTests` test cases
  (`AcceptsPrevAndNext`, `RejectsUnknownDirection`); updated
  `BuildFrameDebuggerStateResponseJsonTests.ProducesExactExpectedShape` to
  set/assert `hasCapturedFrame` instead of `historyCount`/`historyCursor`.
- `tests/Application/FrameDebuggerCommandBridgeTests.cpp` — no changes were
  needed; this file never referenced `StepFrameHistory`/`historyCount`/
  `historyCursor` in the first place.

## Deliberately left untouched (per the phase document's own scope)

- `FrameDebuggerHistoryEntry::computePassPreviews` /
  `FrameDebuggerComputePassPreview` — unchanged, as instructed (Phase 4 of
  this campaign is where the per-compute-pass distinct-texture preview
  mechanism itself gets replaced).
- `src/Editor/FrameDebuggerData.h/.cpp` — untouched. `FormatFrameHistoryLabel()`
  (and its own test, `FrameDebuggerDataTest.FormatFrameHistoryLabelTest`) is
  now unused in production code (its only caller,
  `BuildFrameHistoryToolbarRow()`, was removed this phase) but still compiles
  and still passes; it was intentionally left alone since the phase document
  never asked for it to be touched, and removing it was not required for a
  clean compile.
- A handful of stale comments elsewhere (`FrameDebuggerData.h/.cpp`,
  `FrameDebuggerPreviewProcessing.h/.cpp`, `ImGuiEditorLayer.cpp`,
  `VolumeTexturePreviewRenderer.h/.cpp`) still say `FrameDebuggerHistory::
  CaptureFrame()` in prose — these are comment-only references in files this
  phase was not asked to modify; they do not affect compilation and are
  reasonable cleanup for a later phase/pass if desired.

## Verification performed (Tier 1/2 compile check, per this phase's own Step 3.5)

- `cmake --build build --target gte_core -j 4` — succeeded, rebuilding
  exactly the 7 affected translation units
  (`FrameDebuggerCommandBridge.cpp`, `FrameDebuggerHistory.cpp`,
  `NetworkRoutes.cpp`, `Application.cpp`, `FrameDebuggerPanel.cpp`,
  `ImGuiEditorLayer.cpp`, `NetworkServer.cpp`) plus the library link step,
  with zero errors/warnings introduced.
- `cmake --build build --target GreatTamanaEngineTests -j 4` — succeeded.
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` — all
  **85 tests passed**, 0 failed (includes the new
  `FrameDebuggerCurrentCaptureTest` suite, every existing
  `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerDataTest`/
  `FrameDebuggerCommandBridgeTest`/`FrameDebuggerPreviewProcessingTest`/
  `FrameDebuggerCaptureContextTest` suite, and every
  `ParseFrameDebugger*QueryTests`/`BuildFrameDebugger*ResponseJsonTests`
  suite under `NetworkRoutesTests.cpp`).
- No full build, no full `ctest` regression run, and no live
  `run_app_background`/`gte_send_request` smoke test were performed this
  phase — per the workflow rules, those are Phase 7's job only.

## Definition of Done — status

- [x] `FrameDebuggerHistory.h/.cpp` (class renamed `FrameDebuggerCurrentCapture`)
      compiles with no ring-buffer code left anywhere in it.
- [x] No "Frame History" Prev/Next control exists in the panel UI anymore.
- [x] `GET /frame_debugger/step_history` no longer exists.
- [x] Turning "Enable" off, or resuming playback while Enabled, frees the
      captured frame's GPU textures (via `FrameDebuggerCurrentCapture::Clear()`
      → `std::optional::reset()` → `RenderTexture`'s own RAII destructor) —
      confirmed by code review this phase; full live proof is Phase 7's job.
- [x] This `PHASE1_COMPLETION_REPORT.md` written; code + report committed to
      git together.

No genuine design ambiguity was hit during this phase — the phase document's
own concrete file/line/identifier references matched the real current source
exactly, so `ask_questions` was not needed.
