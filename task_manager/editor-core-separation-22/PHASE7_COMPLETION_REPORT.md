# PHASE7 COMPLETION REPORT — Full regression, documentation, and final end-to-end proof

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (unchanged, as required)
Status: **Complete.** Full clean build succeeded (611/611 steps, zero errors),
full `ctest -C Debug --output-on-failure` regression pass succeeded (2036
tests, 100% of executed tests passing, 8 legitimate environment-gated skips —
up from `editor-core-separation-21`'s own 2004 baseline, a clean **+32** from
this campaign's own new tests across PHASE1/PHASE3/PHASE4/PHASE5/PHASE6), and
a final, live, HTTP-driven, end-to-end verification reproduced all three
original screenshot scenarios one more time against the fully rebuilt binary,
each now proven fixed. **No test failure occurred in Step 3.2 — the full suite
passed clean on the first run**, so no PHASEn-attribution fix/append was
needed this phase (the "diagnose and fix it yourself" contingency in this
phase's own Step 3.2 was never triggered).

## Pre-checks (required reading, done first)

`PHASE0_MASTER_STRATEGY.md` re-read in full, then `PHASE6_COMPLETION_REPORT.md`
plus every `PHASE1..PHASE5_COMPLETION_REPORT.md` in this same folder, per this
phase's own instruction — the full picture of what changed across the whole
campaign (three confirmed root causes, six fixes/detectors, one systemic
audit) was needed before this phase's own final verification could
meaningfully reproduce every original screenshot scenario. `editor-core-separation-21/CAMPAIGN_COMPLETION_REPORT.md`
was also re-confirmed as the correct baseline (2004 tests) this phase's own
Step 1 item 2 requires beating.

## Step 3.1 — Full clean build

```
cmake --build build --target clean   -> 628 files removed
cmake --build build                  -> 611/611 steps succeeded, zero errors
```

## Step 3.2 — Full regression suite

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
-> 100% tests passed out of 2036
-> Total Test time (real) = 183.37 sec
-> 8 legitimate environment-gated skips (same shape/count as every prior campaign's own final run —
   OpenProjectEndpointEndToEndTest's real-compiled-probe test, PmxLoaderRealModelSmokeTest,
   3 ProjectAssemblyHostTest/ProjectAssemblyRegistrationLedgerTest cases, CoreHeadlessConstructionTest —
   none new, none attributable to this campaign)
```

**2036 total, strictly greater than `editor-core-separation-21`'s own 2004
baseline** — the requirement in this phase's own Step 1 item 2 and PHASE0's
own Definition of Done item 6. **Zero test failures occurred** — the entire
suite passed clean on the very first run, so this phase's own Step 3.2
contingency plan (diagnose which phase's change is implicated, fix it
directly, append a "Post-hoc correction found during PHASE7" note to that
phase's own completion report, re-run the full suite to confirm) was never
needed. No `delegate_task` call was made or would have been needed regardless
(forbidden for this phase per PHASE0 Locked Decision #6, restated in this
phase's own task instructions).

## Step 3.3 — Final, live, HTTP-driven, end-to-end verification of all three original scenarios

Performed on a real running `GreatTamanaEditor.exe` (built from the just-completed
full clean build), driven entirely via `run_app_background`/`gte_send_request`/
`stop_app_background`.

### Scenario 1 — duplicate `RenderOpaque`/`DrawSkyBackground` rows

1. `GET /activate_tab?name=Render%20Graph` → `success:true`.
2. Confirmed, via a PowerShell regex count against the raw `GET /render_graph`
   JSON body, that the underlying snapshot genuinely still carries 2 raw
   instances each for `"RenderOpaque"`/`"DrawSkyBackground"` (Game View +
   Scene View, both panels visible by default) — this raw cardinality is
   correct, permanent, and by design (PHASE0's own Step 2.1); the bug this
   campaign fixed was never about the raw snapshot, only the UI's own
   presentation of it.
3. `GET /get_swapchain` screenshot confirmed the "Render Graph" panel's own
   "Offscreen Regime (Game View + Scene View)" table renders exactly one row
   per unique pass name (`▸ AtmosphereAerialPerspective...` groups with an
   expand arrow for multi-instance passes, plain leaf rows for single-instance
   ones), and the "Disabled Built-In Passes" section read "Every known
   built-in pass is currently enabled" at baseline.
4. **The fix's own proof**: `GET /render_graph/set_pass_enabled?name=RenderOpaque&enabled=false`
   then a fresh `GET /get_swapchain` screenshot — "Disabled Built-In Passes"
   now shows **exactly ONE** `"RenderOpaque"` entry, matching the live pass
   table's own grouped, one-row-per-name cardinality one-to-one (no
   "2 rows live, 1 row disabled" contradiction anywhere). Re-enabled
   (`enabled=true`) afterward, confirmed `success:true`.
5. `GET /get_logs?category=RenderPassHonesty` / `?category=FrameDebuggerCoverage`
   / `?category=FrameDebuggerSideChannel` all stayed `{"count":0}` throughout.

### Scenario 2 — `DemoRenderFeaturePlugin_Clear` invisible in Frame Debugger

1. `GET /render_graph/passes` confirmed `DemoRenderFeaturePlugin_Clear`
   `enabled:true` this session.
2. `GET /frame_debugger/open` → `.../enable?value=true` → `.../capture` →
   baseline `totalEventCount:79`.
3. `GET /get_swapchain` screenshot of the Frame Debugger window confirmed a
   real, populated tree (`Game View > Compute LUT > ...`, `RenderOpaque`,
   `Compute Dispatches (Post-GameView) > ...`) — the pass itself sits further
   down under `"Other Render Passes"`, past the visible fold, so its own
   presence was confirmed the more rigorous way below rather than relying on
   a screenshot crop.
4. **The fix's own proof**: `GET /render_graph/set_pass_enabled?name=DemoRenderFeaturePlugin_Clear&enabled=false`
   then re-`/capture` → `totalEventCount` dropped to **75** — a clean,
   exactly-explainable **-4** (declared twice per frame, Game View + Scene
   View, each instance's own parent+child leaf pair = 2 indices each,
   disabling the shared name removes both instances' leaves = 4 indices
   total) — byte-for-byte matching PHASE4's own original live-verification
   result. Re-enabled → back to **79** exactly.
5. `GET /get_logs?category=FrameDebuggerCoverage` stayed `{"count":0}` — the
   Clause B detector confirms zero regression of the exact bug it exists to
   catch.

### Scenario 3 — `DrawSkyBackground` toggle-off, sky still shows

1. `GET /get_texture?texture_name=GameView` baseline = 17109 bytes, real sky
   gradient (blue-to-orange horizon over dark ground).
2. `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false` →
   `GET /frame_debugger/open` → `.../enable?value=true` (per this phase's own
   corrected Step 3.3 wording — there is no separate HTTP "pause"/"step"
   endpoint; this single call both engages Pause and arms the deferred replay
   capture) → `.../capture` → `totalEventCount` dropped by a clean **-2**
   (79 → 77, `"DrawSkyBackground"`'s own single GameView-only leaf gone).
3. **The fix's own proof**: `GET /get_texture?texture_name=GameView` = **1582
   bytes** — a flat, dark image with zero sky gradient. `GET
   /get_texture?texture_name=FrameDebuggerReplayStep0` (the corrected query
   parameter, `texture_name` not `name`, per this phase's own corrected
   guidance) returned **HTTP 504** on a fresh, never-captured-with-sky-enabled
   session — "was never registered (or never rendered again) within the
   timeout" — the CORRECT fixed behavior: with `recordSkyBackground` now
   empty (never published, per PHASE1's fix) and this demo scene's
   `objectCount == 0`, `AddReplayPasses()`'s own `totalStepCount` is `0`, so
   it declares no replay step at all — zero sky pixels reach the replay
   preview either, exactly like the live Game View.
4. Re-enabled `"DrawSkyBackground"`, confirmed `GET /get_texture?texture_name=GameView`
   restored to the byte-identical 17109-byte baseline — a positive control
   proving the mechanism itself still works correctly when honestly enabled.
5. `GET /get_logs?category=FrameDebuggerSideChannel` / `?category=RenderPassHonesty`
   / `?category=FrameDebuggerCoverage` / `?min_level=Error` all stayed
   `{"count":0}` throughout every correct-behavior check.

### Step 3.3 item 4 — deliberately reintroduce ONE original bug, confirm the detector fires, then fully revert

Per this phase's own instruction, the cheapest reproduction was chosen: PHASE1's
own single early-`return;` guard inside `Core.cpp`'s `"DrawSkyBackground"`
provider (line 874) was temporarily replaced with a comment
(`/* TEMPORARY PHASE7 regression-proof: return; commented out */`),
reintroducing PHASE1's own original, confirmed Root Cause #3 bug exactly.

1. Incremental build succeeded. Launched a real `GreatTamanaEditor.exe`
   (PID 7332).
2. `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false` →
   `GET /frame_debugger/open` → `.../enable?value=true` → `.../capture`
   (`totalEventCount:77`, `hasCapturedFrame:true`).
3. `GET /get_logs?category=FrameDebuggerSideChannel` → **exactly 1 fresh
   `GTE_LOG_ERROR`**: *"RenderPassBlackboard key
   'Atmosphere.GameSkyBackgroundCallback' was published this frame even
   though its own gating pass is DISABLED - a disabled pass's own side effect
   is still visible to whichever other pass reads this key (Clause C
   violation)."* — the exact, reported Root Cause #3 bug, reproduced live, on
   demand, by this permanent detector, against the fully-clean-built binary.
   `GET /get_logs?min_level=Error` showed exactly this 1 entry — zero crash,
   zero unrelated error.
4. Stopped the app, reverted the one line back to `return;`, rebuilt
   (succeeded), and confirmed via `git_status` that the working tree read
   **"nothing to commit, working tree clean"** — a byte-for-byte-identical
   revert, zero net diff, satisfying this step's own explicit
   `git diff --stat`-equivalent requirement.
5. Relaunched a fresh instance afterward and re-confirmed the correct,
   fixed behavior one final time (zero sky in `GameView`, `504` on a
   never-registered `FrameDebuggerReplayStep0`, all four log categories
   `{"count":0}`) — the campaign's own real, permanent, shipped state, not
   the deliberately-broken one, is what actually builds and runs today.

### A live-testing anomaly encountered (not a regression, not this phase's own fix)

During this phase's own live-testing session, the running `GreatTamanaEditor.exe`
process crashed twice, unprompted, each time shortly after a `GET
/get_texture?texture_name=FrameDebuggerReplayStepN` call that resolved to an
HTTP `504` timeout (the "never registered/rendered" branch). This is the
SAME class of anomaly PHASE1's own completion report already documented
("Anomaly encountered — one engine crash mid-session, not attributable to
this phase's own code change") — reproduced here independently, on a
completely different day/build, seemingly correlated with the same
504-timeout code path rather than with anything this campaign's own PHASE1-6
changed (the crash reproduced identically both with the real, fixed
`DrawSkyBackground` guard in place AND with PHASE1's own guard deliberately,
temporarily reverted — ruling out this campaign's own fix as the cause).
Both times, a fresh `run_app_background` relaunch immediately restored full,
correct functionality with zero data loss (the in-process `Logger` does not
persist across a process crash, as already documented). This is flagged here
plainly, exactly matching PHASE1's own precedent of recording a live-testing
anomaly honestly rather than silently working around it — it is NOT reported
via `bug_report` (that tool is reserved for a malfunctioning MCP TOOL, not an
engine-side crash; every MCP tool used here — `run_app_background`,
`gte_send_request`, `stop_app_background` — behaved correctly throughout,
accurately reporting the real state of the OS process/HTTP server at every
call) and it is NOT something this phase's own scope (full regression + docs
+ final verification) is chartered to root-cause or fix — a genuinely new,
separate investigation, out of scope here, same as PHASE1 concluded for its
own occurrence of this same anomaly.

## Step 3.4 — Documentation

1. **`AGENTS.md`** — a new paragraph added to the existing "Render Pass
   System" section (immediately following `editor-core-separation-21`'s own
   paragraph, before the "Full history:" list), describing this campaign's
   own final, shipped state: all three confirmed root causes and their exact
   fixes (the `RenderPassToggleGuard.h` pattern and where it must be applied;
   the corrected `RenderPassCategory::Debug` vs `FrameDebuggerInternal`
   distinction and the "Other Render Passes" fallback bucket; the grouped
   pass-row UI), both new PHASE6 detectors with their own log category names
   (`FrameDebuggerCoverage`/`FrameDebuggerSideChannel`), and the final
   build/test numbers. The "Full history:" list was also extended with
   `task_manager/editor-core-separation-22/PHASE0_MASTER_STRATEGY.md`,
   matching every prior campaign's own precedent in that same list.
2. **`docs/conventions/render-pass-side-channel-honesty.md`** (new file) —
   the "gate every side effect, not just the `RenderPassDesc`" rule with the
   `"DrawSkyBackground"` worked example, the `RenderPassCategory::Debug`/
   `FrameDebuggerInternal` distinction and when to use which, the "Other
   Render Passes" fallback bucket's structural guarantee, both new
   detectors' own design and log categories, the checklist for a future
   provider author, and an honest restatement of Clause C's own
   curated-allowlist limitation — deliberately additive, narrower, and
   layered on top of `docs/conventions/render-pass-toggle-honesty.md`
   (never merged into or overwriting that file).
3. **`docs/README.md`** — a new Conventions index bullet added immediately
   after the existing "Render Pass Toggle Honesty" entry, pointing at the new
   file, matching `editor-core-separation-21`'s own precedent exactly.

## Step 3.5 — `CAMPAIGN_COMPLETION_REPORT.md`

Written as a separate, top-level, whole-campaign summary file in this same
folder — see `CAMPAIGN_COMPLETION_REPORT.md`, mirroring
`editor-core-separation-21`'s own precedent of keeping the two files
distinct (this file describes only THIS phase's own work; that one describes
the whole seven-phase campaign end to end).

## No delegation, no unresolved ambiguity

No `delegate_task` call was made (not permitted for this phase, per PHASE0
Locked Decision #6, restated explicitly in this phase's own task
instructions — this is the one phase where a test failure in Step 3.2 would
otherwise have been the most tempting reason to delegate, and the rule was
followed regardless; as it happened, no test failure occurred at all). No
`ask_questions` call was needed — every design decision this phase faced
(which bug to deliberately reintroduce for Step 3.3 item 4; how to phrase the
new `AGENTS.md`/`docs/conventions/` content) was already resolved either by
this phase's own explicit instructions or by direct precedent from
`editor-core-separation-21`'s own PHASE6/CAMPAIGN report shape.

## Files changed

- `src/Core/Core.cpp` (Step 3.3 item 4's own temporary
  reintroduce-then-revert experiment — confirmed via `git_status` to leave
  **zero** net diff; not a real, permanent change)
- `AGENTS.md` (new "Render Pass System" paragraph + "Full history" list
  update)
- `docs/README.md` (new Conventions index bullet)
- `docs/conventions/render-pass-side-channel-honesty.md` (new)
- `task_manager/editor-core-separation-22/PHASE7_COMPLETION_REPORT.md` (this
  file)
- `task_manager/editor-core-separation-22/CAMPAIGN_COMPLETION_REPORT.md`
  (new — the whole-campaign summary)

## End of phase checklist (Step 3.6, and of the whole campaign)

1. ✅ Full clean build: 611/611 steps, zero errors (3.1).
2. ✅ Full `ctest`: 100% of executed tests passing (2036/2036 executed, 8
   legitimate skips), strictly greater than the 2004 baseline (3.2).
3. ✅ `CAMPAIGN_COMPLETION_REPORT.md` written (3.5), alongside this phase's
   own `PHASE7_COMPLETION_REPORT.md` as two distinct files, matching
   `editor-core-separation-21`'s own precedent.
4. Next: `git_add` + `git_commit` covering every doc file changed and both
   reports.
