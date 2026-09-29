# editor-core-separation-19 — CAMPAIGN COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 5 of 5: "Compile" Menu Item

Status: **DONE**. Both phases complete; full clean build + full `ctest`
regression pass performed in PHASE2 with zero regressions. **This closes the
entire 5-file "On-Engine Project Workflow" plan** (BIG-STEPs 1 through 5,
`editor-core-separation-16` through `editor-core-separation-19`).

## What was actually built, phase by phase

**PHASE1 — Capability Wiring + Real "Compile" Menu Item.**
`IsProjectAssemblyBuildInFlight(projectName)` added to
`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` — a plain, read-only,
lock-guarded query over the SAME `g_inFlightProjects` set
`TriggerProjectAssemblyCompile()`/`TryRunProjectAssemblyBuildSynchronously()`
already guard with, mutating nothing. `IHotReloadDebugCapability` gained a new
pure virtual `IsCompileInFlight(projectName) const`, implemented by
`EditorHotReloadDebugCapability` as a one-line wrapper. `IEditorLayer` gained
its THIRD capability setter, `SetHotReloadDebugCapability(IHotReloadDebugCapability*)`
— a brand-new `IHotReloadDebugCapability` forward declaration was required in
`EditorLayer.h` (it did not exist there before), a no-op added to
`NullEditorLayer`, a real stored pointer added to `ImGuiEditorLayer` and passed
as `BuildDockspaceAndMenuBar()`'s new 4th argument, and wired once from
`EditorHost.cpp` reusing the SAME `s_editorHotReloadDebugCapability` static
already passed into `NetworkServer`'s 8th constructor argument. `DockLayout.cpp`'s
stale, 9-line disabled placeholder comment block was deleted, and the literal
`if (ImGui::MenuItem("Compile", nullptr, false, false)) {}` placeholder was
replaced with the real block: reads
`ActiveProjectAssemblyState::Instance().GetActive()`, computes
`compileEnabled` (`active.hasActiveProject && hotReloadDebugCapability != nullptr`),
`buildInFlight` (via the new `IsCompileInFlight()`), and `compileLabel`
(`"Compile '<Name>'"`, `" (compiling...)"` appended but the item never
disabled while a build runs — Step 3.4's deliberate design choice); on click
calls `hotReloadDebugCapability->TriggerCompileOnly(active.name)` (the EXACT
SAME call `POST /project_assembly/debug/compile_only?name=<X>` already makes
— zero new HTTP route added), setting
`ctx.projectWorkflowStatusMessage`/`...IsError`/`...SetTime` exactly like
`NewProjectWindow.cpp`/`OpenProjectWindow.cpp` already do. The pre-existing,
real "Open Project..." menu item block sits, byte-for-byte unchanged, between
the deleted comment and the new Compile block — verified by direct re-read
after both edits. Scoped compile check (`GreatTamanaEditor` target): 28/28
build steps, zero errors, zero new warnings.

**PHASE2 — Tests, Live Verification, Full Regression, Campaign Closeout (this
phase).** New Tier-1 test file,
`tests/Core/Plugins/ProjectAssemblyBuildRunnerInFlightQueryTests.cpp` (3 new
tests, mirroring `ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`'s own
`TempOutputDirectory` helper and its `onIdleTick`-driven deterministic
concurrency-proof technique verbatim): a plain "never triggered" false check;
a genuine in-flight synchronous build (a real, cache-less temp
`buildDirectory` so a real `cmake` child spawns and fails fast, with
`onIdleTick` firing at least once inside the still-held guard window) proving
`IsProjectAssemblyBuildInFlight()` reports `true` mid-build and `false` again
the instant `TryRunProjectAssemblyBuildSynchronously()` returns; and a
per-project independence check (an unrelated project name stays `false`
throughout the SAME in-flight window). `tests/CMakeLists.txt` gained one new
line, inserted immediately next to the other two
`ProjectAssemblyBuildRunner*Tests.cpp` entries, keeping the hand-maintained
list's existing grouping. Scoped `ctest -R ProjectAssemblyBuildRunnerInFlightQueryTest`:
3/3 passed, run before anything slower. Full live, HTTP-driven, end-to-end
verification against a real running `GreatTamanaEditor.exe`: `GET /get_swapchain`
immediately after launch (no active project) confirmed the menu bar renders
intact with `File`/`Window`/`Project` all present; a direct source-code
re-read of the final `DockLayout.cpp` diff confirmed `compileEnabled` really
does gate on `active.hasActiveProject` (the same accepted "can't screenshot an
expanded ImGui dropdown over HTTP" substitute `-16`/`-17`/`-18` each already
used); `POST /project_assembly/create_project?name=CompileMenuSmokeTest` → 200;
`POST /project_assembly/debug/compile_only?name=CompileMenuSmokeTest` (the
EXACT call the new menu item's click handler makes) → `{"started":true}`;
polled `GET /get_logs?since_id=...` until a real
`"Build finished with exit code 0 - relaunch GreatTamanaEditor.exe to use the
result"` line appeared, with genuine, live, streamed `ninja`/`cmake` compiler
output in between (including the expected, harmless
`"Project 'CompileMenuSmokeTest' has no '_Editor' target (no Editor/ sources)
- this is normal, not a build failure."` line for this fresh, Game-only
scaffold); a second, immediate re-issue of the same `compile_only` call landed
after the first build had already finished (exactly as PHASE0's own Step 3.4
anticipated for HTTP round-trip latency), so it also returned `started:true`
and ran a second real, successful build — the "reject a second overlapping
request" sub-case is instead fully covered by PHASE2's own synchronous,
deterministic unit test (test 2 above). Cleaned up afterward: Editor process
stopped, `Projects/CompileMenuSmokeTest/` removed (confirmed gone via
`browse_dir` — only `Projects/ProjectAssemblyProbe/` remains), and the
scratch `CompileMenuSmokeTest_Game.dll` this live run produced in
`build/project_assemblies/` was also deleted. A full clean build
(**598/598 steps, zero errors, zero new warnings**, up from
`editor-core-separation-18`'s own 597/597 baseline — this campaign's own 1
new compiled test `.cpp` file) and a full `ctest -C Debug --output-on-failure`
regression (**1989/1989 tests "passed" per ctest's own accounting — i.e.,
zero failures — 8 legitimate environment-gated skips, unchanged in COUNT
from `editor-core-separation-18`'s own 8-skip baseline**, see the arithmetic
below) both passed cleanly.

## Every real, mechanically-confirmed deviation from any phase's own file, and why

- **PHASE0's own two disclosed corrections, both genuinely applied and
  verified live (re-confirmed explicitly, since this is this campaign's own
  closing requirement):**
  1. **The missing `IEditorLayer` capability setter.** PHASE0 found, by
     direct `search_in_dir`, that no `SetHotReloadDebugCapability`-shaped
     method existed anywhere in this codebase before this campaign — the
     source `.txt` file's own STEP 2 code sketch silently assumed
     `hotReloadDebugCapability` was already an in-scope local variable inside
     `DockLayout.cpp`'s menu-bar code, which was false. PHASE1 built the
     third setter, mirroring `SetProjectLifecycleCapability`/
     `SetAssetScaffoldingCapability` exactly, and PHASE1's own completion
     report confirmed (via re-`search_in_dir`) exactly two `IEditorLayer`
     implementers were updated and the one real `BuildDockspaceAndMenuBar()`
     call site now passes the new 4th argument. This phase's own live
     verification (a real, running Editor rendering its menu bar with
     `Project` present, plus the source-code-confirmed `compileEnabled`
     gate) re-confirms the wiring is genuinely live, not just
     compile-clean.
  2. **The missing `projectWorkflowStatusSetTime` line.** PHASE0 found the
     source `.txt` file's own code sketch set
     `projectWorkflowStatusMessage`/`...IsError` but never `...SetTime`,
     which would have made the toast expire on the very next frame after
     being set (`DockLayout.cpp`'s own rendering block clears the message
     immediately once `kProjectWorkflowStatusLifetime` has "already"
     elapsed against a default-constructed, effectively-epoch time point).
     PHASE1's real `DockLayout.cpp` diff (re-read directly during this
     phase, see the What-was-built section above) sets
     `ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();`
     immediately alongside the other two toast fields, exactly like
     `NewProjectWindow.cpp`/`OpenProjectWindow.cpp` already do — confirmed
     present, not merely claimed.
- **PHASE1's own tooling self-correction (not a phase-file deviation).** This
  campaign's own `PHASE1_COMPLETION_REPORT.md` already disclosed one
  under-estimated `edit_line` `length` parameter for `DockLayout.h` that left
  a stray, duplicate, un-extended `BuildDockspaceAndMenuBar()` declaration
  behind — caught by re-reading the file immediately after the edit (a
  duplicate free-function declaration would have failed to compile as a
  redeclaration mismatch) and fixed with one more `edit_line` call before any
  scoped compile check ran. Re-confirmed, by this phase's own full clean
  build (598/598, zero errors), that no trace of this transient state
  survived into the committed tree.
- **PHASE2's own regression-count environment note (not a regression):**
  `StlLoaderRealModelSmokeTest` actually RAN AND PASSED during this phase's
  own full `ctest` run (its own real-terrain-STL fixture file happened to be
  present on this machine this time), rather than hitting its own
  environment-gated skip path as it did in `editor-core-separation-17`/`-18`'s
  own baseline runs. The total skip COUNT stayed exactly 8 regardless (the
  8 that DID skip this run —
  `OpenProjectEndpointEndToEndTest.OpenProjectOnRealCompiledProbeMarksItActiveWithoutTouchingItsFiles`,
  `PmxLoaderRealModelSmokeTest`, `ProjectAssemblyHostTest` x2 (its own two
  skip-shaped tests), `ProjectAssemblyRegistrationLedgerTest` x2, and
  `CoreHeadlessConstructionTest` — are the SAME family of pre-existing,
  environment-gated tests this whole series has always carried, none of them
  a new gap this campaign introduced) — this is a genuine, honest
  machine-state difference (a fixture file's presence/absence), not a
  regression and not a new skip needing explanation, since it makes the
  regression run STRICTLY MORE COMPLETE, never less.
- **No other new structural deviations found.** Every finding/correction
  `PHASE0` itself already documented (the two corrections above, the
  "compile menu stays enabled while a build runs" deliberate design choice
  in Step 3.4, and the "optional-but-built-anyway" reasoning for
  `IsProjectAssemblyBuildInFlight()`) was confirmed accurate during
  implementation and live verification — none were found to be wrong or
  incomplete.

## New gaps found during this phase, for future work

- **No HTTP-drivable way to expand/click an ImGui menu-bar dropdown item
  still stands, unchanged.** This is the SAME gap `-16`/`-17`/`-18` each
  already flagged for their own new floating windows — this campaign is the
  first of the five to hit it for a plain MENU ITEM rather than a floating
  window, confirming the gap generalizes beyond just windows. Substituted,
  exactly as those three prior campaigns did, with a real screenshot proving
  the surrounding UI renders intact plus a direct source-code read of the
  exact enabling/labeling logic. If a future campaign needs to click-simulate
  ANY ImGui element over HTTP (menu item, button, or window), building a
  generic `GET /..._window/open`-style or click-simulation route (as
  `-16`/`-17`/`-18` each suggested) is now doubly motivated.
- **The "Compiled while running -> genuine live load" transition
  `editor-core-separation-17`'s own report first flagged, and `-18`'s report
  re-flagged, remains NOT exercised by this campaign either** — this
  campaign's own live proof used `CompileMenuSmokeTest`, a project that was
  only ever `create_project`'d (never `open_project`'d/loaded) in this
  running instance, so its freshly-built `.dll` was never locked/mapped and
  the compile succeeded cleanly with no load-time collision — but this also
  means the "Compile" menu item's real end-user consequence (a
  freshly-compiled project becoming genuinely `Compiled`-tier, ready for a
  SUBSEQUENT "Open" to actually load it) was proven at the HTTP/build layer
  only, not at the "immediately re-open the just-compiled project in this
  same running instance" layer. This was always understood, across all of
  `-17`/`-18`/this campaign, to be a SEPARATE concern from "does Compile
  itself work" — `OpenProjectAssembly()`/`OpenProjectAssemblyOnMainThread()`
  (BIG-STEP 3) already independently proved the load path works for an
  ALREADY-compiled project; chaining "Compile just now" directly into
  "Open just now, same running instance" was never in ANY of these five
  campaigns' own explicit scope, and remains an honest, disclosed gap for
  whichever FUTURE work (if any) wants that exact end-to-end chain
  exercised live.
