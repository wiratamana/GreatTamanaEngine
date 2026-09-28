# PHASE5 — Full Live Verification & Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md` (read first).

Depends on: PHASE1-4, all complete.
Blocks: nothing — this is the final phase of this campaign.

End state of this phase: every Definition-of-Done item from the source
document (`PROJECTWORKFLOW_BIGSTEP_02_CREATE_NEW_PROJECT_2026-09-28.txt`,
its own Step 8) is proven true, live, end-to-end; a new HTTP end-to-end
Tier-1-adjacent test file exists for permanent regression coverage; the
full existing `ctest` suite passes; the campaign is formally closed out.

This is the ONE phase in this whole campaign permitted to run a full
build + full `ctest` regression pass (this campaign's own governing Note
4/5).

---

## STEP 1 — New end-to-end test file (deferred here from PHASE4)

`tests/Network/CreateProjectEndpointEndToEndTests.cpp` (new), mirroring
`tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`'s own
exact fixture shape (a real `Network::NetworkServer` on an ephemeral port,
a real `httplib::Client`, `Network::TestHelpers::WaitUntilAcceptingConnections`) —
but wired against a real `EditorProjectLifecycleCapability` instance
pointed at a THROWAWAY temp directory standing in for the repo's real
`Projects/` folder (never the real, production `Projects/` folder — this
test must never touch real repo state).

Since `EditorProjectLifecycleCapability::CreateNewProjectAssembly()`
resolves its own source root INTERNALLY (via
`ResolveCMakeBuildDirectory(gte::ExecutableDirectory())` — see PHASE3), it
cannot be redirected to a temp directory by construction alone. Two
options, pick whichever is cleanest once PHASE3's real, final code is
re-read (this is a legitimate design choice left open for this phase,
mirrors this whole 5-file series' own repeated "the last mile is decided
by whoever writes the code, informed by everything already spelled out" style):

- **(a)** Add a small, test-only constructor overload/setter to
  `EditorProjectLifecycleCapability` that accepts an explicit
  `buildDirectory` override (defaulting to the real, production
  resolution when unset) — mirrors
  `BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`'s
  own "explicit, required directory parameter, never internally resolved"
  precedent (`ProjectAssemblyBuildRunner.h`), just applied one layer up.
- **(b)** Test `CreateNewProjectAssembly()`'s own real production
  resolution path directly, against THIS repo's own real, current build
  tree, using a genuinely unique, disposable project name (e.g.
  `"GteE2ECreateProjectTest_<random suffix>"`), and delete it in the
  test's own `TearDown()` — accepting that this one test genuinely mutates
  (briefly) and then cleans up real repo state, exactly like
  `ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`'s own
  `CompileOnlyValidNameReturns200Started` test already accepts a real,
  live child-process side effect for the same underlying reason (this
  file's own header comment explains why that test also accepts this
  trade-off).

Whichever is chosen, cover at minimum:
- `POST /project_assembly/create_project?name=<valid unique name>` returns
  200 with `created_source_directory`.
- The same request repeated returns 400, "already exists".
- `?name=CON`, `?name=` (empty), `?name=../evil`, `?name=My Project` all
  return 400, and — critically — a directory listing taken immediately
  before/after each rejected attempt shows NOTHING was written to disk.
- `NetworkServer` constructed with `projectLifecycleCapability == nullptr`
  (mirrors `ProjectAssemblyHotReloadEndpointsNoCapabilityTests`' own exact
  convention) returns 503 for this same route.

Add this file to `tests/CMakeLists.txt`'s hand-maintained list.

## STEP 2 — Full live, end-to-end verification (every item from the
source document's own Step 8)

Using `run_app_background` + `gte_send_request` (never a manual human
click-through — this whole campaign is AI-in-the-loop with minimal human
intervention):

1. `POST /project_assembly/create_project?name=MoriGate` — confirm the
   exact 3-file scaffold, byte-for-byte matching PHASE3's own template,
   and (via a follow-up scratch log line, or by re-reading
   `ActiveProjectAssemblyState` state through a throwaway debug accessor if
   one is convenient) confirm `ActiveProjectAssemblyState::Instance().
   GetActive().name == "MoriGate"` afterward.
2. The SAME request repeated — confirm 400, "already exists", confirm
   nothing was overwritten (compare file contents/mtimes before/after).
3. `?name=../evil`, `?name=CON`, `?name=My Project`, `?name=` (empty) —
   all rejected (400) BEFORE any filesystem write (confirm via a directory
   listing immediately before and after each attempt).
4. The SAME 4 checks above, repeated via the ImGui "New Project..."
   window (screenshot-driven verification via `gte_send_request`'s
   swapchain/game-view capture) — confirm the red error text actually
   appears in the window for the duplicate-name case, and the window
   stays open (does not auto-close) on any failure.
5. Immediately after a successful create, `POST
   /project_assembly/debug/compile_only?name=MoriGate` (the EXISTING,
   unmodified Compile mechanism) genuinely succeeds, producing a real
   `MoriGate_Game.dll` in the project-assembly output directory — poll
   `GET /get_logs` until the build's own completion log line appears, then
   confirm the `.dll` file's existence directly. **This is the end-to-end
   proof that LDD-CP2's "always reconfigure" design genuinely closes the
   gap the source document's own Finding 1/3 flagged** — write down,
   verbatim, in this phase's own completion report, whether the automatic
   reconfigure step actually turned out to be load-bearing on this machine
   (i.e., would the plain `CONFIGURE_DEPENDS` glob alone have been enough)
   — purely informational at this point (the code path never branches on
   it), but valuable, permanent, honest documentation for a future reader
   of this campaign.
6. Delete `Projects/MoriGate/` (and any other scratch project folder
   created by this step) once every check above has passed, and re-run a
   plain `cmake --build build` once more so the build tree returns to its
   prior, clean state before STEP 3's full regression pass.

## STEP 3 — Full build + full regression pass (the one place this
campaign is permitted to run it)

