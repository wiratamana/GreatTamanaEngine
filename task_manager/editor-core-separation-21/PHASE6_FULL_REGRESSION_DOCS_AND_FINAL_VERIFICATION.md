# PHASE6 — Full Regression, Documentation, and Final Live Verification

Parent: `PHASE0_MASTER_STRATEGY.md` (MUST READ FIRST).
Previous phase report: `PHASE5_COMPLETION_REPORT.md` (MUST READ FIRST).

This is the LAST phase of the campaign. This is the ONE phase explicitly
allowed — expected — to run a full clean build and a full `ctest` regression
pass (see `PHASE0_MASTER_STRATEGY.md` Step 3.1 rule 2: every other phase is
incremental-only).

---

## Step 1: The Goal (Where are we going?)

Close out the campaign with the same rigor every prior `render-pass-*`/
`editor-core-separation-*` campaign in this codebase's own history closed
out with (see every `CAMPAIGN_COMPLETION_REPORT.md` cited throughout
`AGENTS.md` for the expected bar): a full clean build, a full regression test
pass with the test count only ever growing versus the pre-campaign baseline,
permanent documentation matching this codebase's established tone/precision,
and a final, live, end-to-end, HTTP-driven proof that:
1. The originally reported lie is gone, for good.
2. Every other Confirmed-Lie finding from PHASE3 is gone, for good.
3. The Iron Rule detector from PHASE5 is live, wired in, and silent under
   normal operation (zero false positives against the final, fully-fixed
   codebase).

## Step 2: The Situation (Where are we now?)

By this point, PHASE1-5 have: diagnosed and fixed the reported bug, audited
and fixed every other discovered lie, and built a permanent detector. What
remains is proving all of it holds together as ONE coherent whole, not five
separate patches that happen to each pass in isolation, and writing it down
so no future agent has to rediscover any of this from scratch (exactly the
role `AGENTS.md` already plays for every prior campaign).

## Step 3: The Plan

### 3.1 — Full clean build

1. Establish the pre-campaign baseline test count/skip count first: check
   `PHASE1_COMPLETION_REPORT.md` (or, if not recorded there, re-derive it
   from `AGENTS.md`'s own most recent campaign entry — as of this writing
   `editor-core-separation-20`'s own entry cites "1993 tests, 8 legitimate
   environment-gated skips") so this phase's own final numbers can be
   reported as a genuine delta, matching every prior campaign's own reporting
   convention exactly.
2. Full clean build:
   ```
   cmake --build build
   ```
   (Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.)
   If a genuinely clean/from-scratch build is warranted (e.g. suspicion of
   stale incremental state after five phases of edits), reconfigure first:
   ```
   cmake -S . -B build
   ```
   then build again — use judgment; a stale-but-correct incremental build is
   NOT grounds to force a multi-minute full reconfigure+rebuild if
   `cmake --build build` alone already succeeds cleanly.
3. If the build fails: diagnose and fix directly in this phase (this is
   allowed and expected — do not `delegate_task` a build failure back out;
   only delegate if the fix requires touching a phase's own already-closed,
   substantially different subsystem in a way that itself needs its own
   fresh `ask_questions` design decision).

### 3.2 — Full regression test pass

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
1. Every test must pass (100% of executed tests), matching every prior
   campaign's own bar. Any newly-failing test is a REAL regression from this
   campaign's own changes — diagnose it fully (which phase's change broke
   it, why) and fix it directly; never loosen a test's expectation to make
   it pass without first understanding why it failed (per `AGENTS.md`'s own
   "Testability & Regression Safety" rule).
2. Confirm the test count is >= the pre-campaign baseline (Step 3.1.1) plus
   at least the new tests PHASE2/PHASE4/PHASE5 added.

### 3.3 — Final live, end-to-end, HTTP-driven verification

Launch `build\GreatTamanaEditor.exe` via `run_app_background` and, purely
through `gte_send_request` (no mouse/manual UI interaction), reproduce the
FULL scenario from the original bug report end-to-end, one last time, as the
campaign's own closing proof:
1. `GET /frame_debugger/open`, `/enable?value=true`, `/capture`,
   `/state` — baseline, everything enabled, confirm
   `AtmosphereAerialPerspectiveCompositePass` present and correctly populated
   (a real, executing pass is NOT itself a bug — only a disabled-yet-executing
   one is).
