# PHASE5 — Docs, Full Regression, Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST). Also read
`PHASE1`/`PHASE2`/`PHASE3`/`PHASE4`'s own `PHASEn_COMPLETION_REPORT.md`
files before starting — PHASE4's report in particular may record the REAL,
final JSON shape if it differed at all from PHASE2's own sketch; this
phase's docs must describe the REAL shape, not the sketch. This is the ONLY
phase in this campaign allowed to run a full clean build/full `ctest` pass
(Workflow Rule 1) — use `ask_questions` if the full regression pass surfaces
anything genuinely ambiguous about how to fix it (Workflow Rule 7's one
exception).

## Step 1: The Goal

Document `GET /render_graph` in `docs/conventions/networking.md` (one new
bullet, matching the style/depth of every existing endpoint bullet already
there), confirm `AGENTS.md`'s existing "Render Graph" panel mentions still
read accurately given Phase 3's refactor, run the ONE full clean build + full
`ctest` regression pass + a final live HTTP smoke test this whole campaign is
allowed to run, and write `CAMPAIGN_COMPLETION_REPORT.md` summarizing the
whole five-phase campaign.

## Step 2: The Situation

Confirmed, `docs/conventions/networking.md` is a single, long, linear,
append-only file — every prior campaign (`network-impl-1` through
`stl-parser-2`, `scene-serialization-2`, `logger-1`) added its own endpoint
as ONE new bullet, appended near the end of the file's main bulleted list
(or, for a large enough feature, e.g. "Named Texture Capture", its own `##`
subsection at the very end) — `read_file` the CURRENT end of this file
before editing, to confirm the exact latest bullet this phase's own new
bullet should be appended immediately after.

Confirmed, `AGENTS.md` mentions "Render Graph" panel behavior in at least
these places (`search_in_dir` hits, Step 2 of PHASE0_MASTER_STRATEGY.md):
lines ~270, ~304, ~560 (approximate — re-confirm exact current line numbers
via a fresh `search_in_dir` before editing, since earlier phases in THIS
campaign do not touch `AGENTS.md` and cannot have shifted these lines, but
OTHER concurrently-landed campaigns might have). Read each hit in context —
most describe the panel's EXISTING behavior (which passes ran, GPU-driven
batch readout) in ways that remain accurate after this campaign (the panel
still shows the exact same information, just sourced from
`RenderGraphMetadata` now instead of three raw sources) — these likely need
NO edit at all. Only edit an `AGENTS.md` passage if this campaign made it
factually WRONG (e.g. if any passage specifically says the "Export DOT"
button is disabled/planned-for-later — that is now false and MUST be
corrected to describe the real, shipped behavior).

## Step 3: The Plan

### Step 3.1 — `docs/conventions/networking.md` update

Append ONE new bullet (after the current last bullet in the file — `read_file`
to find it, do not guess), following the exact voice/depth of every existing
bullet (see the `GET /list_tabs`/`GET /activate_tab` bullet, or the `GET
/get_texture`/`GET /list_textures` section, as the closest-shaped templates —
both describe: which campaign added it, which bridge it is built on, the
exact request/response contract, and the exact status-code mapizing):

```markdown
- **`GET /render_graph`** (`editor-core-separation-7` campaign,
  `task_manager/editor-core-separation-7/PHASE0_MASTER_STRATEGY.md`) returns
  the exact same data the Editor's "Render Graph" panel shows, as one
  versioned JSON object - built on `FrameCaptureBridge`'s existing
  "publish once per frame from the main thread, read a thread-safe copy
  from the network thread" pattern (a SECOND `Publish*`/`Get*` pair,
  `PublishRenderGraphMetadata()`/`GetPublishedRenderGraphMetadata()`,
  alongside its existing texture-list pair - not a new, sixth bridge class;
  see that campaign's PHASE0 Locked Design Decision #4 for why). The
  response body is `gte::rg::RenderGraphMetadata::to_json()` verbatim:
  `schema_version` (an integer, bumped only on a future breaking shape
  change - currently `1`), `offscreen_regime`/`present_regime` (each one
  ExecuteTimingMode regime's own `regime_name`/`passes`/`resources`/
  `timing_slot_budget_exhausted` - every enum already resolved to a human
  string, every GPU timing value already resolved to BOTH a display string
  AND a raw nullable number, every pass's `RenderPassTagMask` already
  resolved to at most one human `tag_group_label` via
  `RenderPassGroupRegistry`), `gpu_driven_batches` (the same "instances
  culled this frame" readout the panel's own GPU-Driven Batches section
  shows), and `render_features` (the same loaded `_v2` plugin
  ordering/blend-mode readout the panel's own Plugin Render Features
  section shows). ALWAYS reflects the truly latest real engine frame,
  completely independent of whether a human has the Editor panel's own
  "Pause" checkbox ticked (that checkbox only freezes what ONE ImGui window
  displays - see that campaign's PHASE0 Locked Design Decision #9).
  Responds `200` on success, `503` if the bridge pointer itself is null
  (reachable only in a test that constructs `NetworkServer` directly, the
  same convention every other bridge-backed route already documents above).
  No query parameters in this first version - see that campaign's own
  PHASE0 Locked Design Decision #8 for why a future `?regime=`/`?pass=`
  filter is deliberately deferred rather than spun up speculatively. See
  `task_manager/editor-core-separation-7/PHASE0_MASTER_STRATEGY.md` for the
  full five-phase campaign writeup.
```

