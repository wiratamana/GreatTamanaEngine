# PHASE3: Final Integration, Full Build, and Live Verification

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first, along with
`PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md`'s and
`PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md`'s own completion
reports (read the reports, not just the strategy files — they carry
whatever actually happened, including any deviation from the plan). Part
of the `render-pass-4` campaign._

## Step 1: The Goal

Prove, with a full build and full regression pass (the ONLY phase in this
campaign allowed to run either), that PHASE1's detector and PHASE2's real
ordering fix are both correctly integrated and that the real, live engine
still renders the Game View and Scene View exactly as before — atmosphere,
opaque geometry, sky background, and (once shipped) transparency all still
composite correctly, and neither view is a solid white/black frame (the
exact failure mode this whole campaign traces back to). Close out the
campaign with a completion report and doc updates so a future reader can
find the full history in one place.

## Step 2: The Situation

- Every code change for this campaign is already complete by the time
  this phase starts — PHASE1 and PHASE2 both already ended with their own
  incremental compile + targeted smoke check + git commit. This phase is
  the integration/verification capstone, not a place to introduce new
  design decisions.
- `AGENTS.md`'s own Testability/Regression-Safety section (see repo root)
  is explicit: "Run the actual test suite before considering any change
  to `gte_core` done — a successful build is not enough." This phase is
  where that rule is finally honored for the whole campaign at once.
- Regression test command:
  `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`
- Full build command: `cmake --build build` (working directory:
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`).
- Live verification tools: `run_app_background` to launch the real engine
  executable non-blocking, `gte_send_request` against
  `/get_game_view`/`/get_swapchain` (and `/frame_debugger/...` endpoints,
  per `AGENTS.md`'s "Networking"/"Frame Debugger" sections, if a deeper
  look at pass execution order is useful) to visually confirm rendering,
  then `stop_app_background` to close it down again afterward — do not
  leave the process running at the end of this phase.

## Step 3: The Plan

### 3.1 — Full build

Run the full `cmake --build build` command above. If it fails, diagnose
the failure directly (read the compiler error, find the exact file/line)
and fix it with a real code edit — do not paper over a real compile error
by weakening PHASE1/PHASE2's own logic (e.g. do not delete the `assert()`
just to make a build "succeed" if it's actually firing on a genuine,
real contradiction somewhere in the engine that PHASE2's audit missed;
if that happens, treat it as new, real information — fix the actual
mistagged pass, or use `ask_questions` if the right fix isn't obvious).

### 3.2 — Full regression suite

Run `ctest -C Debug --output-on-failure` from the `build` directory.
Every test must pass, including every new test PHASE1/PHASE2 added. If
anything newly fails: diagnose it fully before touching anything — read
the actual failure output, trace it back to the specific PHASE1/PHASE2
change that caused it, and fix the root cause with a real code edit. Per
PHASE0's cross-cutting rules, if the needed fix is large/unclear enough to
warrant its own focused investigation, use `delegate_task` (with the
mandatory `ask_questions`-propagation instruction) to spin up a dedicated
fix task rather than guessing broadly inside this phase.

### 3.3 — Live visual verification

Launch the real engine via `run_app_background`, then:

- `gte_send_request` against `/get_game_view` and `/get_swapchain` —
  confirm both show a normal, fully-composited frame (sky + any visible
  opaque geometry + atmosphere aerial-perspective tinting), NOT a solid
  white or black frame (the exact symptom the original, already-fixed
  `AtmosphereComposite` bug produced live).
- If the Editor is enabled in this build, also check the Scene View the
  same way, and optionally open the Frame Debugger
  (`GET /frame_debugger/open`, then `/frame_debugger/enable` +
  `/frame_debugger/capture`, per `AGENTS.md`'s "Frame Debugger" section)
  to visually confirm the captured pass tree's execution order still
  looks sane (Atmosphere LUTs, then Opaque, then Sky, then Transparent,
  then Composite/Present, in that visual order) — this is a genuinely
  useful, concrete way to eyeball PHASE2's own reordering having no
  unintended side effect on the real, shipping pass sequence.
- `stop_app_background` the process once done — do not leave it running.

### 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Write `task_manager/render-pass-4/CAMPAIGN_COMPLETION_REPORT.md`,
mirroring the shape of `render-pass-3`'s own
`CAMPAIGN_COMPLETION_REPORT.md` (read it once for the level of detail
expected): what shipped in PHASE1 (the detector, exactly what it catches
and does not catch), what shipped in PHASE2 (the one real behavior
change — restate it precisely: passes are now processed in
RenderPassEvent-then-declaration-order for dependency resolution and
tie-breaking, not raw declaration order), the full build + `ctest` result,
the live verification result, and an explicit "what remains true and
false about `RenderPassEvent` now" summary for a future reader who only
has time to read this one file (a direct callback to this whole
campaign's own origin: `RenderPassEvent` LOOKED like it decided order but
didn't — state plainly, in one place, exactly what is and isn't true about
it now that both phases have landed).

### 3.5 — `AGENTS.md` update

If PHASE2's own Step 3.5 did not already do this (check its completion
report first), add the short new paragraph to `AGENTS.md`'s "Render Pass
System" section describing the `render-pass-4` campaign's existence and
its one real behavior change, in the same "LOUD, DELIBERATE" callout style
already used there for `render-pass-3`'s own Locked Design Decision 5.
Link to `task_manager/render-pass-4/PHASE0_MASTER_STRATEGY.md` and the new
`CAMPAIGN_COMPLETION_REPORT.md`, exactly mirroring the existing "Full
history: ..." line pattern already used for the three prior campaigns.

### 3.6 — Final git commit

Commit the full-build/regression-pass evidence (the completion report +
any `AGENTS.md` edit) together, once everything above is green.

## Definition of Done

- `cmake --build build` succeeds with zero errors, zero new warnings
  introduced by this campaign's own changes.
- `ctest -C Debug --output-on-failure` passes in full — every test in the
  suite, not just the ones this campaign added.
- Live `/get_game_view`/`/get_swapchain` (and Scene View, if applicable)
  both show a correct, fully-composited frame — confirmed by actually
  looking at the returned image, not just assuming success from an HTTP
  200.
- `task_manager/render-pass-4/CAMPAIGN_COMPLETION_REPORT.md` exists and
  covers PHASE1, PHASE2, and this phase's own build/test/live-verification
  results.
- `AGENTS.md`'s "Render Pass System" section documents this campaign.
- Everything is committed to git on `feature/render-pass-impl`.

## What We Will NOT Do

- Do NOT introduce any new design decision in this phase — if the full
  build/regression pass surfaces something PHASE1/PHASE2 didn't
  anticipate, fix it at its actual root (in the file/phase it belongs to
  conceptually) rather than patching around it here, and say so plainly
  in the completion report.
- Do NOT leave the live-verification engine process running in the
  background when this phase ends.
- Do NOT skip the full build/`ctest` run "because the incremental checks
  in PHASE1/PHASE2 already passed" — this phase exists specifically
  because an incremental check is not equivalent to the full suite, per
  `AGENTS.md`'s own explicit rule.
