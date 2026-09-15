# PHASE5 — Full Regression Pass, Live Smoke Test, Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.
**Also read first:** `PHASE1_COMPLETION_REPORT.md` through
`PHASE4_COMPLETION_REPORT.md` (this campaign's own, in
`task_manager/doc-refactor-1/`) — reconcile against whatever their own
"Deviations" sections recorded rather than this document's assumed wording
wherever they disagree. Also skim
`task_manager/network-impl-7/PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md`
Sections 3.5 and 3.6 one more time — THIS phase is where those two
specific obligations (deferred by this campaign's own Phase 1) finally get
discharged.

## Step 1: The Goal

This is the one phase in the whole `doc-refactor-1` campaign explicitly
permitted — and required — to run a full clean build and a full `ctest`
regression pass, across all three of this project's standard build
configurations, PLUS a manual live runtime smoke test of the
`activate_tab`/`list_tabs` HTTP surface. It also closes out the whole
campaign with `task_manager/doc-refactor-1/CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation / The Problem

- Phases 1-4 were entirely Markdown edits verified only by fast, no-op
  compile checks — this is intentional (per `PHASE0`'s Workflow Rule #1), but
  it means NO phase so far has actually proven that:
  - The doc-only changes truly didn't touch anything `cmake`/`ctest` cares
    about (they shouldn't have, since no `.cpp`/`.h`/`CMakeLists.txt` file was
    touched in Phases 2-4 — but this must be PROVEN, not assumed, before the
    campaign is declared done).
  - `network-impl-7`'s own Section 3.5 (three-configuration build+regression)
    and Section 3.6 (manual live smoke test) obligations, explicitly deferred
    by this campaign's Phase 1, are still outstanding and must happen
    somewhere — this is that "somewhere".
- This project already keeps dedicated build folders for two of the three
  required configurations: `build/` (default —
  `GTE_ENABLE_EDITOR=ON`/`GTE_ENABLE_NETWORK=ON`/`GTE_ENABLE_PROJECT_
  PANEL=ON`), `build-editor-off/` (`-DGTE_ENABLE_EDITOR=OFF`), and
  `build-project-panel-off/` (`-DGTE_ENABLE_PROJECT_PANEL=OFF`) — reuse these
  exact folders rather than inventing new ones, re-running `cmake`'s
  configure step in each to pick up any change since they were last
  generated.

## Step 3: The Plan

### 3.1 — Full pass, configuration 1: default (`build/`)

1. `cmake -S . -B build` (re-configure; picks up nothing new since only `.md`
   files changed, but confirms the configure step itself is still clean).
2. `cmake --build build` (full build, not an incremental sanity check this
   time — treat any warning/error as a real regression to investigate,
   remembering that Phases 1-4 touched zero `.cpp`/`.h`/`CMakeLists.txt`
   files, so a build failure here would indicate something environmental,
   not something this campaign's own edits caused; still must be resolved
   before continuing).
3. `cd build && ctest -C Debug --output-on-failure` — full suite. Record the
   total test count and confirm zero regressions against the count recorded
   in `task_manager/network-impl-7/PHASE4_COMPLETION_REPORT.md`'s own "22
   tests" `*Network*`-filtered figure (the full suite total will be much
   larger; just confirm nothing that used to pass now fails).

### 3.2 — Full pass, configuration 2: `-DGTE_ENABLE_EDITOR=OFF`
(`build-editor-off/`)

1. `cmake -S . -B build-editor-off -DGTE_ENABLE_EDITOR=OFF`.
2. `cmake --build build-editor-off`.
3. `cd build-editor-off && ctest -C Debug --output-on-failure`.
4. This configuration proves `NullEditorLayer::ActivateTab()`,
   `EditorUiCommandBridge` itself, and `NetworkRoutes.cpp`'s pure functions
   all compile/pass with ZERO ImGui linked — confirm specifically that any
   `*ActivateTab*`/`*ListTabs*`-named test still passes here (a
   `GET /activate_tab` for a known name should get a `409`-shaped outcome
   from `NullEditorLayer`'s `tabExists = false`, never a crash, per
   `network-impl-7`'s own Phase 5 Section 3.5 point 2 — re-confirm this
   exact behavior is still correct after the doc-only changes, which it
   should be, but prove it).

### 3.3 — Full pass, configuration 3: `-DGTE_ENABLE_PROJECT_PANEL=OFF`
(`build-project-panel-off/`)

1. `cmake -S . -B build-project-panel-off -DGTE_ENABLE_PROJECT_PANEL=OFF`.
2. `cmake --build build-project-panel-off`.
3. `cd build-project-panel-off && ctest -C Debug --output-on-failure`.
4. Confirm `GET /list_tabs`'s response no longer includes `"Project"`, and
   `GET /activate_tab?name=Project` now returns `404` rather than `409`/`200`
   (per `network-impl-7` Phase 5 Section 3.5 point 3) — this needs the live
   smoke test in Step 3.4 below against THIS specific build, not just a
   `ctest` pass, since it's most convincingly proven by a real HTTP round
   trip.

### 3.4 — Manual live smoke test (the campaign's final acceptance check)

Using the DEFAULT configuration's binary (rebuilt in Step 3.1):

1. `run_app_background` the built `GreatTamanaEngine.exe`.
2. `gte_send_request` `GET /list_tabs` — confirm the full expected panel
   list (including `"Project"`, since this is the default/`ON` config).
3. `gte_send_request` `GET /activate_tab?name=Profiler` — confirm `200`.
4. Immediately `gte_send_request` `GET /get_swapchain` and visually confirm
   (via the returned image) the "Profiler" tab is genuinely the frontmost/
   active tab among the bottom-docked group.
5. `gte_send_request` `GET /activate_tab?name=Memory` — confirm `200`, then
   re-capture `/get_swapchain` and confirm "Memory" is now frontmost instead
   (proving repeated activation genuinely switches each time).
6. `gte_send_request` `GET /activate_tab?name=NotARealTab` — confirm `404`.
7. `stop_app_background` the process.
8. Separately, using the `build-project-panel-off/` binary: `run_app_
   background` it, `gte_send_request` `GET /activate_tab?name=Project` and
   confirm `404` (not `409`/`200`), then `stop_app_background` it. This is
   the one assertion from Step 3.3 that a live HTTP round trip proves more
   convincingly than a unit test alone.

### 3.5 — `task_manager/doc-refactor-1/CAMPAIGN_COMPLETION_REPORT.md`

Write this final report tying together all five `PHASEn_COMPLETION_REPORT.md`
files in this same folder:
- What was built/moved in each phase (one short paragraph per phase, citing
  its own completion report).
- The final `docs/` tree (list every file, grouped `architecture/`/
  `conventions/`/root).
- Final `README.md`/`AGENTS.md` sizes vs. their original ~133 KB/~155 KB
  (quote the actual new sizes from `browse_dir details:true`).
- The final full-suite test count from Step 3.1, and confirmation of zero
  regressions across all three configurations.
- Confirmation the manual live smoke test in Step 3.4 passed in full,
  including the `-DGTE_ENABLE_PROJECT_PANEL=OFF` `404` assertion.
- An explicit closing note that `network-impl-7`'s own Sections 3.5/3.6
  obligations (deferred by this campaign's Phase 1) are now fully discharged
  here, with a cross-reference to
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` (written in
  Phase 1) so a future reader can follow the full trail from either
  direction.

### 3.5.1 — Addendum to `network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` (closes the loop from BOTH directions, per `PHASE0_MASTER_STRATEGY.md`'s own Definition of Done)

The cross-reference in Step 3.5 above only points FROM this campaign's own
report TO `network-impl-7`'s. That report itself (written back in Phase 1,
before any of this phase's verification actually ran) currently says its own
Sections 3.5/3.6 are "deliberately deferred" with no final result recorded —
a future reader who opens ONLY `task_manager/network-impl-7/
CAMPAIGN_COMPLETION_REPORT.md` would have no way to know, without also
finding and reading THIS folder's reports, whether that deferred work ever
actually happened or what it found. Close this the other way too: append a
short new section to the END of the EXISTING
`task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` file (do not
create a new file, do not rewrite anything above it — pure append), titled
e.g. `## Addendum — Sections 3.5/3.6 Discharge Confirmation (via
doc-refactor-1)`, stating: the final full-suite test count and zero-regression
confirmation across all three configurations (from Step 3.1-3.3 above), that
the manual live smoke test (Step 3.4 above) passed in full, and a
cross-reference back to `task_manager/doc-refactor-1/PHASE5_COMPLETION_
REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` for the full verification detail.
Keep this addendum short (a few sentences) — it is a pointer/confirmation, not
a duplicate of either report's own full content.

### 3.6 — Final commit

`git add`/`git commit` everything from this phase (the three build folders'
generated artifacts are already `.gitignore`d — verify this is still true
before committing, do not accidentally stage build output) — the report and
any last-minute fix made during this phase's own verification pass, as the
final commit of the whole `doc-refactor-1` campaign, still on
`fix/doc-refactor`.

## Step 4: Verification for this phase (= verification for the whole campaign)

- All three configure+build+`ctest` passes in Sections 3.1-3.3 are green,
  zero regressions.
- The manual live smoke test in Section 3.4 (all 8 steps, across two
  different build configurations) passes exactly as described.
- `CAMPAIGN_COMPLETION_REPORT.md` is written and accurate.
- Final `git status` is clean (working tree matches the last commit) before
  considering the campaign done.

## Step 5: Deliverables / Files Touched

- Created: `task_manager/doc-refactor-1/PHASE5_COMPLETION_REPORT.md`,
  `task_manager/doc-refactor-1/CAMPAIGN_COMPLETION_REPORT.md`.
- Modified: `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md`
  (Step 3.5.1's short discharge-confirmation addendum, appended at the end —
  no other content in that file changes).
- No source file is expected to be modified in this phase UNLESS Step 3.1-3.3
  surfaces a genuine, previously-undetected regression — if that happens,
  fix it here, note the fix explicitly in this phase's own completion report
  (what broke, why, what changed), and re-run the affected configuration's
  build+`ctest` pass before continuing, mirroring the same "this phase may
  fix a real bug found during final verification" allowance
  `network-impl-7`'s own Phase 5 document granted itself.