(Adjust any wording above that PHASE4's own completion report records as
having actually shipped differently — this phase's job is to document
REALITY, never to re-describe the plan as if it were automatically what got
built.)

### Step 3.2 — `AGENTS.md` spot-check

Re-read every "Render Graph" hit found in Step 2 above, in the CURRENT
`AGENTS.md`. For each: does this passage still read as TRUE after Phases
1-4? If yes, leave it untouched (do not churn a passage just because this
campaign touched a nearby file — only edit what is now factually
incorrect). If a passage specifically references the "Export DOT" button as
disabled/future/planned, correct it to state it is implemented, briefly
describing what it does (one sentence, matching this file's own terse
style elsewhere), and citing this campaign
(`task_manager/editor-core-separation-7/`).

### Step 3.3 — Full regression (THIS PHASE ONLY, Workflow Rule 1's one exception)

1. A full clean build: confirm the exact existing full-build invocation this
   repo's own prior `PHASEn_DOCS_FULL_...` phases used (e.g.
   `editor-core-separation-6`'s own `PHASE8_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`,
   `render-pass-6`'s own `PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md` —
   `read_file` one of those for its own exact "Verification" section's
   command line) — likely `cmake --build build` from a clean or
   near-clean state; if a genuinely FROM-SCRATCH clean build is intended
   (deleting `build/` first), confirm this is really desired via
   `ask_questions` before deleting anything, since a full from-scratch
   rebuild on this machine is documented elsewhere in this campaign's own
   notes as slow.
2. Full regression test: `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`.
3. If any test fails: diagnose first (read the actual failure output, do not
   guess) — is this a REAL regression this campaign's own Phases 1-4
   introduced, or a PRE-EXISTING failure unrelated to this campaign (check
   `git log`/`git blame` on the failing test/the code it covers, or simply
   `git stash`+re-run on the pre-campaign commit if truly unsure)? If it is
   a real regression from this campaign, use `delegate_task` to fix it
   (Workflow Rule 7's one allowed exception for Phase 5) — the delegated fix
   task must ALSO be told to use `ask_questions` for any of its own
   ambiguities (per this whole campaign's own transitive rule), must NOT
   itself call `delegate_task` further, and must re-run the SAME failing
   test(s) (not the full suite necessarily, unless the fix's own blast
   radius is unclear) to confirm the fix before reporting back.
4. Final live HTTP smoke test — the same checks PHASE4's own Verification
   section already ran, repeated once more now that everything (docs
   included) is final: `run_app_background`, `GET /render_graph`,
   `GET /get_swapchain` + `load_image`, `GET /get_logs?limit=50`,
   `stop_app_background`.

### Step 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Write this file (this same folder), summarizing: the original goal (Step 1
of PHASE0), what each of the five phases actually shipped, the final,
locked `GET /render_graph` JSON shape (a real example response, verbatim),
a link back to every `PHASEn_COMPLETION_REPORT.md`, and the full regression
result (pass count, zero unexplained failures — or, if any were found and
fixed via `delegate_task` in Step 3.3, a note on exactly what was wrong and
how it was fixed).

### Verification

Everything in Step 3.3/3.4 above IS this phase's own verification — there
is no separate, additional verification step beyond running the full
regression and the final smoke test and recording the results honestly.

### What this phase does NOT do

- Does not add any new engine feature/endpoint/panel behavior — purely
  documentation + verification + closeout.
- Does not call `delegate_task` UNLESS Step 3.3 finds a real, unexplained
  regression that needs a dedicated fix (Workflow Rule 7's one exception).

### Completion

`git_add` + `git_commit` (the docs changes, `CAMPAIGN_COMPLETION_REPORT.md`,
and `PHASE5_COMPLETION_REPORT.md` together, in one final commit for this
campaign).
