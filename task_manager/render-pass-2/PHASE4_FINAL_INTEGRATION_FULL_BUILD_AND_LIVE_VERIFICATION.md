# PHASE4 — Final Integration: Full Build, Full Regression, Live Verification

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). Read
that file first. Assumes PHASE1, PHASE2, and PHASE3 have all already
landed — read all three completion reports first; PHASE3's own list of
"which tests actually needed updates" is the most load-bearing one to
re-check here._

## Step 1: The Goal (Where are we going?)

This is the ONLY phase in this campaign that runs a full, clean-equivalent
build, a full `ctest` regression run across the ENTIRE existing suite (not
just this campaign's own scoped subset), and a live, HTTP-driven Frame
Debugger screenshot verification against the real, running engine —
confirming the exact target tree shape `PHASE0_MASTER_STRATEGY.md`'s own
Step 1 diagram describes is now genuinely shipped, end to end, in the real
application. This phase also writes the campaign's own final
`CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation (Where are we now?)

- PHASE1 shipped `rg::RenderPassDrawKind` + `"DrawSkyBackground"` tagged
  `DrawQuad`.
- PHASE2 shipped the actual Frame Debugger tree-ownership fix
  (`WrapPassWithOwnedChildEvent()` + its three call sites).
- PHASE3 brought every existing test back to green (scoped run only) and
  updated the two documentation files.
- Nothing has run a FULL build or FULL `ctest` regression pass yet this
  whole campaign — every earlier phase deliberately scoped its own
  verification narrowly (PHASE0's own Cross-Cutting Rules). This phase is
  where any cross-phase integration issue (a stale include, a forgotten
  call site elsewhere in `src/` that also needed the `drawKind` parameter,
  an unrelated regression this campaign accidentally introduced elsewhere
  in the ~1578-test suite) would first surface.
- `render-pass-1`'s own `PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`
  and `CAMPAIGN_COMPLETION_REPORT.md` are the direct template for this
  phase's own shape and level of detail — read them once for the exact
  live-verification methodology (launch engine in background, HTTP
  `/frame_debugger/open` → `/enable` → `/capture` → `/get_swapchain` →
  `/select_event` sequence) before starting.

## Step 3: The Plan

### 3.1 Full build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`). Confirm zero errors
and zero new warnings. If anything fails, diagnose and fix directly (this
is the one phase explicitly allowed to do so) rather than delegating a
whole new phase for a small integration fix — but if the fix is
non-trivial or touches a design decision not already covered by PHASE0-3,
use `delegate_task` (with the mandatory `ask_questions` instruction passed
along, per PHASE0's Cross-Cutting Rules) to hand off a focused fix task
instead of guessing.

### 3.2 Full regression test

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
Debug --output-on-failure`. Compare against `render-pass-1`'s own
`CAMPAIGN_COMPLETION_REPORT.md` baseline (**1578 tests run, 100% passing**,
with `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
correctly `GTEST_SKIP()`-ing) — the total test COUNT will now be HIGHER
than 1578 (this campaign's own PHASE1/PHASE3 added new tests), but the
PASS RATE must still be 100% (modulo that one pre-existing, expected,
environment-gated skip). Any regression found here that traces back to an
earlier phase's own change must be fixed directly in THIS phase (small,
localized fix) or delegated as a focused follow-up task (same
`ask_questions` rule applies to that delegation) — never silently ignored
or "explained away" without a fix.

### 3.3 Live launch + Frame Debugger verification

1. Launch the built `GreatTamanaEngine.exe` in the background
   (`run_app_background`).