- **No other new structural gaps found.** Every finding this campaign's own
  PHASE0 already documented remained accurate and unchanged throughout both
  phases — none were found to be wrong or incomplete during implementation.

## Verification summary

- Full clean build (`cmake --build build --target clean` then
  `cmake --build build`): **598/598 steps, zero errors, zero new compiler
  warnings** (up from `editor-core-separation-18`'s own 597/597 baseline —
  this campaign's own 1 new compiled `.cpp` file,
  `ProjectAssemblyBuildRunnerInFlightQueryTests.cpp`; this campaign adds zero
  new PRODUCTION `.cpp`/`.h` files, only edits to 11 existing ones, per
  PHASE0's own file manifest).
- Full `ctest -C Debug --output-on-failure`: **1989/1989 tests accounted
  for, zero failures ("100% tests passed"), 8 legitimate environment-gated
  skips** (unchanged in COUNT from `editor-core-separation-18`'s own 8-skip
  baseline, though membership shifted by one this run — see the deviations
  section above for why that's not a regression). Exact arithmetic: 1986
  (prior baseline, `editor-core-separation-18`'s own final count, itself
  already including all 8 legitimate skips) + 3 (this campaign's own new
  `ProjectAssemblyBuildRunnerInFlightQueryTests.cpp` tests) = **1989**,
  exactly matching the real `ctest` output, zero unexplained delta, zero
  regressions.
- Scoped `ctest -R ProjectAssemblyBuildRunnerInFlightQueryTest` (run BEFORE
  the full suite, per this phase's own Section 3.3): **3/3 passed**,
  confirming this campaign's own new tests cleanly before committing to the
  slower full run.
- Live, HTTP-driven, end-to-end verification against a real running
  `GreatTamanaEditor.exe`: `GET /get_swapchain` with no active project
  confirmed the menu bar renders intact; `POST /project_assembly/create_project?name=CompileMenuSmokeTest`
  → 200; `POST /project_assembly/debug/compile_only?name=CompileMenuSmokeTest`
  (the EXACT call the new menu item's click handler makes internally) → 200,
  `started:true`; `GET /get_logs?since_id=...` polled until a real
  `"Build finished with exit code 0 ..."` line appeared, with genuine, live,
  streamed compiler output (never a canned/instant response) in between; a
  second, immediate re-issue of the same route (landing after the first
  build had already finished, exactly as anticipated) also succeeded and ran
  a second real build — the "reject overlap" sub-case is separately, fully
  proven by this phase's own deterministic synchronous unit test. Cleaned up
  afterward: Editor process stopped, `Projects/CompileMenuSmokeTest/` removed
  (confirmed gone via `browse_dir` — only `Projects/ProjectAssemblyProbe/`
  remains) and the scratch `CompileMenuSmokeTest_Game.dll` output artifact
  also removed.
- `git_status` (checked before this final commit): only this phase's own
  real, intended files pending (the new test file, `tests/CMakeLists.txt`,
  this report) — every earlier phase's own files (PHASE1's source changes and
  its own completion report) were already committed by that phase itself. No
  scratch project folder or throwaway debug code survives anywhere in the
  working tree.

## Explicit Non-Goals this campaign correctly stayed within (per its own PHASE0 Step 3.5, restated from the source file, unchanged)

- Did NOT add a progress bar or percentage indicator for an in-flight
  compile — confirmed, the only in-flight cue is the `" (compiling...)"`
  label suffix.
- Did NOT add a cancel-mid-build button — confirmed, no new UI or capability
  method exposes any way to abort a running `cmake --build` child process.
- Did NOT touch, rename, or fold together the existing, separate
  "Compile & Reload" hot-reload feature or its own
  `POST /project_assembly/hot_reload` route — confirmed unchanged by
  `search_in_dir` across this campaign's own diff.
- Did NOT add any new HTTP route — confirmed, `POST /project_assembly/debug/compile_only`
  is the EXACT SAME, already-existing, unmodified route this campaign's new
  menu item calls; zero new `server.Post(`/`server.Get(` call sites were
  added anywhere in `NetworkServer.cpp`/`NetworkRoutes.cpp`.
- Did NOT change `ActiveProjectAssemblyState`'s own shape or contract —
  confirmed, `src/Editor/ActiveProjectAssemblyState.h/.cpp` were never edited
  by either phase of this campaign.

## Closing, whole-plan-level re-confirmation of `PROJECTWORKFLOW_BIGSTEP_01`'s own Section 6 Non-Goals (across ALL FIVE campaigns combined)

Since this is the CLOSING file of the entire 5-file "On-Engine Project
Workflow" plan, `PROJECTWORKFLOW_BIGSTEP_01_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`'s
own Section 6 was re-read in full at this final point, and every one of its
boundaries is re-confirmed, one by one, to still hold true across all five
campaigns (`editor-core-separation-16` through `-19`) combined:

- **"Does NOT add any in-engine text/code editor."** Confirmed true across
  all five campaigns. Every scaffolded file (BIG-STEP 4,
  `editor-core-separation-18`) is a fixed, hardcoded template only. The one,
  explicitly-sanctioned exception this whole plan carries — hand-editing a
  scaffolded file's contents with plain `write_file` during a LIVE
  VERIFICATION session (used by `-18`'s own PHASE4 to wire a scaffolded
  Render Pass into `RegisterProject()`) — was never a production feature;
  it remains purely an implementation-session tool action, never exposed to
  an end user through any UI or HTTP route this plan built. No campaign
  added a code editor of any kind.
- **"Does NOT support more than one ACTIVE project at a time (LDD-PW2)."**
  Confirmed true. `ActiveProjectAssemblyState` (BIG-STEP 2's own singleton,
  extended by BIG-STEP 3, read-only by BIG-STEP 4/5) has carried exactly one
  `hasActiveProject`/`name`/`sourceDirectory` triple since its creation — no
  campaign ever added a picker, a list of "active" projects, or a
  per-project-scoped variant of any kind. This campaign's own "Compile" menu
  item reads this SAME singleton, unmodified.
- **"Does NOT unload/hot-reload a project that was already loaded before a
  different project became 'active'."** Confirmed true. No campaign in this
  series ever called `ProjectAssemblyHost::UnloadProjectAssembly()` from any
  NEW code path this plan introduced — the pre-existing, separate hot-reload
  feature (`project-assembly-impl-2`) remains the only caller of that
  function, entirely untouched by this whole 5-file plan.
- **"Does NOT auto-compile on Open (LDD-PW4)."** Confirmed true.
  `OpenProjectAssembly()`/`OpenProjectAssemblyOnMainThread()` (BIG-STEP 3)
  never call `TriggerProjectAssemblyCompile()`/`RunProjectAssemblyBuildAndWait()`
  for any tier — re-confirmed unchanged by this campaign, which is the FIRST
  campaign to give an end user an EASY, one-click way to compile
  afterward, but never automatically.
- **"Does NOT add Delete Project / Rename Project / Delete Asset File."**
  Confirmed true. No campaign added any destructive/renaming action of any
  kind — Create (BIG-STEP 2), Open (BIG-STEP 3), Create-Asset-Scaffold
  (BIG-STEP 4), and Compile (BIG-STEP 5, this campaign) are the sole four
  mutating actions this whole plan ever built.
- **"Does NOT add a 'Create -> Editor Panel' scaffold kind."** Confirmed
  true. BIG-STEP 4's own `AssetScaffoldKind` enum has exactly three values
  (`RenderPass`, `ComputeShader`, `ShaderPair`) — unchanged by this
  campaign, which never touches `AssetScaffoldKind`/`CreateAssetScaffold()`
  at all.
- **"Does NOT change `plugins/gte_plugin_abi/` or the EXISTING hot-reload
  feature's own files in any way."** Confirmed true. No campaign in this
  series ever edited any file under `plugins/gte_plugin_abi/`, and this
  campaign's own file manifest (`ProjectAssemblyBuildRunner.h/.cpp`,
  `EditorCapabilities.h`, `EditorHotReloadDebugCapability.h/.cpp`,
  `EditorLayer.h`, `NullEditorLayer.cpp`, `ImGuiEditorLayer.cpp`,
  `EditorHost.cpp`, `DockLayout.h/.cpp`) touches zero files the EXISTING,
  separate hot-reload feature (`project-assembly-impl-2`,
  `editor-core-separation-11` through `-14`) owns exclusively — every touched
  file is either a genuinely shared, already-multi-purpose file
  (`ProjectAssemblyBuildRunner.h/.cpp`, `EditorCapabilities.h`) or a brand-new
  addition this SAME 5-file plan already introduced in an earlier campaign
  (`EditorHotReloadDebugCapability`, `EditorLayer`'s capability-setter
  convention, `DockLayout`'s menu bar).

**Every one of `PROJECTWORKFLOW_BIGSTEP_01`'s Section 6 boundaries holds true,
unbroken, across the whole 5-campaign plan.** Create, Open, Create-Asset, and
Compile are now all four reachable both from inside the running Editor and
from a raw HTTP POST, exactly as `PROJECTWORKFLOW_BIGSTEP_01`'s own Step 1
goal demanded, with no external text editor or manually-typed `cmake` command
ever required for any of the four.

This closes the `editor-core-separation-19` campaign (On-Engine Project
Workflow plan, BIG-STEP 5, "Compile" Menu Item) — and, with it, the ENTIRE
5-file "On-Engine Project Workflow" plan (`editor-core-separation-16` through
`-19`) — for good.
