# PHASE7 — Full regression, documentation update, and the Entry Gate for BIG-STEP 2

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Step 9 in full (the literal checklist this phase closes out).
Previous phase report to read first: `PHASE6_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

This is the ONLY phase in this campaign allowed to run a full clean build and
a full `ctest` regression pass (PHASE0's Locked Decision #2). By the end of
this phase: the whole engine builds clean, every test passes (100%, count
only ever growing versus this campaign's own starting baseline),
documentation reflects the new capability, and every single checkbox in the
design doc's own Step 9 is independently, freshly re-confirmed true — this
literal checklist is the Entry Gate that unblocks BIG-STEP 2 (Editor
Integration), a SEPARATE, future campaign.

## Step 2: The Situation (Where are we now?)

PHASE1-6 have each already run their own incremental build + targeted
`ctest` filter + live spot-checks. This phase is the FIRST time the full
suite runs since this campaign started — a real risk exists that a targeted
filter in an earlier phase missed an interaction with some unrelated,
pre-existing test (e.g. a `_v2`/`_v3` plugin test that happens to also touch
`RenderFeatureCompositor`'s internals). Budget real time to diagnose and fix
anything a full run surfaces — do not treat a full-suite failure as "someone
else's problem"; per PHASE0's Locked Decision #9, if a genuine, isolated fix
is needed here, this phase MAY `delegate_task` with `position: "next"` to fix
a specific, narrow regression finding (never to redo an entire earlier
phase's work wholesale) before finishing.

## Step 3: The Plan (detailed strategy)

### 3.1 — Full clean incremental build

```
cmake --build build
```
(Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.)
Confirm zero errors, and review warnings for anything newly introduced by
this campaign's own changes (PHASE1-6's files) — fix any genuinely new
warning that this campaign's own code introduced; pre-existing warnings
unrelated to this campaign are out of scope.

### 3.2 — Full regression suite

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
Confirm 100% pass, and confirm the total test count is >= this campaign's
own starting baseline PLUS every new Tier-1 test added across PHASE1-5
(`ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests`,
`RenderFeatureCompositorProjectFeatureTests`, the `RegisterProjectRenderFeature`
API tests, the extended `ProjectAssemblyRegistrationLedgerTests`, the new
`RenderGraphCompilerTest` case) — never a net decrease. If any test fails,
diagnose via `GET /get_logs` where applicable (a live-Editor-dependent test)
or the test's own `ctest --output-on-failure` output, fix the root cause, and
re-run ONLY the affected filter before re-running the full suite again.

### 3.3 — Documentation: `docs/conventions/project-assembly-system.md`

This file's own existing section, `"On-screen Game View compositing —
investigated, confirmed NOT safe today"` (currently around line 289), is now
FACTUALLY OUT OF DATE — this campaign made it safe. Rewrite this section
(keep it at roughly the same location, update its own heading to reflect the
new, current state, e.g. `"On-screen Game View compositing — safe today via
Core::RegisterProjectRenderFeature()"`) describing, as a NEW, complete,
present-tense description (per this campaign's own "treat each phase's
output as genuinely new, no changelog framing" convention):
  - What `Core::RegisterProjectRenderFeature()`/`UnregisterProjectRenderFeature()`
    do and their real signatures.
  - That this is a plain, non-ABI-versioned path, deliberately separate from
    `gte_plugin_abi`'s `IRenderFeatureModule_v2`/`_v3`.
  - The bounded-slot GPU-state design (`kMaxConcurrentProjectRenderFeatures`)
    and WHY it exists (the rename-cycle-starvation hazard), briefly, without
    repeating the full design doc's own prose verbatim.
  - The hot-reload teardown guarantee (PHASE4).
  - That `RenderPassEvent::AfterEverything` is required for any pass this
    callback declares.
  - A short, complete, working code example (mirrors the design doc's own
    Section 5 "Proposed API Shape", updated to match whatever the REAL, final
    signature ended up being after PHASE1-3, if anything drifted during
    implementation).
  - Explicitly state this is BIG-STEP 1 only — there is still no Editor UI
    menu item for this; a Project Assembly author must call this method
    directly from their own `RegisterProject()`.
  - That a registered feature is visible, and structurally distinguishable
    from a `gte_plugin_abi` plugin feature, via `GET /render_graph`'s
    `render_features[]` array (`"is_project_feature": true`) and the Editor's
    "Render Graph" panel (a `"[Project]"` tag, mirroring `"[v3]"` — PHASE2).

**A second, separate stale cross-reference in this SAME file also needs
fixing, confirmed by a fresh read while writing this phase file**: further
down, this file's own "Non-Goals" bullet list (currently around line 507-510)
still lists, as an open, unresolved gap, "a safe way to alias an imported
handle onto an already-tracked physical resource, on-screen Game View
compositing — see above" (the "see above" pointing at the very section this
phase just rewrote). Once this campaign ships, on-screen Game View
compositing is no longer an open gap for the `RegisterProjectRenderFeature()`
path — update this bullet so it no longer implies the whole topic is
unresolved (e.g. split it into two clauses: the generic handle-aliasing
gap remains real and unresolved, while on-screen compositing itself is now
solved via the dedicated compositor path, cross-referencing the rewritten
section instead of contradicting it). Re-read the surrounding bullets before
editing — do not remove or reword anything else in that list.

