# PHASE6 — Completion Report

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE6_TESTS_DOCS_AND_FINAL_CLEANUP.md`. Builds on Phase 1
(`FrameDebuggerCurrentCapture`), Phase 2 (deferred capture trigger), Phase 3
(unified step timeline / per-draw replay rendering), Phase 4 (preview wiring /
data model), and Phase 5 (panel UI and HTTP cleanup audit)._

## Summary

This phase was the campaign-wide Tier-1 test audit plus the documentation
rewrite. **The test audit found every touched Tier-1 test file already
correct and complete** — Phases 1-5 each added/updated their own tests
alongside their own code changes, exactly per `AGENTS.md`'s "every change to
Tier 1 code must come with a matching test change" rule, and this phase's own
re-check (reading every file end to end, cross-checking against every prior
`PHASEn_COMPLETION_REPORT.md`) found **zero gaps and made zero test-file
changes**. `tests/CMakeLists.txt` still lists every surviving test file, with
no deleted file left registered. The documentation side needed real work: a
new "What's new (`frame-debugger-7` campaign)" section was added to
`docs/conventions/frame-debugger.md` (immediately after the existing
`frame-debugger-6` section), several other sections of that same file were
rewritten in place to describe the system as it exists TODAY rather than as
it existed before this campaign (the tree-shape/preview-reconstruction
description, the "multi-frame history" bullet, the HTTP route list, the data
model/panel bullets), `AGENTS.md`'s "Frame Debugger" summary paragraph was
rewritten to be terse and reflect the new reality (matching this file's own
style for every other subsystem summary), and `TODO.md`'s "Frame Debugger"
section gained a new `frame-debugger-7` campaign entry (mirroring the
existing `frame-debugger-4`/`frame-debugger-5`/`frame-debugger-6` entries)
plus a correction to its "Per-individual-draw-call event granularity" item
(which was stale — it no longer falls back to the whole-frame preview).

## Step 3.1 — Test audit (file by file)

Re-read every file named in the phase document, end to end, and cross-checked
each one against the relevant prior phase's own completion report:

- **`tests/Editor/FrameDebuggerDataTests.cpp`** — `ChooseFrameDebuggerPreviewSourceTest`
  (rewritten by Phase 4) already covers all four `FrameDebuggerStepPreviewKind`
  values (`NotYetDrawn` always wins even with both whole-frame images present;
  `PreComposite` shows `Preview`/falls to `None` if absent; `PostComposite`
  prefers `CompositedPreview`, falls back to `Preview`, else `None`;
  `PerObjectStep` shows `PerObjectStepPreview` when resolvable, else `None`,
  never falling back to a whole-frame image) plus the "no entry at all" case
  (`hasEntry == false` → `None` regardless of `stepPreviewKind`). No gap
  found — no change needed.
- **`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`** — the six
  `stepPreviewKind`/`stepPreviewIndex` tests Phase 4 added
  (`PreGameViewLeafGetsNotYetDrawnStepPreviewKind`,
  `GameViewLeafGetsPreCompositeStepPreviewKind`,
  `PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex`,
  `PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind`,
  `CompositePassLeafItselfGetsPostCompositeStepPreviewKind`,
  `PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives`) cover
  exactly the node shapes the phase document asked for: a Pre-GameView leaf, the
  `"GameView"` leaf itself, each per-object child (with increasing
  `stepPreviewIndex`, in `capture.DrawRecords()` order), a Post-GameView leaf
  strictly before the composite pass, the composite pass's own leaf (and a
  leaf strictly after it), and the "no composite pass survived this capture"
  fallback. No gap found — no change needed.
- **`tests/Editor/FrameDebuggerHistoryTests.cpp`** (the renamed counterpart per
  Phase 1's rename — file name kept, class renamed to
  `FrameDebuggerCurrentCapture`) — `FreshCaptureHasNoEntry`/
  `ClearOnAFreshCaptureIsASafeNoOp` cover the single-slot `HasCapture()`/
  `Clear()` state machine exactly as the phase document asked. `CaptureFrame()`
  itself needs a live `Renderer`/`RenderGraph` (Tier 2, same as before this
  campaign) — the file's own top comment already documents this explicitly.
  No gap found — no change needed.
- **`tests/Editor/FrameDebuggerCaptureTests.cpp`** — re-read Phase 3's own
  completion report and the real `RenderSystem::Draw()`/`RenderSystem.cpp`
  source: the `maxDrawCount` clipping logic Phase 3 added stayed a single
  plain inline loop `break` (`if (maxDrawCount.has_value() && consideredCount
  >= *maxDrawCount) { break; }`), never extracted into a pure helper function.
  Per `AGENTS.md`'s own "if it can be extracted as a small pure function...
  do that" guidance vs. "sometimes it genuinely can't be cleanly extracted":
  there is no second boolean/branch to combine this decision with (by the
  time this code runs, the ONLY input is `maxDrawCount` itself, already fully
  resolved by the caller) — extracting a one-line `if` into its own named
  free function, with no other logic to combine it with and no other call
  site that would ever reuse it, would be pure ceremony with nothing
  meaningful to unit-test beyond "does an `if` with a `break` work", which the
  existing Tier-1 test suite doesn't need a dedicated case for. This matches
  Phase 2's own precedent for the same kind of "just a read-and-clear/simple
  cutoff, not worth its own named helper" judgment call. No new Tier-1 test
  was needed for this reason; no change was made to this file.
- **`tests/Application/FrameDebuggerCommandBridgeTests.cpp`** — grepped for
  `StepFrameHistory`/`historyCount`/`historyCursor` — zero hits (this file
  never referenced them in the first place, confirmed already true as of
  Phase 1's own report). No change needed.
- **`tests/Network/NetworkRoutesTests.cpp`** — grepped for `FrameDebugger` (54
  hits) and specifically for `step_history`/`historyCount`/`historyCursor` —
  zero hits of the latter three. Every remaining `ParseFrameDebugger*`/
  `BuildFrameDebugger*` test still matches the real, current 7-route surface
  (`open`/`enable`/`capture`/`select_event`/`set_channel`/`set_levels`/
  `state`), and `BuildFrameDebuggerStateResponseJsonTests.ProducesExactExpectedShape`
  already asserts `hasCapturedFrame` (not the old `historyCount`/
  `historyCursor` fields). No change needed.
- **`tests/CMakeLists.txt`** — confirmed `Editor/FrameDebuggerDataTests.cpp`,
  `Editor/FrameDebuggerCaptureTests.cpp`, `Editor/FrameDebuggerHistoryTests.cpp`,
  `Editor/FrameDebuggerSnapshotBuilderTests.cpp`,
  `Editor/FrameDebuggerPreviewProcessingTests.cpp`,
  `Application/FrameDebuggerCommandBridgeTests.cpp`, and
  `Network/NetworkRoutesTests.cpp` are all still listed exactly once each,
  under the `GTE_ENABLE_EDITOR` guard where applicable — no deleted file
  (there were none to delete this campaign; every touched file was renamed-
  in-place or rewritten, never removed) is still referenced. No change
  needed.

**Net result: zero test-file changes made this phase.** This is a legitimate,
complete outcome for a "campaign-wide audit" phase — the goal was to CONFIRM
Phases 1-5's own incremental test updates were thorough, not to force a change
where none was needed (mirroring Phase 5's own "nothing to fix" outcome for
its own dead-code sweep).

## Step 3.2 — `docs/conventions/frame-debugger.md` rewrite

Added a new **"What's new (`frame-debugger-7` campaign)"** section immediately
after the existing "What's new (`frame-debugger-6` campaign)" section,
matching the exact writing style/level of detail every prior campaign's own
section in this file already uses. It covers, explicitly:

1. **The Bug 1 fix** — the deferred capture-trigger mechanism, explaining the
   root cause (`PrepareFrameDebuggerCaptureContext()` arms the capture context
   BEFORE `Game::Render()` runs, but the Enable checkbox is only actually
   clicked LATER that same frame, inside `Build()`) and the fix (all three
   real capture triggers now only arm a pending flag, consumed one frame
   later via the two-bool `m_pendingCaptureTrigger`/`m_replayServicedThisFrame`
   handshake).
2. **The removal of the multi-frame history ring buffer** — called out as an
   EXPLICIT, LOCKED, user-approved BREAKING CHANGE relative to
   `frame-debugger-3`/`frame-debugger-4`.
3. **The removal of the per-compute-pass distinct-texture preview** — called
   out as an EXPLICIT, LOCKED, user-approved BREAKING CHANGE relative to
   `frame-debugger-5`, with an explicit note that raw per-pass texture pixel
   inspection is still possible elsewhere (the "Render Graph" panel /
   `GET /get_texture`) — no diagnostic capability was actually lost.
4. **The new per-object replay-rendering mechanism** — describes the "N
   self-contained, debug-only Render Graph passes, each redrawing objects
   `[0..i]` from scratch" design, why it was chosen over a shared-target-
   plus-mid-pass-copy scheme (`rg::PassContext` has no way to obtain a raw
   `VkImage` mid-pass without widening a type shared by every other pass in
   the engine), and its accepted O(N²) draw-call cost (paid only once per
   explicit capture trigger).
5. Updated the tree-shape diagram's surrounding prose (the "Preview
   reconstruction" bullet in the existing "What is real today" section) so
   EVERY leaf — compute or per-object — now reads as having a real,
   always-available accumulated preview image, removing every remaining
   sentence that implied a compute leaf shows "its own special texture."
6. Updated "Still-deferred future work" with a new item: the O(N)
   shared-target replay optimization that was investigated and explicitly
   deferred during Phase 3's own design (per
   `PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md`'s own Step
   2), since it would require widening `rg::PassContext` — a broader, higher-
   risk change than this campaign's own scope justified.

Beyond the new section itself, the following EXISTING sections of this same
file were rewritten in place (not just appended to) so the file accurately
describes the system exactly as it exists after Phase 5, per this phase's own
Definition of Done:

- The file's own intro paragraph (added a fourth "follow-up campaign,
  `frame-debugger-7`" sentence, mirroring the existing `frame-debugger-4`/`-5`/
  `-6` sentences).
- "What is real today"'s "Capture is snapshot-ON-DEMAND" bullet (now
  describes the genuine one-frame deferral).
- "What is real today"'s "A REAL multi-frame history ring buffer exists"
  bullet — REPLACED entirely with "Exactly ONE captured frame is ever held in
  memory."
- "What is real today"'s "Preview reconstruction is real, but still a cheap
  copy..." bullet — REPLACED entirely with the new per-step accumulated
  re-render mechanism and the `FrameDebuggerStepPreviewKind`-based picking
  rule.
- "The data model and panel" section's four per-file bullets
  (`FrameDebuggerData.h/.cpp`, `FrameDebuggerCapture.h/.cpp`,
  `FrameDebuggerHistory.h/.cpp`, `Panels/FrameDebuggerPanel.h/.cpp`) — all
  four rewritten to reflect the new types/fields/renames
  (`FrameDebuggerCurrentCapture`, `perObjectStepPreviews`,
  `FrameDebuggerStepPreviewKind`, `m_pendingCaptureTrigger`/
  `m_replayServicedThisFrame`, `m_lastPreviewChoice`), including an explicit
  note on why `maxDrawCount`'s clipping logic stayed an inline `break` (Step
  3.1 above).
- The "Enable checkbox" and "event tree pane" bullets later in that same
  section — updated for the deferred-trigger timing and the new
  Disable/Resume-clears-the-capture lifecycle rule.
- "HTTP automation" — updated from eight routes to seven (removed
  `step_history`), and the `/state` JSON field list (`historyCount`/
  `historyCursor` → `hasCapturedFrame`).
- "Testing this feature" — updated to describe the CURRENT Tier-1 coverage
  (dropping the now-removed history-ring-buffer/compute-pass-write-collection
  references) and to point at Phase 7 for this campaign's own live,
  HTTP-driven verification (not yet performed as of this phase).
- "Still-deferred future work" — the O(N) optimization note above.

The `frame-debugger-3`/`frame-debugger-4`/`frame-debugger-5` "Known
limitation, now fixed" sections were deliberately left untouched — they are
historical records of what was true AT THE TIME those campaigns ran, exactly
like the file's own established convention for every prior campaign's history.

## Step 3.3 — `AGENTS.md` summary paragraph

Rewrote the "Frame Debugger" section's summary paragraph. The PRE-Phase-6
version had grown into a long, campaign-by-campaign narrative (accumulated
across `frame-debugger-3`/`-5`/`-6`) that no longer matched this file's own
terse, 2-4-sentence style used for every other subsystem (compare "GPU Vertex
Skinning"/"Atmosphere Scattering"). The new version is a single, condensed
paragraph that states the CURRENT reality — `FrameDebuggerCurrentCapture`
(single-capture, no history), the new per-object replay-rendering preview
mechanism (with the accepted BREAKING CHANGE called out explicitly), the
deferred-capture-trigger fix, and the seven-route HTTP surface — then links
out to `docs/conventions/frame-debugger.md` for full detail, exactly matching
the file's own existing pattern (a short summary + a `Full convention:` link)
used everywhere else in this document.

## Step 3.4 — `TODO.md`

Grepped `TODO.md`'s "Frame Debugger" section for the now-removed mechanisms:

- Added a new `frame-debugger-7` campaign entry (mirroring the existing
  `frame-debugger-4`/`-5`/`-6` entries' own shape) documenting both bug fixes
  and explicitly flagging the two BREAKING CHANGES as superseding two
  specific "DONE" bullets recorded earlier in the same file (the
  `frame-debugger-3`-era "A frame-history ring buffer..." bullet and the
  `frame-debugger-5`-era "eagerly retains a real GPU copy of every surviving
  compute pass's own... write" bullet) — those earlier bullets were left
  untouched, per this file's own established "keep the historical record of
  what was true when it was written" convention (the same convention
  `docs/conventions/frame-debugger.md`'s own "Known limitation, now fixed"
  sections already use), rather than silently rewriting history.
- Corrected the "Still genuinely deferred" section's
  "Per-individual-draw-call event granularity" item — it was stale (it said
  "a per-entity leaf's own preview still falls back to the existing
  whole-frame `compositedPreview`/`preview` image", which was true before
  this campaign but is no longer true: a per-entity leaf's own preview is now
  a real, correct, per-step accumulated image, never a fallback). Updated
  the wording (`PARTIALLY DONE` → `MORE DONE`) to reflect this, while keeping
  the one still-genuinely-open sub-item (a truly ISOLATED/cropped/masked
  image of just one entity's own pixels, which would need a stencil/ID-buffer
  or a full draw-call-level replay — NOT what this campaign built, which is
  an accumulated-up-to-this-step whole-frame image, not an isolated crop).

## Verification performed (targeted test-binary compile+run, per this phase's
own Step 4/workflow rules)

- `cmake --build build --target GreatTamanaEngineTests -j 4` — `ninja: no
  work to do` (expected: this phase made zero source/test-file changes, only
  documentation).
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` — all
  **80 tests passed**, 0 failed — identical count to Phase 4/5's own last
  verification, confirming this phase introduced zero regressions (expected,
  since no code changed).
