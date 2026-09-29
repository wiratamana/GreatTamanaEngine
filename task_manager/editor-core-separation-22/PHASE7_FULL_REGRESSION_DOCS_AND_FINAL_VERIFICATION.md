# PHASE7 — Full regression, documentation, and final end-to-end proof

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: `PHASE6_COMPLETION_REPORT.md`, and, for
full context, skim every `PHASE1..PHASE6_COMPLETION_REPORT.md` in this same
folder — this phase's own final verification must reproduce (and disprove)
every one of the three original screenshot scenarios, so it needs the full
picture of what changed across the whole campaign.

## Step 1: The Goal (Where are we going?)

1. A full clean build succeeds with zero errors.
2. A full `ctest -C Debug --output-on-failure` regression pass succeeds,
   with the total executed/passing test count STRICTLY GREATER than
   `editor-core-separation-21`'s own final baseline (2004 tests, per that
   campaign's own `CAMPAIGN_COMPLETION_REPORT.md`) — never equal, never
   fewer, since PHASE1/PHASE4/PHASE5/PHASE6 each add real new tests.
3. `AGENTS.md` and `docs/conventions/` describe this campaign's own final,
   shipped behavior, in the same tone/precision/format as every prior
   campaign's own entry.
4. A final, live, HTTP-driven, end-to-end verification reproduces all
   three of the ORIGINAL user-reported scenarios one more time, this time
   proving each is fixed — not merely re-running each phase's own already-
   passed, narrower check in isolation.

## Step 2: The Situation (Where are we now?)

