# editor-core-separation-9 — PHASE5: Docs, Full Regression & Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it FIRST, in full. Read
`PHASE1`–`PHASE4`'s own `PHASEn_COMPLETION_REPORT.md` files before starting
— this phase's docs updates must accurately reflect what was ACTUALLY
shipped (including any deviations recorded in those reports), never what was
originally planned if the two differ.

**Use `ask_questions`** whenever a real design ambiguity comes up that
`PHASE0_MASTER_STRATEGY.md` or this file does not already resolve.

**This is the ONLY phase in this campaign allowed to run a full clean build
and the full `ctest` regression pass (Workflow Rule 1, `PHASE0_MASTER_STRATEGY.md`).**
If it surfaces a real, newly-broken, unexplained test failure, use
`delegate_task` to hand off a dedicated fix (Workflow Rule 7's one stated
exception) — do not attempt to silently work around a real regression by
loosening a test's expectation.

## Step 1: The Goal

Close out the campaign the same way every prior one in this codebase has:
update the two permanent, living documentation files this campaign's own
changes affect, run the one full build + full regression pass this whole
campaign is allowed to run, do one final live end-to-end HTTP smoke test
covering every phase's own new surface at once, and write
`CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation

Read `AGENTS.md`'s existing "Plugin Architecture" section (the exact
paragraph this phase extends — mirror its established prose style: one
dense paragraph per campaign, stating what shipped, what the one real
honest caveat is, if any, and a pointer to the full phase-by-phase writeup),
`docs/conventions/plugin-architecture.md` (the file that section links to
— confirm it exists and read its current structure before adding to it),
and `plugins/gte_plugin_abi/PublicSurface.md`'s existing "Added by later
phases" bullet list (the exact format this phase's own new bullet must
match — copy the `editor-core-separation-6` PHASE1 bullet's own format
precisely).

## Step 3: The Plan

### 3.1 — `plugins/gte_plugin_abi/PublicSurface.md`

Append a new bullet under "Added by later phases", mirroring the existing
`editor-core-separation-6` bullet's exact format: list every new type that
crossed the ABI boundary this campaign (`PluginRenderResource.h`'s types,
`IPluginRenderPassBuilder_v3.h`'s types, `IRenderFeatureModule_v3`), state
that `PluginRenderOperationRegistry`/`PluginRenderPassBuilderAdapter_v3`/
`PluginRenderResourceTranslation.h` are `gte_core`-internal (never under
`plugins/gte_plugin_abi/`), confirmed by re-reading every file this campaign
actually added before writing this bullet (do not write it from memory of
the phase plan — the phase plan may have drifted during real implementation,
per every prior campaign's own honest-disclosure precedent, e.g.
`editor-core-separation-8`'s own "CORRECTED FINDING" sections).

### 3.2 — `AGENTS.md`'s "Plugin Architecture" section

Add one new paragraph (after the existing `editor-core-separation-3`
paragraph, before the "Full convention:" link line) summarizing, honestly:
what `_v3` is, why it exists (the closed-enumeration problem `_v2` had —
one sentence), the operation-registry mechanism (one sentence), that `_v3`
is now the RECOMMENDED path for new plugin authors while `_v2` remains fully
supported forever (Locked Product Decision #1), the 2-pass GPU blur demo
plugin as the concrete proof, and a pointer to
`task_manager/editor-core-separation-9/PHASE0_MASTER_STRATEGY.md`/
`CAMPAIGN_COMPLETION_REPORT.md` for the full writeup — mirroring the exact
prose density and citation style of every other paragraph in that same
section (re-read `editor-core-separation-3`'s own paragraph immediately
before writing this one, for tone).

### 3.3 — `docs/conventions/plugin-architecture.md`

Add a matching, slightly more detailed section (this file is where the FULL
detail belongs — `AGENTS.md` only ever holds a short summary + link,
per its own "Documentation" section at the top of that file). Include the
final, real operation-registry table shipped (id/kind — copy it verbatim
from `PHASE2_COMPLETION_REPORT.md`/`PHASE3_COMPLETION_REPORT.md`'s own
tables, do not re-derive it), the exact 2-pass blur demo's shape, and the
blackboard's exact contract.

### 3.4 — Full regression pass

1. `git_status` — confirm branch + clean tree (Workflow Rule 9).
2. Full clean build: `cmake --build build` (if a genuinely clean
   rebuild is warranted — check with the human via `ask_questions` if
   unsure whether a full `rm`-and-reconfigure is expected here vs. relying
   on the existing incremental tree's own correctness after 4 phases of
   incremental builds already passing; prior campaigns' own PHASE-closeout
   docs, e.g. `editor-core-separation-6` PHASE8, ran a full clean build —
   mirror that unless told otherwise).
3. `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`
   — full pass. Record the exact pass count and compare against the
   baseline stated in `AGENTS.md`'s own most recent campaign paragraph (the
   `editor-core-separation-1` paragraph currently cites 1773 tests as of
   that campaign — find whatever the ACTUAL current baseline is by
   `search_in_dir` across `AGENTS.md` for the highest recently-stated test
   count before this campaign's own new tests are added, since several
   campaigns have landed since `editor-core-separation-1`). If any test
   newly fails and is not obviously explained by this campaign's own
   changes, use `delegate_task` to hand off a dedicated, scoped fix
   (Workflow Rule 7's stated exception) rather than debugging it inline
   here indefinitely.
4. Final live end-to-end HTTP smoke test: `run_app_background`
   `GreatTamanaEditor.exe` with BOTH `_v2` and `_v3` demo plugins loaded
   simultaneously — confirm no collision/crash, `GET /get_game_view` looks
   correct, `GET /render_graph` shows every pass from every phase's own demo
   plugin correctly, `GET /get_logs?limit=500` shows zero unexpected
   warnings/errors across a full run touching every new mechanism this
   campaign added (resource creation, the operation registry, the
   blackboard). `stop_app_background` when done.

## Verification

Everything in Step 3.4 above — this phase's own verification IS the plan,
there is no separate lighter-weight check this time (Workflow Rule 1's one
stated exception).

## Non-Goals for this phase

- No new code beyond what is required to fix a genuine regression the full
  `ctest` pass surfaces (via `delegate_task`, per Workflow Rule 7).
- No scope creep — do not use this phase as an opportunity to add anything
  not already planned by `PHASE0_MASTER_STRATEGY.md`/`PHASE1`–`PHASE4`.

## Completion

Write `PHASE5_COMPLETION_REPORT.md` (this phase's own report) AND
`CAMPAIGN_COMPLETION_REPORT.md` (the whole-campaign closeout, mirroring
every prior campaign's own `CAMPAIGN_COMPLETION_REPORT.md` shape — a
phase-by-phase summary table, the final honest caveats section if any real
one exists, e.g. the `Dispatch` group-count cap's own "no device-lost
recovery path exists in this engine" caveat restated from Design Doc
R28/Locked Product Decision #2, and the final test-count delta). `git_add` +
`git_commit` ("editor-core-separation-9 PHASE5 + campaign closeout: docs,
full regression, IPluginRenderPassBuilder_v3 shipped").