1. `cmake --build build` (full build, not incremental-only — confirms
   every file this whole 5-phase campaign touched, across `gte_core`/
   `gte_editor`/the test target, still compiles clean together).
2. `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`.
3. Confirm: same total test count as this campaign's own starting
   baseline, PLUS the new tests this campaign added (PHASE1's 4, PHASE2's
   9, PHASE5's own new end-to-end file's ~5-6) — every one passing, no
   new failures, no new legitimate skips beyond whatever the baseline
   already had.
4. If ANYTHING fails: diagnose the failure directly (read the exact
   `ctest` output, find the exact assertion/file), fix the ROOT CAUSE in
   the actual phase's own file where the bug was introduced (never patch
   around it inside PHASE5 itself), and re-run the full suite again before
   considering this phase done.

## STEP 4 — Documentation touch-up (small, in-scope)

`docs/conventions/project-assembly-system.md` — re-read its own current
content in full first. If it contains any claim that would now be
factually wrong given this campaign's own new "Create New Project"
capability (e.g. an old "every Project Assembly folder must be created by
hand" statement, if one exists — confirm by `search_in_dir` before
assuming), add a small, clearly-dated section describing the new
capability and linking back to
`PROJECTWORKFLOW_BIGSTEP_01_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
and this campaign's own `PHASE0_MASTER_STRATEGY.md` — mirroring
`editor-core-separation-15`'s own PHASE5 precedent (LDD-HR9) for how a
stale doc claim gets superseded rather than silently deleted. If no such
stale claim actually exists, state that explicitly in this phase's own
completion report instead of inventing a documentation change that isn't
needed.

## STEP 5 — Campaign closeout

Write `CAMPAIGN_COMPLETION_REPORT.md` in this same folder, summarizing:
- What was actually built, phase by phase.
- Every real, mechanically-confirmed deviation from any phase's own file,
  and why.
- The LDD-CP2 "was the auto-reconfigure actually load-bearing on this
  machine" finding (STEP 2.5 above), stated plainly.
- Any NEW gap found during this campaign, the same way
  `PHASE0_MASTER_STRATEGY.md`'s own Section 2.2 corrections were found —
  in particular, flag anything relevant for the NEXT campaign in this
  5-file series ("Open Project", BIG-STEP 3) to know about before it
  starts (e.g. anything about `ActiveProjectAssemblyState`'s real,
  shipped shape that differs from what BIGSTEP_01's own Section 3 assumed).

## Definition of Done — this phase, and this whole campaign

- [ ] The new end-to-end test file (STEP 1) exists, is registered, and
      passes.
- [ ] Every one of STEP 2's 6 live verification items was actually
      performed (not assumed) and passed.
- [ ] Full build + full `ctest` pass (STEP 3), zero regressions.
- [ ] `docs/conventions/project-assembly-system.md` was actually checked
      for a stale claim, and either fixed or explicitly confirmed to need
      no change.
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` is written.
- [ ] No scratch project folder or throwaway debug code remains anywhere
      in the final commit.

## What this phase does NOT do

- Does NOT implement any part of "Open Project"/"Create Script Asset"/
  "Compile menu" (BIG-STEPs 3-5) — those are separate, future campaigns;
  this phase's only job regarding them is leaving an honest, accurate
  completion report for whoever starts the next one.
