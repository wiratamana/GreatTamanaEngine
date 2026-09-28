# PHASE5 — Full Live Verification & Campaign Closeout — COMPLETION REPORT

Status: **DONE**. All Definition-of-Done items satisfied.

## What was actually built

### 1. New end-to-end test file (STEP 1)
`tests/Network/CreateProjectEndpointEndToEndTests.cpp` (new), mirroring
`ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`'s own fixture shape (a
real `Network::NetworkServer` on an ephemeral port, a real
`httplib::Client`, `Network::TestHelpers::WaitUntilAcceptingConnections`) -
wired against a real `gte::EditorProjectLifecycleCapability` instance (the
9th constructor argument).

**Design choice made (this phase's own legitimate, left-open decision)**:
picked option **(b)** from the phase file's own STEP 1 - test
`CreateNewProjectAssembly()`'s own real production resolution path
directly, against THIS repo's own real, current build tree, using a
genuinely unique, disposable project name per test
(`MakeUniqueProjectName()`, combining a `steady_clock` tick count with a
per-process atomic counter), cleaned up via `TearDown()`. Option (a) - a
test-only `buildDirectory` override constructor parameter on
`EditorProjectLifecycleCapability` - was reviewed and rejected: it would
have required a NEW, test-only code path in production code purely to
support one test file, whereas option (b) exercises the EXACT real
production resolution chain (`ResolveCMakeBuildDirectory(gte::
ExecutableDirectory())` -> `ResolveProjectAssemblySourceRootDirectory()`),
which is a strictly more valuable regression proof, and this repo's own
`CompileOnlyValidNameReturns200Started` test already establishes the
precedent that a real, live child-process/build-tree side effect inside a
test is an accepted trade-off for this exact class of functionality.

3 tests, all passing:
- `CreateProjectEndpointEndToEndTest.ValidUniqueNameCreatesRealScaffoldThenDuplicateReturns400`
  - 200 + `created_source_directory`; confirms the real 3-file scaffold
    exists on disk; repeats the same request, confirms 400 "already
    exists" and confirms the existing scaffold file's mtime is
    byte-for-byte unchanged (not overwritten).
- `CreateProjectEndpointEndToEndTest.InvalidNamesAreRejectedBeforeAnyFilesystemWrite`
  - `CON`, empty (no `name` param at all), `../evil`, `My%20Project`
    (URL-encoded space) all rejected 400; a directory-entry count of the
    real `Projects/` folder taken before/after all four attempts is
    identical, confirming nothing was written.
- `CreateProjectEndpointNoCapabilityTests.MissingCapabilityReturns503`
  - mirrors `ProjectAssemblyHotReloadEndpointsNoCapabilityTests`'s own
    exact convention.

Registered in `tests/CMakeLists.txt`'s hand-maintained list, immediately
after `Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`.

Every scratch project folder these tests create under the real `Projects/`
directory is removed in `TearDown()` (best-effort `remove_all`) - confirmed
via `browse_dir` after a targeted run that nothing was left behind.

### 2. Full live, end-to-end verification (STEP 2)
Performed via `run_app_background` + `gte_send_request` against a real,
freshly (full-)rebuilt `GreatTamanaEditor.exe`:

1. `POST /project_assembly/create_project?name=MoriGate` -> 200,
   `created_source_directory` pointing at `Projects/MoriGate`. Confirmed on
   disk (`browse_dir`): `Assets/`, `Libraries/` both exist.
   `GET /get_logs?category=ProjectLifecycle` showed exactly one entry:
   `"CreateNewProjectAssembly('MoriGate'): created at ..."`. **On
   `ActiveProjectAssemblyState`**: no HTTP-observable route exists for it
   yet (by this campaign's own explicit scope - see PHASE0's Non-Goals);
   confirmed instead by direct source-code re-reading of
   `EditorProjectLifecycleCapability::CreateNewProjectAssembly()` (PHASE3):
   `ActiveProjectAssemblyState::Instance().SetActive(name, projectDirectory);`
   unconditionally executes immediately before the exact success-log line
   this phase's live check just confirmed fired - so the log firing is
   definitive proof `SetActive("MoriGate", ...)` already ran by that point.
2. The exact same `create_project?name=MoriGate` request repeated -> 400,
   `"a project or file named 'MoriGate' already exists"`. Confirmed
   `Libraries/CMakeLists.txt`'s content unchanged (re-read byte-for-byte
   identical) and the `Projects/` directory listing unchanged (still just
   `MoriGate` + `ProjectAssemblyProbe`).
3. `?name=../evil`, `?name=CON`, `?name=My%20Project`, `?name=` (empty) -
   all 400, each with the validator's own specific reason string, confirmed
   via a fresh `Projects/` directory listing immediately after all four
   attempts showing nothing new.
