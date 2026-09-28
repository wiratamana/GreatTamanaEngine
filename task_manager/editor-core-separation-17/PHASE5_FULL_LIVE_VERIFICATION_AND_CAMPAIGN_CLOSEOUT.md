# PHASE5 — Full Live Verification & Campaign Closeout
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Depends on: PHASE1-4, all done and each individually committed.
Blocks: nothing — this is the last phase. Its own report is what the NEXT
campaign (BIG-STEP 4, "Create Script Asset") should read first.

This is the ONLY phase in this campaign allowed to run a full clean build
and the full `ctest` regression suite (per this whole task's own governing
Note 4).

---
## Step 1: The Goal

Mechanically prove every Definition-of-Done item the source document
(`PROJECTWORKFLOW_BIGSTEP_03_OPEN_PROJECT_2026-09-28.txt`, STEP 8) lists,
against a REAL, running `GreatTamanaEditor.exe` — never assumed from
reading code alone — then close out the campaign with a report mirroring
`editor-core-separation-16/CAMPAIGN_COMPLETION_REPORT.md`'s own exact
shape.

## Step 2: The Situation

By the start of this phase: `ProjectValidityTier`/
`ClassifyProjectAssemblyFolder()` (PHASE1), `ProjectLifecycleLoadCommandBridge`
(PHASE2), the two new capability methods + `EditorHost` wiring (PHASE3),
and both HTTP routes + `OpenProjectWindow` + the enabled menu item (PHASE4)
all exist and each individually passed its own fast, incremental compile
check. This phase is where they are proven to work together, for real,
end-to-end, and where the one remaining category of risk — genuine
concurrency behavior under a real HTTP request hitting a real running
Editor — finally gets tested against reality instead of just reasoned
about.

## Step 3: The Plan

### 3.1 — New end-to-end test file

`tests/Network/OpenProjectEndpointEndToEndTests.cpp`, mirroring
`tests/Network/CreateProjectEndpointEndToEndTests.cpp`'s own exact
structure (a real `NetworkServer` + a real `EditorProjectLifecycleCapability`,
option (b) from that file's own precedent — test against the real
production resolution path, using genuinely unique/disposable project
folders created and torn down in `SetUp()`/`TearDown()`, never the
persistent `ProjectAssemblyProbe`). At minimum:

1. `GET /project_assembly/list_projects` against a fixture with 2-3
   manually-created folders (one of each: Tier 0 missing
   `Libraries/CMakeLists.txt`, one Tier 1 with `Libraries/CMakeLists.txt`
   but no `Assets/*.cpp`, one Tier 2 with a real `.cpp` but no `.dll`)
   returns exactly the right `{name, tier}` pairs.
2. `POST /project_assembly/open_project?name=<Tier2Fixture>` returns
   `success` (HTTP 200), `load_attempted=false`, and a `status_message`
   containing "not yet compiled".
3. `POST /project_assembly/open_project?name=<Tier0Fixture>` (or a
   genuinely nonexistent name) returns HTTP 400 with a clear error
   message, and does not mark it active
   (`ActiveProjectAssemblyState::Instance().GetActive()` unaffected).
4. `POST /project_assembly/open_project` with each of the 4 invalid-name
   shapes BIG-STEP 2's own test already proved (`"../evil"`, `"CON"`,
   `"My Project"`, `""`) all rejected before any state mutation.