- No full build, no full `ctest` regression run, and no live
  `run_app_background`/`gte_send_request` smoke test were performed this
  phase — per the workflow rules, those are Phase 7's job only.

## Definition of Done — status

- [x] Every touched Tier-1 file has matching, passing tests — confirmed via
      the file-by-file audit above (Step 3.1) and the targeted
      `*FrameDebugger*` test run (80/80 passed).
- [x] `docs/conventions/frame-debugger.md`, `AGENTS.md`, and `TODO.md`
      accurately describe the system exactly as it exists after Phase 5 —
      confirmed via the rewrite described above.
- [x] `PHASE6_COMPLETION_REPORT.md` written (this file), code (none) + docs
      committed together via `git_add`/`git_commit`.

## Genuine ambiguities encountered

None required `ask_questions`. The phase document's own instructions (re-open
every named test file and confirm coverage; write a new documentation section
in a specified location with specified content; update AGENTS.md/TODO.md if
needed) were all directly executable against the real current source/docs
with an unambiguous outcome. The one judgment call worth recording explicitly
(as the phase document itself asked, for the `maxDrawCount` inline-vs-
extracted decision) is documented in Step 3.1 above.

## Next phase (Phase 7) — what to build on top of this

- This phase made no functional/behavioral code changes — Phase 7's full
  build + live HTTP-driven verification has nothing new to account for from
  this phase beyond the documentation itself now being accurate.
- `docs/conventions/frame-debugger.md`'s new "What's new (`frame-debugger-7`
  campaign)" section, and the corresponding `TODO.md`/`AGENTS.md` updates,
  already forward-reference `task_manager/frame-debugger-7/
  CAMPAIGN_COMPLETION_REPORT.md` (to be written by Phase 7) and
  `PHASE7_LIVE_VERIFICATION_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md` — Phase 7
  should make sure both actually exist and match what this phase's docs
  already promise (a full clean build, a full `ctest` regression pass, and a
  live, HTTP-driven, screenshot-verified proof of both bug fixes).