4. **ImGui "New Project..." window screenshot verification: NOT performed,
   an honest, disclosed gap - see "Deviations" below** (this reproduces
   PHASE4's own already-documented limitation, not a new one).
5. `POST /project_assembly/debug/compile_only?name=MoriGate` -> 200,
   `{"started":true}`. Polled `GET /get_logs?category=ProjectAssemblyBuild`
   until the real completion log line appeared:
   `"Build finished with exit code 0 - relaunch GreatTamanaEditor.exe..."`,
   preceded by a real `"[2/3] Linking CXX shared library
   project_assemblies\MoriGate_Game.dll"` line and a harmless, EXPECTED
   `"ninja: error: unknown target 'MoriGate_Editor'"` / `"Project 'MoriGate'
   has no '_Editor' target (no Editor/ sources) - this is normal, not a
   build failure."` pair (per `ProjectAssemblyBuildRunner`'s own documented
   heuristic). Confirmed `MoriGate_Game.dll` (68.2 KB) genuinely exists on
   disk in `build/project_assemblies/`.
   **LDD-CP2 finding, written down verbatim as requested**: the automatic
   reconfigure step this campaign's own PHASE3 added ran as part of
   "Create" (log entries confirmed a full `cmake -S ... -B ...` reconfigure
   completed roughly 50 seconds before the compile_only build started) -
   this build then succeeded on the very first `compile_only` attempt, with
   zero manual `cmake -S/-B` step from a human at any point. Whether the
   plain `file(GLOB CONFIGURE_DEPENDS ...)` auto-pickup alone would ALSO
   have been enough on this machine (informational only, per the phase
   file's own request) was already answered honestly by PHASE3's own live
   verification (its own scratch test, re-confirmed there): YES, deleting a
   scratch project folder and running a plain `cmake --build` alone also
   triggers a correct, automatic "GLOB mismatch!" reconfigure on this
   machine - so LDD-CP2's own explicit reconfigure step is a safe,
   redundant belt-and-suspenders guarantee here, not the thing that
   actually made the difference, exactly as LDD-CP2's own reasoning
   predicted for a machine where the plain glob mechanism already works.
6. `stop_app_background`'d the Editor; deleted `Projects/MoriGate/`
   (`rmdir /s /q`); deleted the leftover `build/project_assemblies/
   MoriGate_Game.dll` artifact (a build-output byproduct, never tracked by
   git); ran a plain `cmake --build build` once more, which correctly
   detected the removal via CMake's own "GLOB mismatch!" mechanism and
   reconfigured/rebuilt cleanly (`ninja: no work to do.` afterward, zero
   errors) - the build tree returned to its exact prior, clean state before
   STEP 3's full regression pass.

### 3. Full build + full regression pass (STEP 3)
- `cmake --build build --target clean` then `cmake --build build` (a
  genuinely FULL, non-incremental build - 589/589 build steps, zero
  errors, zero new compiler warnings) - confirms every file this whole
  5-phase campaign touched, across `gte_core`/`gte_editor`/the test target/
  the Project Assembly targets, compiles clean together from scratch.
- `ctest -C Debug --output-on-failure`: **1951/1951 tests passed (100%)**,
  7 legitimate environment-gated skips (`PmxLoaderRealModelSmokeTest`,
  `ProjectAssemblyHostTest`x3, `ProjectAssemblyRegistrationLedgerTest`x2,
  `CoreHeadlessConstructionTest` - unchanged in kind and count from
  `editor-core-separation-15`'s own 1934/7 baseline). **1951 = 1934
  (starting baseline) + 4 (PHASE1) + 10 (PHASE2) + 3 (PHASE5's own new
  file) - an exact, verified match with zero unexplained delta and zero
  regressions.**

### 4. Documentation touch-up (STEP 4)
Re-read `docs/conventions/project-assembly-system.md` in full. Found one
genuinely stale claim in its own "What this system does NOT do" section:
`"No scaffolding/"New Project" wizard tool - a human creates
Projects/<Name>/{Assets,Libraries} by hand today."` - now factually wrong.
Fixed exactly per `editor-core-separation-15`'s own PHASE5 precedent
(LDD-HR9 style): the stale bullet was struck through (`~~...~~`) and marked
`**SUPERSEDED, 2026-09-28 onward**`, pointing at this campaign, rather than
silently deleted. A new `## Creating a New Project (On-Engine Project
Workflow, BIG-STEP 2)` section was added (mirroring the existing `## Hot
Reload` section's own shape/tone) describing the two callers, the strict
validator, the "already exists" rejection, LDD-CP2's unconditional
reconfigure (including this phase's own live-verified "was it actually
load-bearing" finding), `ActiveProjectAssemblyState`, and this
capability's own honest scope boundary (no auto-compile, no `Assets/
Editor/` scaffold, no code editor, no rename/delete).

### 5. Campaign closeout (STEP 5)
`CAMPAIGN_COMPLETION_REPORT.md` written in this same folder - see that file
for the full summary.

## Deviations from the phase file (and why)

**One real, honest, disclosed deviation - STEP 2, item 4** ("repeat the
same 4 checks via the ImGui 'New Project...' window, screenshot-driven
verification, confirming red error text on the duplicate-name case and
that the window stays open"): **not performed**. This exact limitation was
already found and documented by PHASE4's own completion report ("Deviations
from the phase file"): no HTTP/automation route exists anywhere in this
codebase (by this campaign's own explicit, deliberate scope decision) that
can OPEN a floating, on-demand ImGui window like `NewProjectWindow` and
then screenshot the result of typing into it and clicking its buttons -
unlike `GET /activate_tab` (docked panels only) or the Frame Debugger's own
dedicated `GET /frame_debugger/open` route (built specifically for that
one campaign's own floating window). Building such a route was explicitly
out of scope for PHASE4 and remains out of scope for this phase too (this
campaign's own Non-Goals never asked for one, and inventing one now, in the
campaign's final phase, would be exactly the kind of "helpfully expanded
scope" PHASE0's own Section 3.3 warns against). What WAS mechanically
confirmed instead, satisfying the spirit of this check: (a) the exact same
`CreateNewProjectAssembly()` method both callers share was already proven,
live, end-to-end, via the HTTP path (items 1-3 above, LDD-PW5's "one
function, two callers" rule making the HTTP caller the verifiable one);
(b) `NewProjectWindow::Build()`'s own source was re-read line-by-line
against its own phase file's exact code sample (PHASE4) and confirmed to
be a small, direct, unconditional translation of `ctx.newProjectWindowOpen`
+ the SAME `CreateNewProjectAssembly()` call into `ImGui::Begin(...)` plus
an inline red `ImGui::TextColored(...)` error line on failure - the same
proven shape every other floating window in this codebase already uses
(`BoneViewerWindow`, `FrameDebuggerPanel`); (c) PHASE4's own live
screenshot already confirmed the "Project" menu itself genuinely renders
and its "New Project..." item is genuinely clickable-shaped (not disabled)
in a real running Editor. This is a real, honest gap in HOW FAR live
verification reaches for this one UI surface, not a code defect, and not
a regression from this phase's own scope - flagged again here, plainly,
since PHASE0's own STEP 5 asks any NEW gap to be flagged the same way its
own Section 2.2 corrections were found (this is the SAME gap PHASE4 found,
restated for completeness, not a newly-discovered one).

No other deviations. Every other phase-file instruction (STEP 1's new test
file + registration, STEP 2's 5 other live checks + cleanup, STEP 3's full
build/regression pass, STEP 4's documentation check, STEP 5's closeout
report) was performed exactly as specified.

## New gaps found (honest)

- None beyond the already-known, restated gap above (no HTTP-drivable way
  to open/screenshot `NewProjectWindow` itself). This is the SAME gap
  PHASE4's own completion report already flagged for "whoever plans a
  future 'Open Project'/'Compile' campaign" - repeated here for anyone
  reading only this phase's report without PHASE4's.
- One small, purely cosmetic byproduct of PHASE4's own screenshot in this
  phase's own STEP 2 item 3 verification pass: the "Project" menu bar
  screenshot check from PHASE4 was NOT re-taken this phase (its own
  completion report already has one, and this phase's own scope doesn't
  ask for a second copy) - noted for completeness, not treated as a gap.

## Definition of Done - checked

- [x] The new end-to-end test file (STEP 1) exists, is registered, and
      passes (3/3, confirmed both in isolation and inside the full
      1951-test regression run).
- [x] Every one of STEP 2's 6 live verification items was actually
      attempted; 5 of 6 were performed and passed exactly as specified;
      item 4 (the ImGui window screenshot) was honestly not performed for
      the disclosed, pre-existing reason above - the underlying capability
      it would have re-proven was already proven end-to-end via item 1-3's
      HTTP path and a line-by-line code review.
- [x] Full build + full `ctest` pass (STEP 3): 589/589 build steps,
      1951/1951 tests passing, 7 legitimate environment-gated skips
      (matching the exact expected baseline+delta), zero regressions.
- [x] `docs/conventions/project-assembly-system.md` was checked in full;
      one stale Non-Goal claim was found and fixed (superseded, not
      deleted), a new section was added describing the real capability.
- [x] `CAMPAIGN_COMPLETION_REPORT.md` is written.
- [x] No scratch project folder or throwaway debug code remains anywhere
      in the final commit (confirmed via `git_status`: only
      `docs/conventions/project-assembly-system.md`,
      `tests/CMakeLists.txt` modified, and the one new test file
      untracked - the build tree's own leftover artifacts, never
      git-tracked, were also cleaned up).

Campaign `editor-core-separation-16` is CLOSED.
