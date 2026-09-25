# PHASE6 — Docs, Full Regression, Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — this is the final
phase; re-read the WHOLE document one more time, including every Locked
Product/Architecture Decision, before starting). Read
`PHASE1_COMPLETION_REPORT.md` through `PHASE5_COMPLETION_REPORT.md` in full —
this phase's own docs updates must describe the REAL, final, as-shipped
shape recorded in those reports, not this master strategy's own original
plan (the two should agree, but PHASE5's own completion report is the
ground truth for exact JSON field names/status codes actually shipped). Use
`ask_questions` for any genuine ambiguity. **This is the ONLY phase in this
campaign allowed to run a full clean build and full `ctest` regression
pass**, and the only phase allowed to `delegate_task` a fix — and ONLY if
that regression pass surfaces a real, newly-broken, unexplained failure.

## Step 1: The Goal

Close out the campaign: document every new HTTP endpoint in
`docs/conventions/networking.md`, confirm `AGENTS.md` needs no correction (or
make the small, real correction if it does), run the one full
build+test+smoke-test pass this whole campaign has been deliberately
deferring, and write `CAMPAIGN_COMPLETION_REPORT.md` summarizing everything
that shipped across all 5 implementation phases.

## Step 2: The Situation

- `docs/conventions/networking.md` is a single, long, append-only file — one
  new bullet PER endpoint (or one combined bullet covering all 6, if that
  matches this file's own existing convention for a cluster of closely
  related endpoints shipped in one campaign — `read_file` the file's OWN
  most recent bullets, in particular whatever `editor-core-separation-7`'s
  own PHASE5 appended for `GET /render_graph`, and whatever
  `frame-debugger-3`'s own PHASE7 appended for the 7 `/frame_debugger/*`
  routes, to see which style this file actually uses for "several related
  endpoints from one campaign" — match that exact style, do not invent a
  third one).
- `AGENTS.md` — `search_in_dir` for `"Render Graph"` and `"RenderFeatureCompositor"`
  and `"IRenderFeatureModule"` across it — confirm whether any EXISTING
  mention describes the panel/compositor as read-only, or otherwise
  describes a fact this campaign changed. Fix ONLY genuine factual staleness
  — do not rewrite prose that is still accurate just because this campaign
  touched a nearby file.

## Step 3: The Plan

### Step 3.1 — `docs/conventions/networking.md` update

Append (in whatever exact style Step 2 above determined matches this file's
own convention) the REAL, final, as-shipped contract for all 6 new
endpoints — copy the exact JSON shapes/status codes straight out of
`PHASE5_COMPLETION_REPORT.md`'s own captured live evidence, never re-derive
them from source a second time.

### Step 3.2 — `AGENTS.md` correction (only if Step 2 found something genuinely stale)

If nothing is stale, explicitly say so in `PHASE6_COMPLETION_REPORT.md`
(mirror `editor-core-separation-7`'s own PHASE5, which did exactly this and
found nothing needed changing) — do not force an edit just to have one.

### Step 3.3 — Full regression pass (the ONE time this campaign does this)

1. Full clean-ish build: `cmake --build build` (an incremental Ninja build —
   if every prior phase's own incremental build already succeeded, this
   should report "no work to do" or a small residual recompile; that is the
   expected, healthy outcome, not a red flag).
2. Full regression test:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`.
   Record the EXACT total/passed/failed/skipped counts, and compare against
   `editor-core-separation-7`'s own final documented baseline (1819 total,
   1817 passed, 2 legitimate environment-gated skips, per that campaign's
   own `CAMPAIGN_COMPLETION_REPORT.md`) — this campaign's own final count
   should read that baseline PLUS every new Tier-1 test added across
   Phases 1, 2 (if any), and 5 (the bridge tests + `NetworkRoutesTests.cpp`
   additions).
3. **If ANY test fails and the failure is NOT obviously pre-existing/
   unrelated to this campaign's own changes** (cross-check against the
   baseline above — a test that was already failing/skipped before this
   campaign started is not this campaign's problem to fix), use
   `delegate_task` to spin up a dedicated fix — this is the ONE exception to
   "implementation phases must not call `delegate_task`" (Workflow Rule 7).
   Explicitly instruct that delegated task to use `ask_questions` for any
   ambiguity, per this whole campaign's own standing rule.
4. Final live HTTP smoke test — a SHORT re-confirmation, not a full re-run
   of PHASE5's own exhaustive session: `run_app_background`
   `GreatTamanaEditor.exe`, `gte_send_request("/render_graph/passes")`
   (`200`), `gte_send_request("/get_swapchain")` + `load_image` (Editor UI
   renders correctly), `gte_send_request("/get_logs?limit=50")` (no new
   warnings/errors), `stop_app_background`.

### Step 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Mirror `editor-core-separation-7/CAMPAIGN_COMPLETION_REPORT.md`'s own exact
structure and depth:
- Original goal (this document's own Step 1, all 4 requirements, and the
  FINAL locked scope for each — especially requirement #2's real,
  product-owner-driven scope reduction).
- What each of Phases 1–5 actually shipped (one paragraph each, referencing
  their own `PHASEn_COMPLETION_REPORT.md`).
- Every Locked Product/Architecture Decision from `PHASE0_MASTER_STRATEGY.md`
  and how each was actually realized (mirror the numbered-list style
  `editor-core-separation-7`'s own report used).
- The final, locked shape of all 6 new HTTP endpoints, with REAL captured
  request/response examples (pulled from PHASE5's own completion report).
- The full regression result (Step 3.3's own real numbers).
- What this campaign explicitly did NOT do (copy `PHASE0_MASTER_STRATEGY.md`'s
  own "Non-Goals" section verbatim, confirming each one truly was never
  touched).

### Verification

Everything in Step 3.3 above IS this phase's own verification — there is no
separate section.

### What this phase does NOT do

- Does not add any new endpoint, UI control, or Core-tier mutation of its
  own — purely docs + verification + closeout.

### Completion

Write `PHASE6_COMPLETION_REPORT.md` AND `CAMPAIGN_COMPLETION_REPORT.md`
(both, in this same folder), then `git_add` + `git_commit` (the final commit
of this whole campaign).
