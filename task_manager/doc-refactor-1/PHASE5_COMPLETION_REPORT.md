# PHASE5 Completion Report — Full Regression Pass, Live Smoke Test, Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Executed as specified in
`PHASE5_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`, having first read
`PHASE0_MASTER_STRATEGY.md`, `PHASE1_COMPLETION_REPORT.md` through
`PHASE4_COMPLETION_REPORT.md` (this campaign's own, in
`task_manager/doc-refactor-1/`), and `task_manager/network-impl-7/
PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md` Sections 3.5/3.6.

## What was done

### 3.1 — Full pass, configuration 1: default (`build/`)

1. `cmake -S . -B build` — clean re-configure, succeeded (the `ktx`
   sub-project's own `git describe` warning is pre-existing/benign and
   unrelated to this campaign, confirmed by inspecting its own
   `cmake/version.cmake`).
2. `cmake --build build` — `ninja: no work to do.` on the very first
   invocation this phase (confirming Phases 1-4 truly touched zero
   `.cpp`/`.h`/`CMakeLists.txt` files, as their own reports claimed).
3. `ctest -C Debug --output-on-failure` — **1326 tests, 100% passed** (1
   machine-gated `PmxLoaderRealModelSmokeTest` skip, zero failures) in this
   first run. A latent bug affecting
   `GameEntityCommandsTest.InstantiateLightDefaultCallMatchesCreateDirectionalLightEntityRotation`
   happened not to manifest in this configuration's specific allocation
   timing on this run (it is undefined-behavior-dependent, not
   configuration-specific in cause) — it was caught instead in configuration
   2 below; see Section 3.2 for the full root-cause writeup and fix, and
   Section 3.2.1 for this configuration's own re-verification after the fix.

### 3.2 — Full pass, configuration 2: `-DGTE_ENABLE_EDITOR=OFF` (`build-editor-off/`)

1. `cmake -S . -B build-editor-off -DGTE_ENABLE_EDITOR=OFF` — clean
   configure, succeeded.
2. `cmake --build build-editor-off` — succeeded cleanly, zero
   warnings/errors.
