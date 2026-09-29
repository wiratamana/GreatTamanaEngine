# PHASE4 — Tests, Live Verification, Full Regression, Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1`/`PHASE2`/`PHASE3`
all compiling cleanly. This is the ONLY phase in this campaign that runs a
full clean build and the full `ctest` regression suite (master task's own
Note 4) — every earlier phase only ever did a fast, scoped compile check.

## STEP 1 — The Goal

Mechanically PROVE, not merely assert, the following, in this exact order:
1. Every item in `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done"
   checklist.
2. The single most load-bearing claim of this entire campaign: TWO
   independently-scaffolded Render Passes, manually wired into the SAME
   project's `RegisterProject()`, compile together with **zero**
   duplicate-`GTE_RegisterProject` linker error.
3. Zero regressions anywhere else in the engine (full build + full
   `ctest`).
4. A `CAMPAIGN_COMPLETION_REPORT.md`, same shape/style as
   `task_manager/editor-core-separation-16/CAMPAIGN_COMPLETION_REPORT.md`
   and `task_manager/editor-core-separation-17/CAMPAIGN_COMPLETION_REPORT.md`
   (read BOTH of those first, as concrete style references, before writing
   this campaign's own).

## STEP 2 — The Situation

At the start of this phase, PHASE1-3 have each already done their own
fast, scoped compile checks (per each phase's own final "Fast compile
check" step) — this phase is the first point in the whole campaign where a
FULL clean build has been attempted, and therefore the first point any
cross-file integration mistake (a missed include, a parameter-list
mismatch in the 10th `RegisterRoutes()` pointer, an ODR issue from
`EditorContext.h`'s forward-declared enum) would actually surface. Budget
real time for this — do not treat it as a formality.

Also re-read `docs/conventions/project-assembly-system.md`'s own "Known,
permanent limitation, not a bug" paragraph before starting: a Project
Assembly `.dll` already `LoadLibraryW()`'d by the CURRENTLY RUNNING
`GreatTamanaEditor.exe` instance can never be successfully recompiled by
that same instance (Windows locks a mapped `.dll` image). This means the
load-bearing proof in STEP 3 below MUST use a project that has never been
loaded into the currently-running instance (a brand-new scratch project,
never `Open`ed) — if it were instead run against `ProjectAssemblyProbe`
(already loaded at every startup, per that same doc's "Capability #1/#2"
sections), the Compile step would fail with a linker "permission denied"
error that has NOTHING to do with this campaign's own duplicate-symbol
hazard, and could be misdiagnosed as a genuine regression.

## STEP 3 — The Plan

### 3.1 — New end-to-end HTTP test file,
`tests/Network/CreateAssetEndpointEndToEndTests.cpp`

Mirror `tests/Network/CreateProjectEndpointEndToEndTests.cpp`'s own real,
existing `NetworkServer`/`httplib::Client`/`WaitUntilAcceptingConnections`
fixture SHAPE (read that file first, in full). Note one real difference,
though: that file never itself sets `ActiveProjectAssemblyState` directly
(it only calls `CreateNewProjectAssembly()` via HTTP, which sets it as a
side effect) — for THIS file's own direct `SetActive()`/`Clear()` use (see
below), mirror `tests/Network/OpenProjectEndpointEndToEndTests.cpp`'s own
"capture a before snapshot, never assume a pristine empty state" idiom
instead (`ActiveProjectAssemblyState` is a real, process-wide singleton
shared by EVERY test in `GreatTamanaEngineTests.exe`, including every other
`*ProjectEndpointEndToEndTest` file, so this file's own "no active project"
case below MUST explicitly `Clear()` it first, then restore whatever the
captured "before" snapshot actually was afterward — never assume it starts
`false`). Minimum cases:
- No active project set (captured "before" snapshot, then
  `ActiveProjectAssemblyState::Instance().Clear()` called explicitly,
  restored afterward per above) →
  `POST /project_assembly/create_asset?kind=render_pass&name=X` → `400`,
  error message mentions "no active project".
- Invalid `kind` query value → `400`, error message names the three valid
  values.
- Capability pointer is `nullptr` (constructed with it omitted/null) →
  `503`.
- A real active project, set up by calling
  `ActiveProjectAssemblyState::Instance().SetActive(name, scratchDirectory)`
  directly against a real scratch temp directory (with `scratchDirectory /
  "Assets"` pre-created) — safe to redirect here, unlike
  `CreateNewProjectAssembly()`'s own test, because `CreateAssetScaffold()`
  never resolves its own source root internally, it only ever reads
  `ActiveProjectAssemblyState::Instance().GetActive()` (see PHASE1's own
  STEP4 for the full reasoning) — then one successful call per kind (3
  cases), asserting the EXACT expected `created_files` list and that each
  file genuinely exists on disk with the expected substituted content.
  Restore (or `Clear()`) `ActiveProjectAssemblyState` in this test's own
  teardown too.
- Same name, same kind, called twice → second call `400`, "already
  exists", and the ORIGINAL file's content/mtime unchanged (a real,
  mechanical proof "zero files touched" on rejection, not just an assumed
  side effect).
- Same name, different case, called twice → second call ALSO `400`.

Add this new file to `tests/CMakeLists.txt`'s `if(TRUE)` block, same section
as `CreateProjectEndpointEndToEndTests.cpp`/`OpenProjectEndpointEndToEndTests.cpp`
(NOT the `if(GTE_ENABLE_PROJECT_PANEL)` sub-block — this feature has no
dependency on that switch at all).

### 3.2 — Scoped test run first

Before touching the FULL suite, run only this campaign's own new tests
(PHASE1's unit tests + this phase's new end-to-end file) via ctest's `-R`
regex filter, to catch anything cheaply/quickly. Only proceed to 3.3 once
these are green.

### 3.3 — The load-bearing live proof (do this against a REAL running
`GreatTamanaEditor.exe`, not just a unit test — a unit test proves the
TEMPLATE never emits a second `GTE_DEFINE_PROJECT_EXPORTS_GAME(...)` call;
it does NOT, by itself, prove the real toolchain actually links two
manually-wired passes together cleanly)

1. `run_app_background` a fresh `GreatTamanaEditor.exe`.
2. `POST /project_assembly/create_project?name=DualPassProof`.
3. `POST /project_assembly/create_asset?kind=render_pass&name=First`.
4. `POST /project_assembly/create_asset?kind=render_pass&name=Second`.
5. `stop_app_background` the running instance (per this file's own STEP 2
   above — a currently-loaded `.dll` blocks its own recompile; but
   `DualPassProof` was only ever CREATED, never OPENED/loaded into THIS
   instance, so this step may be unnecessary in practice — confirm which
   is actually true live rather than assuming; if `Core::LoadProjectAssemblies()`'s
   own unconditional startup scan (confirmed real,
   `editor-core-separation-17`'s own completion report) means a
   newly-created-but-not-yet-compiled project is genuinely never loaded
   until it's both compiled AND the process restarts/`Open`s it, then this
   whole project is safe to compile with the SAME still-running instance,
   and this step can be skipped — verify live, do not guess).
6. Directly edit `Projects/DualPassProof/Assets/DualPassProofGame.cpp`'s
   own `RegisterProject()` body (via `write_file`/`edit_line` — this is the
   ONE, single, explicitly-sanctioned exception in this whole campaign to
   "no in-engine text editor": the STRATEGY session/implementer edits it
   directly with ordinary file tools, exactly the way a real end user would
   with VS Code/Notepad, per this whole 5-file master plan's own Non-Goal:
   "Does NOT add any in-engine text/code editor... editing its contents
   afterward still requires an external editor") to add:
   ```cpp
   #include "FirstRenderPass.cpp" // WRONG - do not #include a .cpp; declare instead:
   ```
   Correct approach: add forward declarations
   `void RegisterFirstRenderPass(gte::Core&);` /
   `void RegisterSecondRenderPass(gte::Core&);` near the top of
   `DualPassProofGame.cpp` (these are ordinary, non-exported free functions
   living in DIFFERENT translation units within the SAME `_Game` target —
   an ordinary forward declaration + relying on the linker to resolve the
   symbol across `.cpp` files within one shared-library target, standard
   C++, no header needed since both TUs compile into the same target), then
   call both from inside `RegisterProject()`'s own body:
   `RegisterFirstRenderPass(core); RegisterSecondRenderPass(core);`.
7. `POST /project_assembly/debug/compile_only?name=DualPassProof` (the
   EXISTING, already-shipped route).
8. Poll `GET /project_assembly/hot_reload/status` and/or
   `GET /get_logs` until the build's own final "Build finished with exit
   code N" log line appears (per `docs/conventions/project-assembly-system.md`'s
   own documented behavior of `TriggerProjectAssemblyCompile()`).
9. **The actual proof**: exit code `0`, and the log contains NO "multiple
   definition of `GTE_RegisterProject`" text anywhere. If a linker error
   DOES appear, this is a real, campaign-blocking regression — stop, do not
   proceed to 3.4, and delegate a fix task (see STEP 4 below).
10. Clean up `Projects/DualPassProof/` afterward — this is a throwaway
    fixture, never committed.

### 3.4 — Full regression

1. `cmake --build build --target clean` then `cmake --build build` — full
   clean build. Zero errors, zero NEW compiler warnings versus the
   pre-campaign baseline (compare against `editor-core-separation-17`'s own
   completion report's own "594/594 steps" baseline number, adjusted for
   this campaign's own new files).
2. `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`
   — expect the PRIOR baseline (1974, per `editor-core-separation-17`'s own
   completion report) plus this campaign's own new test count, zero
   failures, same 8 legitimate environment-gated skips (unless this
   campaign's own new tests introduce a genuinely new, honestly-explained
   skip — do not silently absorb an unexplained delta; if the arithmetic
   doesn't add up exactly, find out why before writing the completion
   report, exactly like `editor-core-separation-17`'s own report had to
   correct its own predicted arithmetic).

### 3.5 — Remaining Definition of Done items

Walk `PHASE0_MASTER_STRATEGY.md`'s own checklist top to bottom, live,
via HTTP + screenshots (`gte_send_request`), checking off each box for
real — do not mark anything done without a concrete, reproducible
verification step attached (mirroring every prior campaign's own
completion-report style of citing the EXACT command/output that proved
each claim).

## STEP 4 — If anything fails

Per the master task's own Note 4/instructions: if a test fails or the
load-bearing proof (STEP 3.3) does NOT hold, diagnose the root cause using
the engine's own internal log (`GET /get_logs`) and this phase's own
compile-error output, THEN use `delegate_task` to dispatch a focused fix
back into whichever earlier phase (PHASE1/2/3) actually owns the broken
code — do not patch it inline inside this phase's own report without
also updating that owning phase's `.md` file if the fix reveals the
phase's own written instructions were wrong (mirroring how this whole
5-file plan already corrects the external master-plan `.txt` file's own
mistakes in `PHASE0`/`PHASE1` rather than silently working around them).
**Every delegated task from this phase (or any phase) must itself be
instructed to use `ask_questions` if it hits a genuine design ambiguity**,
per the master task's own repeated instruction — restate this explicitly
in every `delegate_task` prompt you write, do not assume it's implied.

## STEP 5 — `CAMPAIGN_COMPLETION_REPORT.md`

Write this file (same folder, `task_manager/editor-core-separation-18/`)
once everything above is green, structured exactly like
`editor-core-separation-17/CAMPAIGN_COMPLETION_REPORT.md`:
- "What was actually built, phase by phase" (one paragraph per PHASE1-4).
- "Every real, mechanically-confirmed deviation from any phase's own file,
  and why" — MUST include, at minimum: the `ProviderScope`/`RenderPassProvider`
  signature correction (PHASE0/PHASE1's own finding), the
  `BeginPopupContextItem`-vs-`BeginPopupContextWindow` correction and its
  concrete mutual-exclusion fix (PHASE2's own finding), and whether STEP
  3.3's own "is `stop_app_background` actually necessary" question resolved
  true or false, live.
- "New gaps found during this campaign, for the NEXT campaign (BIG-STEP 5,
  'Compile menu') to know about" — at minimum, note explicitly whether
  `Core::LoadProjectAssemblies()`'s own startup-only load behavior (flagged
  by `editor-core-separation-17`'s own completion report as "BIG-STEP 5's
  future 'Compile' menu action is the FIRST production feature that will
  ever cause a real, live Tier-3 load call to fire in genuine end-user
  use") is still true and unchanged after this campaign, since BIG-STEP 5
  is the very next campaign in this whole 5-file series and will need this
  exact context.
- "Verification summary" (full build step count, full ctest count/skips,
  the STEP 3.3 live proof's own exact commands/log excerpts).
- "Explicit Non-Goals this campaign correctly stayed within" (restate
  `PHASE0_MASTER_STRATEGY.md`'s own LDD-CA3/the master-plan file's own
  Non-Goals section, confirming none were silently expanded).

## STEP 6 — Git

`git_add`/`git_commit` every new/modified file from this whole campaign
(PHASE1-4's source changes + this report), in ONE commit, with a message
summarizing the whole BIG-STEP 4 campaign (mirroring
`editor-core-separation-16`/`-17`'s own final commit style — check
`git_status` first to confirm nothing stray/scratch is about to be
committed, e.g. a leftover `Projects/DualPassProof/`/`Projects/Scratch2/`
folder from this phase's or PHASE2/3's own live-verification steps).
