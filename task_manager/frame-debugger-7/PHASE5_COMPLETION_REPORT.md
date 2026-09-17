# PHASE5 — Completion Report

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE5_PANEL_UI_AND_HTTP_CLEANUP.md`. Builds on Phase 1
(`FrameDebuggerCurrentCapture`), Phase 2 (deferred capture trigger), Phase 3
(unified step timeline / per-draw replay rendering), and Phase 4 (preview
wiring / data model)._

## Summary

This phase is the campaign's "make sure nothing was left half-done" audit
pass over Phases 1-4's own surface area: a full identifier sweep across
`src/` and `tests/`, a `/frame_debugger/*` HTTP route sanity pass, and
empty/placeholder-state UI polish. **Every single item this phase document
asked to check was already correctly resolved by Phases 1-4** — Phase 1's own
HTTP/identifier removal and Phase 4's own dead-code removal were both
thorough and left zero live dead-code hits anywhere in `src/`/`tests/`. This
phase therefore made **zero functional/behavioral code changes**; it consists
entirely of the verification sweep itself (documented in full below) plus
this report. Per Step 4's own Definition of Done, that is a legitimate,
complete outcome for this phase — the goal was to CONFIRM the surface is
clean, not to force a change where none was needed.

## Step 3.1 — Full identifier sweep (every dead-code hit found and how it was resolved)

Ran `search_in_dir` (recursive) across `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\src`
and `...\tests` for each identifier the phase document named:

| Identifier | Hits in `src/` | Hits in `tests/` | Resolution |
|---|---|---|---|
| `FrameDebuggerHistory` | 54 hits / 13 files | 7 hits / 3 files | **All comment-only.** Every hit is either (a) the literal, intentionally-kept file name `FrameDebuggerHistory.h/.cpp`/`FrameDebuggerHistoryTests.cpp` (Phase 1 explicitly kept the file names to minimize include churn — confirmed, matches Phase 1's own completion report), (b) a doc-comment prose reference to the type's OLD name/history (`FrameDebuggerHistoryEntry` — the payload struct name Phase 1 explicitly kept, per its own report), or (c) a historical-campaign narrative comment (e.g. "PHASE1 (frame-debugger-4) - ..."). No live `class FrameDebuggerHistory` (the ring buffer) exists anywhere — the live class is `FrameDebuggerCurrentCapture`, confirmed by reading `FrameDebuggerHistory.h` itself (line 59 declares `struct FrameDebuggerHistoryEntry`, not a `FrameDebuggerHistory` class). No action needed. |
| `step_history` | 0 | 0 | Already fully removed (Phase 1). No action needed. |
| `StepFrameHistoryFromCommand` | 0 | 0 | Already fully removed (Phase 1). No action needed. |
| `historyCount` | 19 hits / 7 files | 16 hits / 3 files | The only Frame-Debugger-related hits (`FrameDebuggerCommandBridge.h`, `EditorLayer.h`, `NetworkRoutes.h`, `FrameDebuggerData.cpp`) are comment-only, explicitly documenting that this field was REPLACED by `hasCapturedFrame` (Phase 1). Every other hit (`Profiling/FrameGraphData.*`, `Profiling/FrameProfiler.*`, `Profiling/FrameProfilerTests.cpp`, `Profiling/JobScopeTimerTests.cpp`, `Profiling/ScopeTimerTests.cpp`) is a genuine, unrelated, still-live identifier of the CPU Profiler subsystem (`FrameProfiler::HistoryCount()`/`m_historyCount`) that only matched because `search_in_dir` is case-insensitive by default — confirmed by inspection this is a completely different feature/class, out of scope for this campaign. No action needed. |
| `historyCursor` | 4 hits / 4 files | 1 hit / 1 file | All comment-only, all explicitly documenting the `historyCount`/`historyCursor` → `hasCapturedFrame` replacement (Phase 1) or the removed `ClampFrameDebuggerHistoryCursor()` free function (`FrameDebuggerHistoryTests.cpp`'s own top comment). No action needed. |
| `computePassPreviews` | 3 hits / 1 file (`FrameDebuggerHistory.h`) | 0 | All three are comment-only, all explicitly documenting that this field was REMOVED (Phase 4) and replaced by `perObjectStepPreviews`. No live field exists. No action needed. |
| `CollectComputePassTextureWrites` | 1 hit (`FrameDebuggerData.cpp`) | 1 hit (`FrameDebuggerSnapshotBuilderTests.cpp`) | Both comment-only, both explicitly documenting the function's removal (Phase 4). No action needed. |
| `CollectComputePassVolumeTextureWrites` | 1 hit (`FrameDebuggerData.cpp`) | 1 hit (`FrameDebuggerSnapshotBuilderTests.cpp`) | Same as above — comment-only, documenting the removal. No action needed. |
| `ComputePassCopySource` / `HdrComputePassCopySource` | 2 hits (`FrameDebuggerHistory.cpp`) | 0 | Comment-only, explicitly documenting the removal of the whole HDR-round-trip capture code (Phase 4). No action needed. |
| `BuildFrameHistoryToolbarRow` | 0 | 0 | Already fully removed (Phase 1). No action needed. |

**Net result: zero live dead-code hits for any of the nine identifiers this
phase document named.** Every single remaining occurrence is a comment
correctly describing this campaign's own history (a "we used to have X, it
was replaced/removed" note), exactly the acceptable outcome Step 3.1's own
wording anticipated ("resolve every hit (fix, delete, or confirm it's a
comment correctly describing history, never live code)"). No file needed
editing as a result of this sweep.

## Step 3.2 — `FrameDebuggerCommandBridge`/`NetworkRoutes` final pass

Read `src/Application/FrameDebuggerCommandBridge.h` end to end:
`FrameDebuggerCommandKind` is exactly `{OpenWindow, SetEnabled, CaptureNow,
SelectEvent, SetChannel, SetLevels, GetState}` — no `StepFrameHistory` value,
confirmed. `FrameDebuggerStateOutcome` carries `hasCapturedFrame` (not
`historyCount`/`historyCursor`).

Read `src/Network/NetworkServer.cpp`'s route-table function
(`RegisterRoutes()`) end to end: the registered `/frame_debugger/*` routes
are exactly `open`, `enable`, `capture`, `select_event`, `set_channel`,
`set_levels`, `state` — seven routes, matching this phase document's own
expected list exactly, with **no** `step_history` route registered anywhere.
`src/Network/NetworkRoutes.h` was likewise confirmed to have no
`ParsedFrameDebuggerStepHistoryQuery`/`ParseFrameDebuggerStepHistoryQuery`
declarations left (Phase 1 already removed both the declaration and
definition).

`docs/conventions/networking.md` was grepped for `frame_debugger` — **zero
hits** (this file has never enumerated the `/frame_debugger/*` route family
at all; it documents `/get_swapchain`, `/get_game_view`, `/get_texture`,
`/list_textures`, `/activate_tab`, `/list_tabs`, the `EngineCommandBridge`-
backed POST routes, etc., but never the Frame Debugger's own routes). Per
this phase document's own explicit instruction ("only touch it if it actually
enumerates them"), **no edit was made to this file.**

(Note: `docs/conventions/frame-debugger.md` DOES still list a stale
`GET /frame_debugger/step_history?direction=prev|next` line in its own
"HTTP automation" bullet list — but that file is explicitly Phase 6's own job
per `PHASE6_TESTS_DOCS_AND_FINAL_CLEANUP.md`'s Step 3.2 ("This is the single
most important documentation file for this feature... rewrite"), not
Phase 5's — this phase document's own Step 3.2 names `networking.md`
specifically, not `frame-debugger.md`, so this was left for Phase 6 as
designed, and is flagged here for that phase's own author.)

## Step 3.3 — Empty/placeholder state polish

Read `src/Editor/Panels/FrameDebuggerPanel.cpp` end to end (`Build()`, and
every function it calls) and confirmed all three required messages already
exist, are each independently reachable, and are never conflated:

1. `"No frame captured yet."` — `BuildEventTreePane()`, shown exactly when
   `snapshot.rootNodes.empty()` (no capture this session, or cleared by
   Disable/Resume per Phase 1's lifecycle rule, or the one real frame between
   the Enable edge and its deferred capture landing per Phase 2).
2. `"Nothing drawn yet at this point in the frame."` — `BuildInspectorPane()`,
   shown in the texture-preview child window exactly when
   `m_lastPreviewChoice == FrameDebuggerPreviewSourceChoice::NotYetDrawn`
   (a real, valid Pre-GameView compute leaf is selected — Phase 4's
   `NotYetDrawn` bucket), sitting right next to (and never conflated with)
   the separate, ordinary `"No Texture"` fallback for every other
   no-image-available case.
3. `"No event selected."` — `BuildEventDetailsSection()`, shown exactly when
   `m_selectedEventIndex == -1` (`details` has no value) — pre-existing,
   confirmed unchanged and still correct.

All three strings are distinct, grep-verified single hits each in this file,
each gated by its own distinct boolean condition (`rootNodes.empty()` /
`m_lastPreviewChoice == NotYetDrawn` / `!details.has_value()`) — they can
never be shown in place of one another.

**"Frame History" toolbar removal — re-checked for a leftover gap/orphaned
separator**: read `Build()` top-to-bottom. Layout is: `BuildToolbarRow()` →
`ImGui::Separator()` → (current-capture/selection bookkeeping, no UI) →
`BuildFrameStepperRow()` → `ImGui::Separator()` → either the disabled-state
message or the split tree/inspector panes. There is no orphaned
`ImGui::Separator()` call or unexplained vertical gap left over from the
removed Prev/Next toolbar row — Phase 1's removal was clean; no edit needed.

## Step 3.4 — `FrameDebuggerStateSnapshotView`/HTTP `/state` response sanity

Confirmed end-to-end:

- `IEditorLayer::FrameDebuggerStateSnapshotView` (`EditorLayer.h`) carries
  `hasCapturedFrame` (no `historyCount`/`historyCursor`).
- `FrameDebuggerPanel::BuildStateSnapshotView()` sets
  `view.hasCapturedFrame = m_currentCapture.HasCapture();` — a direct,
  correct reflection of Phase 1's `HasCapture()` accessor.
- `Application::Run()`'s bridge-pump copies `stateView.hasCapturedFrame` into
  `fdResult.state.hasCapturedFrame` (per Phase 1's own report — re-confirmed
  the field names still match after Phases 2-4's edits, since none of those
  phases touched this plumbing).
- `NetworkServer.cpp`'s `ToFrameDebuggerStateResponseView()` copies
  `outcome.hasCapturedFrame` into `view.hasCapturedFrame`.
- `NetworkRoutes.cpp`'s `FrameDebuggerStateToJson()`/
  `BuildFrameDebuggerStateResponseJson()` emits
  `body["hasCapturedFrame"] = state.hasCapturedFrame;` — confirmed by reading
  `NetworkRoutes.h`'s own doc comment (lines 698-703), which spells out the
  exact resulting JSON shape:
  `{"enabled":bool,"windowOpen":bool,"hasCapturedFrame":bool,"totalEventCount":int,"selectedEventIndex":int,"channel":"all","levelsBlack":0.0,"levelsWhite":1.0}`
  — no leftover `historyCount`/`historyCursor` field anywhere in this chain.
- `tests/Network/NetworkRoutesTests.cpp`: `BuildFrameDebuggerStateResponseJsonTests.ProducesExactExpectedShape`
  already sets/asserts `state.hasCapturedFrame = true;` /
  `EXPECT_EQ(parsed["hasCapturedFrame"], true);` — no leftover old-field
  assertion exists (re-confirmed by direct grep this phase; Phase 1's report
  already stated this was updated then). No test edit was needed.

## What this phase found overall

**Nothing to fix.** Phases 1 and 4 already performed thorough, correct
removals with no stragglers — every single identifier/route/UI-string this
phase document asked to verify was already exactly right. This phase's own
value-add is the documented, exhaustive confirmation itself (the table above,
plus the route-table/JSON-shape trace), which is what `PHASE0_MASTER_STRATEGY.md`
asked a "make sure nothing was left half-done" phase to produce.

## Verification performed (compile check only, per this phase's own Step 4/workflow rules)

- `cmake --build build --target gte_core -j 4` — `ninja: no work to do`
  (expected: this phase made zero source edits).
- `cmake --build build --target GreatTamanaEngineTests -j 4` — `ninja: no
  work to do` (same reason).
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` — all
  **80 tests passed**, 0 failed — identical count to Phase 4's own last
  verification, confirming zero regressions (expected, since no code changed).
- No full build, no full `ctest` regression run, and no live
  `run_app_background`/`gte_send_request` smoke test were performed this
  phase — per the workflow rules, those are Phase 7's job only.

## Definition of Done — status

- [x] Zero hits for any of the Step 3.1 dead identifiers (other than clearly
      historical comments) — confirmed via the table above.
- [x] The three empty/placeholder states above are each independently
      reachable and readable in the UI — confirmed by code reading
      (`BuildEventTreePane()`/`BuildInspectorPane()`/`BuildEventDetailsSection()`).
- [x] Quick compile check only, performed and passed (both targets reported
      "no work to do" since no source was changed; the existing 80/80
      `*FrameDebugger*` test suite still passes).
- [x] `PHASE5_COMPLETION_REPORT.md` written (this file, listing every dead-code
      hit found and how it was resolved), code (none) + report committed.

No genuine design ambiguity was hit during this phase — the phase document's
own instructions (grep-and-resolve, route-table cross-check, three-message
UI check, `/state` JSON shape check) were all directly executable against the
real current source with an unambiguous, already-correct outcome, so
`ask_questions` was not needed. The one judgment call this phase made
explicit — leaving `docs/conventions/frame-debugger.md`'s stale
`step_history` mention alone, since that file is Phase 6's own explicitly-
scoped job, not Phase 5's `networking.md`-scoped one — is a straightforward
reading of the two phase documents' own respective scopes, not a genuine
ambiguity requiring a human decision.

## Next phase (Phase 6) — what to build on top of this

- `docs/conventions/frame-debugger.md` still contains one stale line (the
  `GET /frame_debugger/step_history?direction=prev|next` bullet in its own
  "HTTP automation" section) that Phase 6's own documentation rewrite should
  remove/replace as part of its planned "What's new (`frame-debugger-7`
  campaign)" section addition — flagged above, not fixed here, since it is
  explicitly out of this phase's own scope (`networking.md`, not
  `frame-debugger.md`).
- Every other identifier/route/UI-state surface this campaign touches is
  confirmed clean going into Phase 6 — no further sweep of `src/`/`tests/`
  should be needed for the nine identifiers this phase re-checked.
