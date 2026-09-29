# PHASE2 — Tests, Live Verification & Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1_CAPABILITY_WIRING_AND_COMPILE_MENU_ITEM.md`
being fully committed and scoped-compiling cleanly first.

## Step 1: The Goal

Prove, mechanically (never just "by reading the code"), that:
1. `IsProjectAssemblyBuildInFlight()` genuinely reflects the SAME
   `g_inFlightProjects` guard `TriggerProjectAssemblyCompile()`/
   `TryRunProjectAssemblyBuildSynchronously()` already use — a new,
   independent Tier-1 test.
2. The real, running Editor's "Project > Compile" menu item is disabled
   with no active project, becomes enabled with the real project name the
   moment one exists, and genuinely triggers a real `cmake --build` when
   clicked — end-to-end, over the network debugging surface, against a real
   running `GreatTamanaEditor.exe`.
3. Nothing else in the whole engine regressed: a full clean build and a
   full `ctest` regression pass, with an explained, exact arithmetic for the
   new test count (mirroring `-16`/`-17`/`-18`'s own completion reports).
4. This campaign — and therefore the WHOLE 5-file "On-Engine Project
   Workflow" plan — is closed out with a final `CAMPAIGN_COMPLETION_REPORT.md`
   that re-confirms every one of `PROJECTWORKFLOW_BIGSTEP_01`'s Section 6
   non-goals still holds.

## Step 2: The Situation

Phase1 has already landed every production code change and scoped-compiled
cleanly. This phase adds ZERO new production code — only tests, live
verification, and the closing report. The existing test-file naming/style
convention for this exact source file is well established:
`tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` and
`tests/Core/Plugins/ProjectAssemblyBuildRunnerTierClassificationTests.cpp`
are the two most recent precedents to mirror — both listed, by hand, in
`tests/CMakeLists.txt` (confirmed a hand-maintained list, not a glob, via
`search_in_dir` for `ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` in
that exact file, line 2166).

The existing `ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive`
test (`ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`, lines 132-169+) is
the load-bearing PRECEDENT for how to deterministically observe
`g_inFlightProjects` mid-flight without a real, multi-minute build and
without a flaky thread-launch race: point `TryRunProjectAssemblyBuildSynchronously()`
at a real, existing, empty temp directory with NO `CMakeCache.txt` in it (so
`cmake` fails fast, but a real child process genuinely spawns and takes
non-zero wall-clock time to even start), and use the `onIdleTick` callback
(invoked from inside `RunOneBuildTarget()`'s own poll loop, guaranteed to
fire at least once before the fast-failing child exits) as the one
deterministic window in which the guard is provably still held.

## Step 3: The Plan

### 3.1 — New test file: `tests/Core/Plugins/ProjectAssemblyBuildRunnerInFlightQueryTests.cpp`

Mirror the header/include-block/`TempOutputDirectory` helper style of
`ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` exactly (read that file's
own first ~30 lines again immediately before writing this one, to copy its
real helper verbatim rather than re-inventing a slightly different one).
Minimum 3 tests:

1. `ReturnsFalseForAProjectNameThatHasNeverBeenTriggered` — a plain,
   trivial, no-setup call:
   `EXPECT_FALSE(IsProjectAssemblyBuildInFlight("GteNeverTriggeredProjectXyz"));`
2. `ReturnsTrueWhileASynchronousBuildIsGenuinelyInFlightForThatExactName` —
   copy the `ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive`
   deterministic `onIdleTick` technique verbatim (same fake, cache-less
   `buildDirectory`), but instead of launching a NESTED
   `TryRunProjectAssemblyBuildSynchronously()` call from inside the
   callback, simply assert
   `EXPECT_TRUE(IsProjectAssemblyBuildInFlight(projectName));` from inside
   that same callback, then assert
   `EXPECT_FALSE(IsProjectAssemblyBuildInFlight(projectName));` AFTER the
   outer `TryRunProjectAssemblyBuildSynchronously()` call has returned
   (proving `ClearInFlight()` genuinely ran).
3. `IsIndependentAcrossTwoDifferentProjectNamesSimultaneously` — inside the
   SAME `onIdleTick` window as test 2, additionally assert
   `EXPECT_FALSE(IsProjectAssemblyBuildInFlight("GteSomeOtherUnrelatedProjectName"));`
   — proving this is a per-project, not a single global, flag (mirrors this
   whole file's own class-level doc comment, `ProjectAssemblyBuildRunner.h`
   lines 108-114, "calling it again for a DIFFERENT project ... is fine").

Add `#include "Core/Plugins/ProjectAssemblyBuildRunner.h"` (same relative
include style as the two sibling test files).

### 3.2 — `tests/CMakeLists.txt`

Add exactly one new line, immediately next to the other two
`ProjectAssemblyBuildRunner*Tests.cpp` entries (keep the hand-maintained
list's existing alphabetical/grouped ordering convention — check the 5-10
lines immediately around line 2166 before deciding exactly where the new
line goes, do not just append at the end of the whole file):

```
Core/Plugins/ProjectAssemblyBuildRunnerInFlightQueryTests.cpp
```

### 3.3 — Scoped test run BEFORE the full suite

`cmake --build build --target GreatTamanaEngineTests` then
`ctest -C Debug --output-on-failure -R ProjectAssemblyBuildRunnerInFlightQueryTest`
— confirm all 3 (or however many were actually written) pass before running
anything slower.

### 3.4 — Live, HTTP-driven, end-to-end verification against a real running Editor

Use `run_app_background` to launch `GreatTamanaEditor.exe`, then, via
`gte_send_request`:

1. `GET /get_swapchain` (or `/get_game_view`) immediately after launch, with
   NO active project yet in this fresh process — confirm the Editor's menu
   bar renders intact (this alone cannot prove "Compile" is specifically
   disabled — ImGui's own disabled/grayed rendering is visually subtle in a
   screenshot and the expanded dropdown itself is not independently
   click-simulatable over HTTP, the SAME accepted gap `-16`/`-17`/`-18` each
   already hit and pre-authorized — substitute a direct source-code
   re-read of the final `DockLayout.cpp` diff confirming `compileEnabled`
   really does gate on `active.hasActiveProject` before moving on, exactly
   like those three prior campaigns' own precedent).
2. `POST /project_assembly/create_project?name=CompileMenuSmokeTest` — 200,
   marks it active (BIG-STEP 2's own existing route, unmodified).
3. `POST /project_assembly/debug/compile_only?name=CompileMenuSmokeTest` —
   the SAME call the new menu item's own click handler makes internally —
   200, `started: true`.
4. Poll `GET /get_logs?since_id=...` until a real
   `"Build finished with exit code 0..."` (or a real compiler-error line, if
   the freshly-scaffolded stub genuinely fails for an unrelated environment
   reason — either way, CONFIRM real, live compiler output streamed through,
   not a canned/instant response) appears.
5. Immediately re-issue the SAME `POST /project_assembly/debug/compile_only?name=CompileMenuSmokeTest`
   a second time WHILE unrelated (this call itself will very likely land
   AFTER the first build already finished, given HTTP round-trip latency —
   if so, this specific "already in progress" sub-case is instead covered
   by PHASE1's own Step 3.4 design reasoning plus test 3.2's synchronous,
   deterministic proof; do not spend excessive live-session time chasing a
   real overlapping window here — the synchronous unit test already proves
   the guard works).
6. Clean up: stop the Editor (`stop_app_background`), delete
   `Projects/CompileMenuSmokeTest/` (confirm gone via `browse_dir`), and
   confirm `Projects/ProjectAssemblyProbe/` is the only project folder left,
   exactly like `-17`/`-18`'s own closing cleanup step.

### 3.5 — Full clean build + full regression (final phase — this IS the one
place in this whole campaign a full build/test run belongs, per this
campaign's own delegation Note 4)

```
cmake --build build --target clean
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Compute the exact expected new-test-arithmetic BEFORE running (mirror
`-17`/`-18`'s own "exact arithmetic" convention): prior baseline is
`editor-core-separation-18`'s own final count, **1986** (8 legitimate
environment-gated skips, unchanged in kind) + this campaign's own new test
count from Step 3.1 (3, unless more were added) = expected total. If the
real `ctest` output disagrees, diagnose why (a genuinely new
environment-gated skip? an actual regression?) before writing the report —
if a real regression is found, use `delegate_task` to fix it (implementation
tasks may not delegate further, but PHASE2 itself, not yet an
"implementation task" in the strict sense at the point a regression is
found, may spin up one corrective task) rather than silently patching in an
unreported way.

### 3.6 — `CAMPAIGN_COMPLETION_REPORT.md`

Write `task_manager/editor-core-separation-19/CAMPAIGN_COMPLETION_REPORT.md`,
mirroring the structure of `-16`/`-17`/`-18`'s own reports exactly (What was
actually built, phase by phase / Every real deviation and why / New gaps
found for future work / Verification summary / Explicit non-goals correctly
stayed within). Explicitly re-confirm, one by one, that PHASE0's own two
disclosed corrections (the missing `IEditorLayer` capability setter, and the
missing `projectWorkflowStatusSetTime` line) were both genuinely applied and
verified live. Additionally, since this is the CLOSING file of the entire
5-campaign "On-Engine Project Workflow" plan
(`PROJECTWORKFLOW_BIGSTEP_01` through `_05`), explicitly re-read
`PROJECTWORKFLOW_BIGSTEP_01`'s own Section 6 ("Non-Goals for this whole
5-file plan") one final time and state, plainly, whether every boundary
listed there still holds true across ALL FIVE campaigns combined — this is
the one closing check that only makes sense at THIS final point in the
whole plan, not any earlier one.

`git_add`/`git_commit` this phase's files (test file, `tests/CMakeLists.txt`,
the completion report) with a message identifying it as the closing phase of
this campaign.