Every individual phase (PHASE1-6) already ran its own incremental
build/targeted-test/live-HTTP verification. This phase is the ONE place in
the whole campaign where a full, slow, from-scratch build and the ENTIRE
test suite are allowed to run (per PHASE0's Locked Decision #2) — do not
run either of these in any earlier phase, and do not skip them here.

## Step 3: The Plan (detailed strategy)

### 3.1 — Full clean build

`cmake --build build --target clean` followed by `cmake --build build`.
Record the exact step count and zero-errors confirmation in the completion
report, exactly as `editor-core-separation-21`'s own final report did
("603/603 steps succeeded, zero errors").

### 3.2 — Full regression suite

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`.
Record the exact total/passing/skipped counts. **If any test fails**:
diagnose which phase's own change is implicated (re-read that phase's own
completion report first), then fix it YOURSELF, directly, right here in
PHASE7. **Do NOT call `delegate_task`** — PHASE0's own Locked Decision #6
forbids this for every implementation phase, including this one; delegation
is reserved exclusively for the orchestrating/double-checking layer that
queued these seven phases in the first place, never for a phase itself.
Attribute the fix to whichever phase's own scope it actually falls under by
APPENDING a short, clearly-labeled "Post-hoc correction found during PHASE7"
note to that phase's own `PHASEn_COMPLETION_REPORT.md` (append, do not
rewrite it wholesale — PHASE0's own convention is that a report describes
what was actually done, and a later, necessary correction is itself real
information, not noise to hide). Re-run the full suite again after the fix
to confirm it now genuinely passes before moving on.

### 3.3 — Final, live, HTTP-driven, end-to-end verification of all three original scenarios

Using `run_app_background`/`gte_send_request`/`stop_app_background` against
a freshly-built `build\GreatTamanaEditor.exe`:

1. **Original scenario 1 (duplicate RenderOpaque/DrawSkyBackground rows)**:
   with both Game View and Scene View panels visible, capture the "Render
   Graph" panel's own rendered UI and confirm exactly one row per unique
   pass name, annotated by view, with the "Disabled Built-In Passes"
   section showing the SAME set of names once toggled off — matching
   PHASE5's own Step 3.6 checks, re-run one final time against the fully
   rebuilt binary.
2. **Original scenario 2 (`DemoRenderFeaturePlugin_Clear` invisible in
   Frame Debugger)**: enable it, capture, confirm a real leaf now exists —
   matching PHASE4's own Step 3.5 checks, re-run one final time.
3. **Original scenario 3 (`DrawSkyBackground` toggle-off, sky still
   shows)**: reproduce the EXACT screenshot scenario using
   `GET /frame_debugger/enable?value=true` (there is no dedicated HTTP
   "pause"/"step" endpoint anywhere in this engine — that call alone both
   auto-engages Pause and arms the deferred replay-pass capture, per PHASE1's
   own Step 3.4 item 4), then disable `"DrawSkyBackground"` mid-session, and
   confirm zero sky pixels in either the Game View or the captured replay
   preview (`GET /get_texture?texture_name=FrameDebuggerReplayStepNColor` —
   the query parameter is `texture_name`, not `name`) — matching PHASE1's own
   Step 3.4 checks, re-run one final time.
4. For all three: `GET /get_logs?category=RenderPassHonesty` AND
   `GET /get_logs?category=FrameDebuggerCoverage` (PHASE6's own new
   category) both stay completely empty throughout every correct-behavior
   check above, and — as a deliberate, final, one-time proof mirroring
   `editor-core-separation-21`'s own PHASE6 discipline — briefly,
   deliberately reintroduce ONE of the three original bugs (whichever is
   cheapest to toggle back in temporarily, e.g. reverting PHASE1's single
   guard line), confirm the relevant detector fires a fresh log entry, then
   fully revert (confirm via `git diff --stat` showing zero net change)
   before finishing this phase.

### 3.4 — Documentation

1. `AGENTS.md`: add a new section/paragraph (matching the existing
   "Render Pass System" section's own established tone) describing this
   campaign's own final, shipped state: the `RenderPassToggleGuard.h`
   pattern and where it must be applied (any provider with a side effect
   before its own toggle check), the corrected `RenderPassCategory::Debug`
   vs `FrameDebuggerInternal` distinction, the "Other Render Passes"
   fallback bucket's existence and purpose, the grouped pass-row UI, and
   both new PHASE6 detectors (with their own log category names, for
   future debugging sessions to know to check `GET /get_logs`).
2. `docs/conventions/` — add a new file (e.g.
   `docs/conventions/render-pass-side-channel-honesty.md`, alongside the
   existing `render-pass-toggle-honesty.md` from `editor-core-separation-21`
   — do not merge into or overwrite that file; this is a genuinely
   additional, narrower convention layered on top of it) describing: the
   "gate every side effect, not just the RenderPassDesc" rule with a
   worked example, the `RenderPassCategory::Debug`/`FrameDebuggerInternal`
   distinction and when to use which, and a short checklist for anyone
   adding a new `RenderPipeline` provider in the future ("does this
   provider publish to the blackboard, cache a callback, or mutate a
   member variable another system reads? If yes, does an early
   `ShouldDeclareBuiltInPassThisFrame()` guard run BEFORE that, not just
   before `out.push_back(desc)`?").
3. Update `docs/README.md`'s own Conventions index with a new bullet
   pointing at the new file, matching `editor-core-separation-21`'s own
   precedent exactly.

### 3.5 — Final campaign completion report

Write `CAMPAIGN_COMPLETION_REPORT.md` in this same folder (NOT a
`PHASEn_` file — this is the whole-campaign summary, mirroring
`editor-core-separation-21`'s own top-level report exactly): the three
original bugs, their confirmed root causes (reusing PHASE0's own precise
language), every fix, the final build/test numbers, the final live-
verification proof, files changed across the whole campaign, and an
honest "permanent limitations" section if any genuinely remain (mirroring
`editor-core-separation-21`'s own precedent of stating limitations plainly
rather than smoothing them over) — for example, if PHASE6's Clause C
detector's own curated-allowlist scoping (Step 3.3 of PHASE6) means some
theoretically-possible future side-channel shape remains undetected until
a human adds it to the list, SAY SO explicitly here, exactly as
`editor-core-separation-21`'s own report did for its own two similar,
honestly-stated limitations.

### 3.6 — End of phase (and of the whole campaign)

1. Full clean build: zero errors (3.1).
2. Full `ctest`: 100% of executed tests passing, count strictly greater
   than the pre-campaign baseline (3.2).
3. `CAMPAIGN_COMPLETION_REPORT.md` written (3.5), alongside this phase's
   own `PHASE7_COMPLETION_REPORT.md` if the two are kept separate (use
   judgment — `editor-core-separation-21` kept them as two distinct files;
   match that precedent unless a genuine reason not to is found).
4. `git_add` + `git_commit` covering every doc file changed and both
   reports.