Because a real Tier-3/Tier-4 round trip needs an ACTUALLY COMPILED `.dll`,
this suite's own Tier-3/4 coverage is deliberately achieved by pointing at
`ProjectAssemblyProbe` itself (read-only from this test's point of view —
never delete/recreate it), guarded by a runtime check
("skip if `ProjectAssemblyProbe_Game.dll` does not exist in this build's
own output directory yet" — mirrors this repository's own existing,
legitimate environment-gated skip convention, confirmed real via the
baseline's own "7 legitimate environment-gated skips" count).

Register the new file in `tests/CMakeLists.txt`'s `Network/...` block,
alongside `CreateProjectEndpointEndToEndTests.cpp`'s own real entry.

### 3.2 — Live, HTTP-driven verification against a REAL running Editor

Using `run_app_background` (launch `GreatTamanaEditor.exe`) +
`gte_send_request` (never a guess from source alone):

1. `GET /project_assembly/list_projects` — confirm `"ProjectAssemblyProbe"`
   appears, tagged `"Compiled"` or `"AlreadyLoaded"` depending on whether
   it auto-loaded at startup (check via the existing
   `GET /project_assembly/debug/loaded_assemblies` route first, to know
   which outcome to expect before asserting on it).
2. If `ProjectAssemblyProbe` is NOT currently loaded this session
   (uncommon, but possible on a build without it in the startup output
   directory) — `POST /project_assembly/open_project?name=ProjectAssemblyProbe`
   performs a real load; confirm via
   `GET /project_assembly/debug/loaded_assemblies` showing both `.dll`s
   now resident, and check the engine's own log
   (`GET /get_logs`) for a clean, non-error load sequence.
3. If it IS already loaded — repeat the same request and confirm
   `load_attempted=false` in the JSON response (Tier 4's own "never a
   second load" guarantee) — this is the single most important live check
   in this whole campaign, since a wrong answer here is a live
   `ComponentTypeRegistry` duplicate-registration crash, not a soft
   failure.
4. Exercise the ImGui path too: with the Editor running, take a screenshot
   (`gte_send_request` against whichever endpoint actually renders the
   Editor UI — confirm this first) showing the "Project" menu with "Open
   Project..." enabled; if this session's tooling genuinely cannot open an
   on-demand floating ImGui window over HTTP (the same gap
   `editor-core-separation-16` already flagged twice), explicitly document
   that as an accepted limitation here too, and instead do a careful,
   honest line-by-line code review of `OpenProjectWindow.cpp` against
   PHASE4's own sample as the substitute verification step — never silently
   skip verifying this window at all.
5. Create a brand-new, throwaway Tier-1/2 project first via the ALREADY-WORKING
   `POST /project_assembly/create_project?name=MoriOpenSmokeTest` (BIG-STEP
   2's own real, working feature), then immediately
   `POST /project_assembly/open_project?name=MoriOpenSmokeTest` and confirm
   the Tier-2 ("not yet compiled") status message. Clean up afterward
   (`remove_all` the throwaway folder, re-run a plain `cmake --build build`
   once to return the build tree to its prior, clean state) — mirrors
   `editor-core-separation-16`'s own PHASE5 smoke-test hygiene exactly.

### 3.3 — Full regression

- Full clean build: `cmake --build build --target clean` then
  `cmake --build build` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`). Record the exact
  step count and confirm zero errors/new warnings.
- Full `ctest -C Debug --output-on-failure` from `build/`. Confirm the
  total passing count equals `editor-core-separation-16`'s own final
  baseline (1951) **plus** every new test this campaign added (PHASE1's 10
  + PHASE2's 5 + PHASE5's own new end-to-end tests), same 7 legitimate
  environment-gated skips, zero regressions, zero unexplained delta —
  mirrors `editor-core-separation-16/CAMPAIGN_COMPLETION_REPORT.md`'s own
  exact verification-summary arithmetic style.
- **This is the last phase of this campaign — there is no later phase to
  hand a fix off to.** If ANY test fails, or the full clean build itself
  fails: diagnose as far as you reasonably can using the real failure
  output + `GET /get_logs` if a live-Editor-shaped test is involved, fix it
  yourself if the root cause is genuinely within this campaign's own
  changes (PHASE1-4's files) and re-run the full build/regression suite to
  confirm the fix, but do NOT spin up a new delegated task/session for
  this — you are not permitted to delegate further work from inside this
  phase. If you cannot fully resolve it yourself, document the exact
  broken behavior/file/function/failing test name clearly in
  `CAMPAIGN_COMPLETION_REPORT.md` instead, so a human or a future task can
  pick up the fix from there — never silently patch around a real
  regression by loosening a test's own expectation without understanding
  why it failed.

### 3.4 — `git_status` hygiene check

Confirm only this campaign's own, real, intended files are modified/new —
no scratch project folder, no throwaway debug code, no orphaned temp file
survives anywhere in the working tree.

### 3.5 — `CAMPAIGN_COMPLETION_REPORT.md`

Write this file in the same folder
(`task_manager/editor-core-separation-17/`), mirroring
`editor-core-separation-16/CAMPAIGN_COMPLETION_REPORT.md`'s own exact
section structure:
- "What was actually built, phase by phase" (one paragraph per PHASE1-5).
- "Every real, mechanically-confirmed deviation from any phase's own file,
  and why" — explicitly include the Section 2.2 main-thread-deadlock fix
  from `PHASE0_MASTER_STRATEGY.md` here, since it IS a real, disclosed
  deviation from the literal wording of the source `.txt` file's own STEP 5
  ("both callers submit through the same bridge") — explain plainly why
  the literal reading was wrong and what was built instead.
- "New gaps found during this campaign, for the NEXT campaign (BIG-STEP 4,
  Create Script Asset) to know about" — in particular, confirm/update
  whether `ActiveProjectAssemblyState::SetActive()` being called a SECOND
  time (for a different, already-loaded-elsewhere project) during this
  campaign's own live verification behaved exactly as expected, since
  `editor-core-separation-16`'s own report flagged this as "a new,
  not-yet-tested code path" at the time.
- "Verification summary" (build/test counts, live HTTP verification
  summary, `git_status` hygiene result).
- "Explicit Non-Goals this campaign correctly stayed within."

### 3.6 — Definition of done

- [ ] New end-to-end test file passes.
- [ ] Full clean build: zero errors, zero new warnings.
- [ ] Full `ctest`: 100% passing, correct total count, same legitimate
      skip count.
- [ ] Every live HTTP/UI check in Section 3.2 performed and its real
      result recorded (not assumed).
- [ ] `git_status` clean of any scratch artifact.
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` written and committed alongside every
      phase's own individual completion report.
- [ ] `stop_app_background` called on any `GreatTamanaEditor.exe` instance
      this phase's own verification launched, before finishing.