### 3.4 — Documentation: `AGENTS.md`

Add a short new subsection under (or immediately adjacent to) the existing
`"## Render Pass System"` section (currently around line 133) describing, in
AGENTS.md's own established terse style, that a Project Assembly can register
an on-screen render feature via `Core::RegisterProjectRenderFeature()`,
pointing at `docs/conventions/project-assembly-system.md`'s updated section
(3.3 above) for the full detail — mirror how this file already cross-
references other `docs/conventions/*.md` files elsewhere, rather than
duplicating detail here.

### 3.5 — Final, explicit Step 9 checklist tick-through

Re-state the design doc's ENTIRE Step 9 checklist verbatim in this phase's
own completion report, and against EACH checkbox, write the actual, fresh
evidence confirming it true (a file/line citation, a test name and its
passing result, a live HTTP response/screenshot reference from PHASE6's own
report) — never simply copy-paste "done" without evidence:

  - [ ] `ProjectRenderFeatureCallback.h` exists, compiles standalone, zero
        circular dependency (PHASE1).
  - [ ] `RenderFeatureCompositor::Entry`'s third module-kind + `projectFeatureSlot`
        field, Tier-1 tested (PHASE1/PHASE2); `RenderFeatureDebugEntry::isProjectFeature`
        (mirroring `isV3`) correctly distinguishes a Project Assembly feature
        from a plugin feature in `GET /render_graph`'s JSON AND the Editor's
        "Render Graph" panel (PHASE2 + PHASE6 item 2).
  - [ ] Fixed-size, reusable project-feature slot free list, Tier-1 tested
        AND live rename-cycle-bounded proof (PHASE2 + PHASE6 item 7).
  - [ ] `Core::RegisterProjectRenderFeature()`/`UnregisterProjectRenderFeature()`
        exist, null-safe, reject (never truncate) an over-length name, reach
        the same live `RenderFeatureCompositor` instance (PHASE3).
  - [ ] `ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()` tears
        down render features, releases slots, BEFORE render-pass providers,
        live-confirmed across a real hot-reload cycle, zero crash/duplicate/
        dangling callback, `GET /project_assembly/debug/ledger` matches
        (PHASE4 + PHASE6 item 4/5).
  - [ ] Hand-wired trivial project render feature visible in a real Editor
        Game View screenshot, not merely the debug panel (PHASE6 item 3).
  - [ ] `DetectRenderPassEventContradictions()` reports zero contradictions
        for the new pass kind, confirmed live (PHASE5 + PHASE6).
  - [ ] `gte_plugin_abi`'s existing `_v2`/`_v3` plugins confirmed, live,
        completely unaffected (PHASE6 item 8).
  - [ ] Full `ctest` regression pass, 100%, zero unexplained delta (this
        phase, 3.2 above).

**ENTRY GATE FOR BIG-STEP 2** must be stated explicitly, in these exact
terms, in this phase's own completion report: every checkbox above is
independently confirmed TRUE, and therefore
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
may now be opened by a FUTURE, SEPARATE campaign — this campaign itself does
NOT open or act on that file in any way.

### 3.6 — `CAMPAIGN_COMPLETION_REPORT.md`

Write one final, campaign-level report (a sibling file to the per-phase
`PHASEn_COMPLETION_REPORT.md` files, mirroring
`editor-core-separation-22/CAMPAIGN_COMPLETION_REPORT.md`'s own precedent):
a short summary of what this whole campaign built, the final full-build/
full-`ctest` result, and the Entry Gate statement from 3.5 above, with links
(relative paths) to every phase's own individual completion report for
detail.

### 3.7 — Ambiguity checkpoints

  - If the full `ctest` run surfaces a failure whose root cause is genuinely
    ambiguous or touches a decision beyond a narrow, mechanical fix,
    `ask_questions` before delegating a fix sub-task blindly.
  - If `docs/conventions/project-assembly-system.md`'s existing section
    structure makes an in-place rewrite awkward (e.g. other sections
    cross-reference the OLD "not safe today" framing by name), `ask_questions`
    about how much of the surrounding document may be touched versus staying
    narrowly scoped to the one section.

### 3.8 — End of phase (and end of campaign)

1. Full clean incremental build succeeds (3.1).
2. Full `ctest` regression pass, 100%, count only growing (3.2).
3. `docs/conventions/project-assembly-system.md` and `AGENTS.md` both updated
   (3.3/3.4).
4. `PHASE7_COMPLETION_REPORT.md` written with the full, evidenced Step 9
   tick-through (3.5) and the explicit Entry Gate statement.
5. `CAMPAIGN_COMPLETION_REPORT.md` written (3.6).
6. `git_add` + `git_commit` covering the doc updates and both reports —
   this is the final commit of this campaign.