3. `ctest -C Debug --output-on-failure` — **1140 tests**, 1 skip, **1
   FAILED**: `GameEntityCommandsTest.InstantiateLightDefaultCallMatchesCreateDirectionalLightEntityRotation`
   (`gtest` reported `Value of: RepresentSameRotation(spawnedTransform.rotation,
   editorTransform.rotation)` — `Actual: false`, `Expected: true`).

   **Root-cause investigation** (this phase's own allowed "fix a genuine bug
   found during final verification" scope, per the strategy document's Step
   3/Step 5 note): `tests/Game/GameEntityCommandsTests.cpp`'s test held
   `const Transform& editorTransform = registry.GetComponent<Transform>(editorLight);`
   — a live reference into `ComponentStorage<Transform>`'s dense
   `std::vector` — and then called `game.InstantiateLight(params)`, which
   adds a **second** `Transform` component to the very same
   `ComponentStorage<Transform>` pool via `Registry::AddComponent<Transform>()`.
   `ComponentStorage<T>::Add()` (`src/ECS/ComponentStorage.h`) explicitly
   documents that its returned reference — and, by the same mechanism, any
   reference obtained from a prior `Get()`/`GetComponent()` call — is "NOT
   stable across any later `Add()`/`Remove()` call on this same pool (the
   dense array can reallocate or get swap-moved)". The test's
   `editorTransform` reference was exactly this kind of now-documented-unsafe
   dangling reference: undefined behavior whose observable effect depends on
   the exact `std::vector` growth/reallocation timing at that moment in the
   process — which is why it passed in configuration 1's first run (a
   different set of prior test-suite allocations happened to leave the
   vector's capacity unchanged across the `Add()` call) but failed in
   configuration 2 (fewer other test suites/allocator activity before this
   test in the smaller `-DGTE_ENABLE_EDITOR=OFF` binary changed the exact
   capacity/reallocation state at the critical moment). This is a
   **pre-existing latent bug in the test file itself**, not in
   `Game::InstantiateLight()`/`Game::CreateDirectionalLightEntity()`
   (both of which are correct, deterministic, and share the exact same
   `DefaultDirectionalLightRotation()` literal) — confirmed by inspecting
   both production functions in `src/Game/Game.cpp`.

   **Fix applied** (`tests/Game/GameEntityCommandsTests.cpp`): changed the
   test to copy the rotation **by value** immediately after fetching it
   (`const Quat editorRotation = registry.GetComponent<Transform>(editorLight).rotation;`)
   instead of holding a `const Transform&` across the later `AddComponent()`
   call, and updated the final assertion to compare against that value copy.
   A short comment was added explaining the exact hazard (citing
   `ComponentStorage.h`'s own documented caveat) so a future reader does not
   reintroduce the same class of bug. Re-ran the single test with
   `--gtest_repeat=20 --gtest_shuffle` against the rebuilt
   `build-editor-off/tests/GreatTamanaEngineTests.exe` — **20/20 iterations
   passed** across every random ordering, confirming the fix, not a
   coincidental pass.
4. Rebuilt `build-editor-off` (incremental — only the one changed test
   `.cpp` recompiled) and re-ran the full `ctest` suite:
   **1140 tests, 100% passed** (1 machine-gated skip, zero failures).
5. Confirmed the two `network-impl-7`-specific assertions this
   configuration exists to prove: `*ActivateTab*`/`*ListTabs*`-named tests
   all pass here with zero ImGui linked
   (`ActivateTabEndpointEndToEndTest`/`ActivateTabEndpointNoStandInTests`/
   `ActivateTabEndpointNullBridgeTests`/`NetworkServerTests.ActivateTab*`/
   `ParseActivateTabQueryTests`/`BuildActivateTabResponseJsonTests`/
   `BuildListTabsResponseJsonTests` all passed), and confirmed by direct
   inspection of `NetworkServerTests.ActivateTabWithNullBridgeRespondsService
   UnavailableForAKnownName` passing that a known name against
   `NullEditorLayer`'s always-`tabExists=false` still yields a `409`-shaped
   outcome, never a crash.

### 3.2.1 — Re-verification of configuration 1 after the fix

Since the fix touched a test file shared by all three configurations,
rebuilt `build/` (incremental — one `.cpp` recompiled) and re-ran the
targeted filter `GameEntityCommandsTest.*:*ActivateTab*:*ListTabs*` — **31
tests, 100% passed**, confirming the default configuration was never
actually broken by this bug in its own first run, and remains clean after
the fix. (The strategy document's own Section 3.1 only required the full
suite pass once; re-running the full 1326-test suite a second time after a
one-file test-only change was judged unnecessary — the targeted filter is
the exact set of tests that could possibly be affected by this specific
one-file change, per Workflow Rule #1's "filtered down to the handful of
tests that phase actually touches" allowance.)

### 3.3 — Full pass, configuration 3: `-DGTE_ENABLE_PROJECT_PANEL=OFF` (`build-project-panel-off/`)

1. `cmake -S . -B build-project-panel-off -DGTE_ENABLE_PROJECT_PANEL=OFF`
   — clean configure, succeeded.
2. `cmake --build build-project-panel-off` — succeeded cleanly (this
   configuration compiles the full Editor, unlike configuration 2, so it
   is a much larger incremental build — 89 build steps — all succeeded,
   zero warnings/errors).
3. `ctest -C Debug --output-on-failure` — **1255 tests, 100% passed** (1
   machine-gated skip, zero failures) — this run already included the
   `GameEntityCommandsTests.cpp` fix from 3.2, since the fix was made
   before this configuration was first configured/built.

### 3.4 — Manual live smoke test

Using the default configuration's rebuilt binary
(`build/GreatTamanaEngine.exe`):

1. `run_app_background` — launched successfully (PID 21412).
2. `GET /list_tabs` — `200`,
   `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Project"]}`
   — the full expected panel list, including `"Project"` (default/`ON`
   config).
3. `GET /activate_tab?name=Profiler` — `200`,
   `{"activated_tab":"Profiler","success":true}`.
4. `GET /get_swapchain` immediately after — screenshot visually confirmed
   the "Profiler" tab (CPU Frame Time graph, CPU Scopes table) genuinely
   frontmost among the bottom-docked group.
5. `GET /activate_tab?name=Memory` — `200`,
   `{"activated_tab":"Memory","success":true}` — re-captured
   `/get_swapchain`, screenshot visually confirmed "Memory" (CPU Engine
   Dependencies / GPU Tracked-by-Engine sections) is now frontmost instead,
   proving repeated activation genuinely switches each time.
6. `GET /activate_tab?name=NotARealTab` — `404`,
   `{"error":"unknown tab name 'NotARealTab' - see GET /list_tabs for the
   currently known names","success":false}`.
7. `stop_app_background` — stopped PID 21412 cleanly.
8. Separately, using `build-project-panel-off/GreatTamanaEngine.exe`:
   `run_app_background` (PID 18668); `GET /list_tabs` — `200`,
   `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere"]}`
   — **no `"Project"` entry**, confirming `GTE_ENABLE_PROJECT_PANEL=OFF` is
   correctly reflected; `GET /activate_tab?name=Project` — **`404`**,
   `{"error":"unknown tab name 'Project' - see GET /list_tabs for the
   currently known names","success":false}` — confirmed NOT `409`/`200`,
   proving `IsKnownEditorPanelName("Project")` is genuinely `false` in this
   configuration rather than merely inert; `stop_app_background` — stopped
   PID 18668 cleanly.

All 8 steps passed exactly as specified.

### 3.5 — `CAMPAIGN_COMPLETION_REPORT.md`

Written (see that file in this same folder) tying together all five phases
of this campaign, the final `docs/` tree, final file sizes, the final
test-count/regression summary, and confirmation of the live smoke test.

### 3.5.1 — Addendum to `network-impl-7/CAMPAIGN_COMPLETION_REPORT.md`

Appended (pure append, nothing above it rewritten) a new
`## Addendum — Sections 3.5/3.6 Discharge Confirmation (via doc-refactor-1)`
section at the very end of the existing file, stating the final
per-configuration test counts, the one genuine bug found/fixed during this
phase's own verification, and a cross-reference back to this campaign's own
Phase 5/campaign completion reports.

### 3.6 — Final commit

Confirmed via `git_status`/`.gitignore` inspection that `build/`,
`build-editor-off/`, and `build-project-panel-off/` are all already
`.gitignore`d (`/build/`, `/build-editor-off/`, `/build-project-panel-off/`
lines) before staging anything — no build output was ever at risk of being
committed. `git add`/`git commit` this phase's changes (the one test-file
fix, this report, the campaign completion report, and the
`network-impl-7` addendum) as the final commit of the whole campaign.

## Deviations from the strategy document

- **One genuine, previously-undetected regression was found and fixed this
  phase**, exactly as the strategy document's own Step 5/Deliverables
  section explicitly anticipated and permitted ("No source file is expected
  to be modified in this phase UNLESS Step 3.1-3.3 surfaces a genuine,
  previously-undetected regression — if that happens, fix it here"). The
  bug (a dangling `const Transform&` in
  `tests/Game/GameEntityCommandsTests.cpp`, detailed in Section 3.2 above)
  was in a **test file**, not in `src/`'s own production code — no
  production behavior changed. This is recorded here as the one deviation
  from the "no source file is expected to be modified" default expectation,
  handled exactly per the strategy document's own explicit allowance.
- Section 3.1's own full-suite `ctest` run was only executed once for the
  default configuration in its "before the fix" state (which happened to
  already pass, since the bug's UB didn't manifest there) — re-verification
  after the fix used a targeted filter rather than the full 1326-test suite
  a second time, since the fix touched exactly one test file whose blast
  radius (every test that could possibly be affected) is fully covered by
  that filter; this is consistent with Workflow Rule #1's own targeted-filter
  allowance, not a shortcut around the full-pass requirement (the full
  suite genuinely was run once per configuration, all three green).
- No other deviations. Every deliverable in Section 3.1-3.6 was produced
  exactly as specified.

## Verification performed

- Configuration 1 (default, `build/`): configure clean, `ninja: no work to
  do.` build (before the fix), full `ctest` — 1326 tests, 0 failures
  (1 machine-gated skip). After the fix: incremental rebuild, targeted
  filter (`GameEntityCommandsTest.*:*ActivateTab*:*ListTabs*`) — 31 tests,
  0 failures.
- Configuration 2 (`-DGTE_ENABLE_EDITOR=OFF`, `build-editor-off/`):
  configure clean, full build clean, full `ctest` — 1140 tests, 1 failure
  found and root-caused; fixed; rebuilt; full `ctest` re-run — 1140 tests,
  0 failures (1 machine-gated skip). Fix additionally stress-tested with
  `--gtest_repeat=20 --gtest_shuffle` against the single affected test —
  20/20 passed.
- Configuration 3 (`-DGTE_ENABLE_PROJECT_PANEL=OFF`,
  `build-project-panel-off/`): configure clean, full build clean, full
  `ctest` — 1255 tests, 0 failures (1 machine-gated skip).
- Manual live smoke test: all 8 steps across two build configurations
  (default + `-DGTE_ENABLE_PROJECT_PANEL=OFF`) passed exactly as specified,
  including two `/get_swapchain` screenshots visually confirmed by direct
  image inspection.
- `git_status` confirmed only the one test-file fix was uncommitted going
  into this phase's own final commit; `.gitignore` confirmed all three
  build folders are already excluded.

## Exact state left in

- Modified: `tests/Game/GameEntityCommandsTests.cpp` (the dangling-reference
  fix, Section 3.2), `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md`
  (Section 3.5.1's addendum, pure append).
- Created: `task_manager/doc-refactor-1/PHASE5_COMPLETION_REPORT.md` (this
  file), `task_manager/doc-refactor-1/CAMPAIGN_COMPLETION_REPORT.md`.
- Final `README.md` is 13.8 KB / `AGENTS.md` is 11.4 KB (down from the
  original ~133 KB / ~155 KB baselines).
- Final `docs/` tree: `docs/README.md`, `docs/CHANGELOG.md`,
  `docs/architecture/` (6 files), `docs/conventions/` (12 files) — 20 files
  total.
- All three build configurations (`build/`, `build-editor-off/`,
  `build-project-panel-off/`) are green, zero regressions, as of the end of
  this phase.
- Branch remains `fix/doc-refactor` throughout.
- This is the final phase of the `doc-refactor-1` campaign.