2. `GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false`
   -> fresh `/capture` -> `/state`: confirm ABSENCE, confirm
   `GET /get_game_view` still renders a sane image.
3. Re-enable it, confirm restoration, zero crash, zero warning/error in
   `GET /get_logs` for this specific sequence (aside from anything already
   expected/benign).
4. Repeat steps 2-3 for `DemoRenderFeaturePlugin_Clear`/
   `DemoRenderFeatureSecondPlugin_Clear` and every other Confirmed-Lie
   finding PHASE4 fixed.
5. Pull `GET /get_logs?category=RenderPassHonesty` one final time and confirm
   it is EMPTY (zero entries) across this entire verification sequence —
   the detector staying silent under fully-correct behavior is just as
   important a proof point as it firing under the deliberately-broken PHASE5
   throwaway test.
6. Capture at least one `gte_send_request` `GET /get_game_view` screenshot-
   equivalent image at the very end (everything re-enabled) and confirm, by
   eye, it looks visually consistent with the pre-campaign baseline
   rendering (no regression in the actual rendered picture, exactly matching
   every prior campaign's own "byte-identical/visually unchanged final
   image" closing bar).
7. `stop_app_background` the running instance.

### 3.4 — Documentation

1. **`AGENTS.md`** — add a new section (or extend the existing "Render Pass
   System" section, whichever reads more naturally given where its own
   narrative currently ends) documenting this campaign, `editor-core-
   separation-21`, in the EXACT SAME tone/precision/level of detail as every
   existing entry: what was reported, what the confirmed root cause actually
   was (do not let this be vague — name the exact mechanism PHASE1 found),
   what PHASE3's audit found beyond the one reported bug, what PHASE4 fixed,
   and what PHASE5's permanent detector does and where it lives. Follow this
   codebase's own established convention of being BRUTALLY HONEST about any
   remaining caveat/non-goal (mirroring `editor-core-separation-9`'s/`-20`'s
   own "Honest, load-bearing caveat, restated plainly" sections) — if PHASE3
   found something explicitly deferred by user decision, say so plainly here
   too, do not let it quietly disappear.
2. **New file, `docs/conventions/render-pass-toggle-honesty.md`** (mirroring
   every other `docs/conventions/*.md` file's own existing shape — check
   `docs/README.md`'s own index and add this new file to it too) — the
   permanent, standalone reference for: how `RenderPassToggleRegistry` is
   meant to be consulted by any FUTURE pass that bypasses the generic
   `RenderPipeline::DeclareOnePhase()` flush-loop path (i.e. any provider
   calling `AddRenderPass()` directly inside its own lambda, exactly the
   pattern that caused every lie this campaign fixed), and how the PHASE5
   detector works and what a `GTE_LOG_ERROR("RenderPassHonesty", ...)` entry
   in `GET /get_logs` means and how to fix it if it ever fires again.
3. Do not include any changelog-style "this used to say X, now it says Y"
   commentary anywhere in the new/updated docs — per this project's own
   established convention (every `AGENTS.md` entry is written as a
   standalone, present-tense description of current, final behavior, not a
   diff narrative).

### 3.5 — Wrap-up

1. Write `PHASE6_COMPLETION_REPORT.md` containing: the full clean build
   result, the full `ctest` result with exact before/after test counts, the
   complete Step 3.3 live verification transcript, and links to the new/
   updated docs.
2. Write `CAMPAIGN_COMPLETION_REPORT.md` (in this same folder, sibling to
   the phase reports — matching every prior campaign's own top-level
   closeout file) summarizing the ENTIRE campaign end to end: the original
   report, the confirmed root cause, every fix, the permanent detector, the
   final test/verification numbers, and any explicitly-deferred item.
3. `git_add` + `git_commit` covering the docs, both new report files, and
   anything else outstanding.
4. Do NOT call `delegate_task`. If, even at this final stage, a genuine
   design ambiguity remains unresolved, use `ask_questions` before writing
   the campaign closeout report, since that report is meant to be the
   definitive, final record of what shipped.