2. `GET /frame_debugger/open` — expect `200`.
3. `GET /frame_debugger/enable?value=true` — expect `200`.
4. `GET /frame_debugger/capture` — expect `200`, `hasCapturedFrame: true`.
5. Spawn at least one primitive via `POST /instantiate_primitive` (mirrors
   `render-pass-1` PHASE7's own methodology) so `"RenderOpaque"` has a real
   per-entity child to show alongside everything else, then re-capture.
6. `GET /get_swapchain` — take a screenshot and visually confirm, via
   `load_image`/direct visual inspection of the returned image:
   - `"Compute LUT"` is expandable, AND each individual sub-pass under it
     (e.g. `"AtmosphereTransmittanceLutPass"`) is ALSO now individually
     expandable with a `"Compute Dispatch"` child — this was NOT true
     before this campaign.
   - `"RenderOpaque"` is unchanged — still expandable with its per-entity
     children.
   - **`"DrawSkyBackground"` is now expandable**, with exactly one child
     visible once expanded, named `"Draw Quad"` — this is the literal,
     original bug report, now visibly fixed in the real running UI.
   - `"Compute Dispatches (Post-GameView)"`'s own sub-pass
     (`AtmosphereAerialPerspectiveCompositePass`) is also now expandable
     with its own `"Compute Dispatch"` child.

   **Note (added by this document's own double-check pass):** the fully
   expanded tree is now noticeably deeper than before this campaign (every
   pass leaf gained its own child row), and the event-tree pane is a
   scrollable child window that may not show every row at once inside one
   `GET /get_swapchain` screenshot at the window's default size. If any
   bullet above cannot be confirmed from a single screenshot, take
   additional screenshots after scrolling the tree pane (or after resizing/
   maximizing the Frame Debugger window) rather than assuming a partial
   screenshot proves a negative.

   **Note — there is no HTTP route that dumps the whole event tree with its
   `eventIndex` values as data** (`GET /frame_debugger/state` only reports
   `enabled`/`hasCapturedFrame`/`selectedEventIndex`/`totalEventCount`/etc.,
   never the tree shape). To find the exact `eventIndex` to pass into
   `select_event` for `"DrawSkyBackground"` and its new `"Draw Quad"` child
   in steps 7/8 below, either (a) reason it out from the step 6 screenshot's
   own row order using the same real chronological-execution-order rule
   PHASE0/PHASE3's own worked examples use, or (b) more reliably, iteratively
   call `select_event?index=N` for increasing `N` starting at 0, taking a
   `GET /get_swapchain` screenshot after each and reading the Inspector's own
   `Pass = ...` field, until both rows of interest are found — the same
   trial-and-error technique `render-pass-1`'s own PHASE7 used to arrive at
   its own known-good `index=5`/`index=8` values.
7. `GET /frame_debugger/select_event?index=<the "DrawSkyBackground" pass
   row's own index>` + `GET /get_swapchain` — confirm the Inspector still
   shows `Pass = DrawSkyBackground`, `ZTest = Equal`, `ZWrite = Off` (same
   real facts as before this campaign — PHASE2 never changed the
   pass-level node's own `details`).
8. `GET /frame_debugger/select_event?index=<the new "Draw Quad" child
   row's own index>` + `GET /get_swapchain` — confirm the Inspector ALSO
   shows correct, matching details for this new child row (proving Locked
   Design Decision #2's dual-selectability end-to-end, live, not just in a
   unit test).
9. Stop the engine (`stop_app_background`).

### 3.4 Campaign completion report

Write `task_manager/render-pass-2/CAMPAIGN_COMPLETION_REPORT.md`, mirroring
`render-pass-1`'s own `CAMPAIGN_COMPLETION_REPORT.md` shape: why this
campaign existed (quote the user's own original complaint + the confirmed
root cause from `PHASE0_MASTER_STRATEGY.md`), one-line-per-phase summary,
the final shipped tree shape (screenshot-confirmed), explicit breaking
changes (every test assertion PHASE3 rewrote, the new `eventLabel`/
`totalEventCount` semantics), what was explicitly NOT done (a real Blit
pass; a real `RenderTransparent` pass), and the final build/test/live-
verification numbers.

### 3.5 Update `README.md`'s "Status" section

**Gap found by this document's own double-check pass:** every sibling
campaign in this repo (`frame-debugger-5` through `frame-debugger-9`,
`atmosphere-scattering-2` through `-4`, `network-impl-6`/`-7`, etc.) adds a
new bullet to the top of the root `README.md`'s "## Status" section at the
close of its own final phase, briefly describing what shipped and how it was
verified (see any existing bullet there for the expected tone/length) —
`render-pass-1` itself never did this (a real, pre-existing gap in that
already-shipped, not-to-be-revisited campaign; do NOT go back and edit
`render-pass-1`'s own files to retroactively add one — that is out of this
campaign's scope). Add ONE new bullet at the top of the list for
`render-pass-2` following the same convention (link the phase folder, name
the confirmed bug/root cause, name the fix, and name the exact verification
performed — full clean build, full `ctest` regression, live HTTP-driven
screenshot verification). If, and only if, it is truly effortless to also
add the one missing `render-pass-1` bullet immediately below/above this new
one while already editing this section, doing so is a welcome bonus — but it
is not required, and must never block or delay this phase's own Definition
of Done if it turns out to be non-trivial (e.g. requires re-deriving facts
this document does not already have on hand).

### 3.6 Commit

`git add` + `git commit` the completion report, the `README.md` "Status"
update, and any small integration fixes made directly in this phase,
together.

## Definition of Done

- [ ] `cmake --build build` succeeds, zero errors.
- [ ] `ctest -C Debug --output-on-failure` reports 100% passing (accounting
      for the one pre-existing, expected, environment-gated skip), total
      test count higher than `render-pass-1`'s own 1578 baseline.
- [ ] Live verification screenshots confirm: `"DrawSkyBackground"` is now
      expandable with a `"Draw Quad"` child; every individual `"Compute
      LUT"` sub-pass is now expandable with a `"Compute Dispatch"` child;
      `"RenderOpaque"` unchanged; both a pass-level row and its new child
      row are independently selectable with correct, matching Inspector
      data.
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` written and committed.
- [ ] A new bullet describing this campaign added to the top of the root
      `README.md`'s "## Status" section, matching every sibling campaign's
      own established convention.
- [ ] `git status` on `feature/render-pass-impl` is clean after this
      phase's own commit.

## What We Will NOT Do

- We will NOT merge `feature/render-pass-impl` into any other branch — that
  is outside this campaign's own authority entirely.
- We will NOT use this phase to introduce any NEW feature/design decision
  not already covered by PHASE0-PHASE3 — if the full build/regression/live
  verification surfaces something genuinely new and non-trivial, stop and
  either `ask_questions` directly or `delegate_task` a properly-scoped
  follow-up (with the mandatory `ask_questions` instruction propagated),
  rather than improvising it inline.
