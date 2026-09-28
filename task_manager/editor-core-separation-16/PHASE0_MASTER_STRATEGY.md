# editor-core-separation-16 — PHASE0 MASTER STRATEGY
## On-Engine Project Workflow — BIG-STEP 2 of 5: "Create New Project"

Parent external plan (read the whole 5-file series once, in order, before
touching any code — this campaign implements ONLY BIG-STEP 2, the other four
are separate, later campaigns and are explicitly OUT OF SCOPE here):

- `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-3\PROJECTWORKFLOW_BIGSTEP_01_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
  (LDD-PW1..PW5, Findings 1-7, the `ActiveProjectAssemblyState` primitive,
  the dependency graph, the Section 5 refinement — all cited by number below).
- `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-3\PROJECTWORKFLOW_BIGSTEP_02_CREATE_NEW_PROJECT_2026-09-28.txt`
  (this campaign's own direct source document — its 8 Steps map onto
  PHASE1-5 below, 1:1, with the corrections/decisions Section 2/3 of this
  file spells out).

Prior campaigns this one builds directly on top of (all DONE, all
re-verified against the real, current repo during this strategy's own
preparation, never assumed from memory):
- `task_manager/editor-core-separation-11/` — the whole Project Assembly
  build system (`gte_add_project()`, `ProjectAssemblyHost`, the
  `_Game.dll`/`_Editor.dll` split, `TriggerProjectAssemblyCompile()`).
- `task_manager/editor-core-separation-12/` through `-15/` — the Project
  Assembly Hot Reload feature (`IHotReloadDebugCapability`,
  `EditorHotReloadDebugCapability`, the `/project_assembly/debug/*` and
  `/project_assembly/hot_reload` HTTP routes). This campaign reuses several
  of its pieces (`TriggerProjectAssemblyCompile`,
  `ResolveCMakeBuildDirectory`, `ParseProjectNameQuery`) but does **not**
  modify hot-reload's own control flow at all.

When this campaign (`editor-core-separation-16`) is DONE: an ImGui
"Project > New Project..." menu item AND a raw `POST
/project_assembly/create_project` HTTP request both create a real,
immediately-compileable Project Assembly source folder on disk, mark it as
the engine's one "active" project, and report success/failure through the
exact same shared code path either way.

---

## STEP 1 — THE GOAL (where are we going?)

At the end of this campaign:

1. A brand-new, empty `Projects/<Name>/` folder — containing a minimal,
   immediately-compileable 3-file scaffold — can be created two ways, both
   calling the exact same underlying function:
   - Clicking "Project > New Project..." in the running Editor, typing a
     name, and clicking "Create".
   - `POST http://127.0.0.1:8080/project_assembly/create_project?name=<X>`.
2. A name that is empty, contains illegal characters (path separators,
   `..`, spaces), or collides with a Windows-reserved device name (`CON`,
   `PRN`, ...) is rejected **before any filesystem write happens**, with a
   clear, human-readable error message, in BOTH the ImGui window (red text,
   window stays open) and the HTTP response (400 + JSON error body).
3. A name that already exists as a folder (or ANY file) under
   `Projects/` is rejected the same way — "already exists" — creating or
   overwriting nothing.
4. Immediately after a successful "Create", the EXISTING, unmodified
   `/project_assembly/debug/compile_only?name=<X>` route (or a future
   "Compile" menu item, BIG-STEP 5, out of scope here) can build the
   brand-new project's `<Name>_Game.dll` successfully, on the very first
   try, with **zero manual `cmake` reconfigure step ever required from a
   human** — this campaign's own code performs whatever reconfigure is
   needed, automatically, as an unconditional, defensive part of
   "Create" itself (see LDD-CP2 below — this is a deliberate, load-bearing
   simplification of the source document's own Step 1).
5. The engine now has exactly one, single, shared, always-fresh concept of
   "the currently active Project Assembly" (`ActiveProjectAssemblyState`),
   fully implemented (not a placeholder — see LDD-CP1 below), ready for the
   later "Open Project"/"Create Script Asset"/"Compile menu" campaigns to
   read and extend without inventing a second, competing concept.
6. A new top-level "Project" menu exists in the Editor's menu bar, next to
   "File"/"Window", holding "New Project..." (fully working), plus two
   disabled placeholder items — "Open Project..." and "Compile" — reserved,
   by name, for the later campaigns that implement them (never wired to any
   real action by this campaign).

---

## STEP 2 — THE SITUATION (where are we now?)

Everything below was confirmed by directly reading the real, current source
tree during this strategy's own preparation (`read_file`/`search_in_dir`,
never guessed, never taken only from the two source documents' own prose —
several small, concrete corrections to those documents are called out
explicitly below, marked **CORRECTION**).

### 2.1 — What already exists and works (confirmed, reused as-is)

- Root `CMakeLists.txt`, lines 1735-1741: `GTE_ENABLE_PROJECT_ASSEMBLIES AND
  IS_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects"` gate, `file(GLOB
  GTE_PROJECT_DIRS CONFIGURE_DEPENDS ...)` — confirmed byte-for-byte as the
  source document describes it. A brand-new `Projects/<Name>/Libraries/
  CMakeLists.txt` really does get picked up by CMake's own dependency
  tracking without a human ever typing a bare `cmake -S . -B build` — but
  see LDD-CP2 below for why this campaign does not gamble on that alone.
- `cmake/GteProject.cmake`'s `gte_add_project(NAME)` (lines 22-75): confirmed
  — an `Assets/` folder with zero non-`Editor/` `.cpp` files produces only a
  `message(STATUS ...)` and **no** `<Name>_Game` target at all. A freshly
  created project MUST ship a real, non-empty `.cpp` under `Assets/` or its
  own first "Compile" attempt does nothing, silently.
- `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`: `ResolveCMakeBuildDirectory()`
  (declared line 117) already walks upward from `gte::ExecutableDirectory()`
  to find `CMakeCache.txt` — this campaign's new
  `ResolveProjectAssemblySourceRootDirectory()` (PHASE1) is placed
  immediately alongside it, in the SAME two files, exactly as the source
  document specifies.
- `cmake/templates/ProjectAssemblyExports.h`: confirmed, real, current
  content is exactly the 24-line file the source document quotes —
  `GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterFn)` / `..._EDITOR(...)` macros,
  nothing else. `Projects/ProjectAssemblyProbe/Libraries/` (the one real,
  working example) confirmed to contain exactly `CMakeLists.txt` (one line,
  `gte_add_project(ProjectAssemblyProbe)`) + this same header, byte-for-byte.
- `src/Core/EditorCapabilities.h`: confirmed real, current, 259-line file.
  `IHotReloadDebugCapability` (line 173) is the exact "nullable pointer,
  gte_core holds only the abstract interface" pattern this campaign's new
  `IProjectLifecycleCapability` (PHASE3) mirrors. **CORRECTION** to that
  interface's own header comment (lines 204-208): it says
  `GetLoadedAssemblyFileNames()` is "a placeholder... always returns an
  empty vector" — this is now STALE. `ProjectAssemblyHost::
  GetLoadedAssemblyFileNames()` (confirmed, `src/Core/Plugins/
  ProjectAssemblyHost.h` line 104, real body in the `.cpp`) is a genuinely
  real, live, working accessor today (shipped by `editor-core-separation-13`),
  returning every currently-open `.dll`'s own file name. This is GOOD NEWS
  for LDD-CP1 below: the one piece `ActiveProjectAssemblyState::GetActive()`'s
  `isLoaded` field needs already exists and works, right now, with zero new
  engine-side plumbing required beyond a single setter call.
- `src/Editor/EditorHotReloadDebugCapability.h/.cpp`: confirmed the exact
  "constructed as a namespace-scope static inside `EditorHost.cpp`, wired via
  a setter called once from `EditorHost`'s own constructor body, strictly
  after `m_core` exists but before `m_networkServer` accepts a request"
  pattern (`EditorHost.cpp` lines 213-241) — this campaign's new
  `EditorProjectLifecycleCapability` (PHASE3) copies this pattern exactly.
- `src/Network/NetworkServer.h`: confirmed real, current constructor (lines
  156-163) takes exactly 8 defaulted, non-owning capability/bridge pointers,
  the 8th being `IHotReloadDebugCapability* hotReloadDebugCapability =
  nullptr`. This campaign's PHASE4 appends a 9th,
  `IProjectLifecycleCapability* projectLifecycleCapability = nullptr`,
  mirroring every prior addition's own doc-comment style exactly. Confirmed
  **zero other construction site** for `NetworkServer` exists anywhere in
  `src/` (only `EditorHost.cpp`'s own member-initializer-list construction) —
  a headless/Player-shaped host simply never constructs one at all, so
  "defaulted to `nullptr`" is the complete, sufficient story for that case.
- `src/Network/NetworkRoutes.cpp` line 1201-1211:
  `ParseProjectNameQuery()`'s real, current body checks ONLY
  `nameParam.empty()` — confirmed, matches Finding 4 exactly.
  `/project_assembly/debug/compile_only` (`NetworkServer.cpp` lines
  1313-1330) is confirmed real, live, already working — this campaign's new
  `/project_assembly/create_project` route (PHASE4) is registered
  immediately alongside it, same file, same style.
- `src/Editor/DockLayout.cpp`: confirmed real, current menu bar (lines
  140-176) has exactly two top-level menus, "File" (line 141) and "Window"
  (line 162) — no "Project" menu exists yet anywhere in this codebase. The
  new one this campaign adds is genuinely new, not a rename/extension of
  either existing one.
- `src/Editor/EditorContext.h`: confirmed real, current, 254-line file.
  `sceneIoStatusMessage`/`sceneIoStatusIsError`/`sceneIoStatusSetTime`
  (lines 147-149) and `frameDebuggerWindowOpen` (line 251) are the two
  exact, real, existing precedents this campaign's own new fields
  (PHASE4) mirror.
- `src/Editor/ProjectRootPath.h`: confirmed `ResolveProjectRootDirectory()`
  resolves `<exe dir>/Project` — the EXISTING "Project" panel's own content-
  asset root. **This is a completely different folder from
  `Projects/<Name>/` (plural, repo-root-relative, the Project Assembly
  SOURCE tree this whole campaign writes into)** — confirmed by direct
  comparison; the two must never be confused by whoever implements PHASE3.
- `tests/CMakeLists.txt`: confirmed test files are a hand-maintained,
  explicit list (NOT a glob) — e.g. line 2166,
  `Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`. Any new
  Tier-1 test file this campaign adds (PHASE1/2) MUST be added to this list
  explicitly, or it silently never compiles/runs.

### 2.2 — Corrections to the source documents (found by direct verification)

- **CORRECTION 1**: `PROJECTWORKFLOW_BIGSTEP_02...txt` Step 2 says a Tier-1
  test for `ResolveCMakeBuildDirectory()` already exists "wherever the
  existing... tests already live" and implies
  `tests/Core/ProjectAssemblyBuildRunnerTests.cpp` already exists. **This
  file does not exist anywhere in the repo** (confirmed,
  `search_in_dir` for `ProjectAssemblyBuildRunnerTests` across the whole
  tree — the only hit is a prior campaign's own report noting the same
  absence). The real, only existing sibling test file is
  `tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`.
  PHASE1 below creates a NEW file,
  `tests/Core/Plugins/ProjectAssemblyBuildRunnerSourceRootTests.cpp`, in
  that same real folder, rather than pretending to "extend" a file that
  isn't there.
- **CORRECTION 2**: `IHotReloadDebugCapability::GetLoadedAssemblyFileNames()`
  is NOT the empty placeholder its own header comment still (staleley)
  claims — see Section 2.1 above. Flagged so PHASE3's implementer does not
  waste time re-verifying something already fully usable.
- **CORRECTION 3 (scope decision, not a bug)**: the source document's own
  Step 1 asks for a one-time MANUAL smoke test (create a throwaway project
  by hand, run a bare terminal `cmake --build --target X_Game`, observe
  whether it needs a prior plain reconfigure) to decide, once, which of two
  code shapes `CreateNewProjectAssembly()` needs. This campaign does not
  do this — see LDD-CP2 below for the concrete, code-only replacement.

---

## STEP 3 — THE PLAN (how do we get there?)

### 3.1 — New Locked Design Decisions this campaign adds

**LDD-CP1 — `ActiveProjectAssemblyState` is built FULLY, correctly,
working, in this campaign — never a placeholder shape.** The source
document's BIGSTEP_01 Section 3 already specifies the complete, correct
behavior (`isCompiled`/`isLoaded` re-derived fresh on every call, never
cached); Section 4 only says the field NAMES are the hard requirement for
this campaign. Since Section 2.1 above confirms every piece
`GetActive()` needs (`std::filesystem::exists()`,
`ProjectAssemblyHost::GetLoadedAssemblyFileNames()`) already exists and
works today, building it fully now costs one extra, cheap setter call
(mirrors `EditorHotReloadDebugCapability::SetProjectAssemblyHost()`'s own
exact precedent) and leaves ZERO rework for the "Open Project" campaign —
strictly better than shipping a half-working placeholder that campaign
would otherwise have to come back and finish. See PHASE3 for the full
implementation.

**LDD-CP2 — "Create" ALWAYS runs one synchronous, unconditional,
no-target `cmake -S <repoRoot> -B <buildDir>` reconfigure step, immediately
after writing a new project's 3-file scaffold, every single time, with no
prior manual smoke test or runtime branching on "which CMake behavior did
we observe".** This replaces the source document's own Step 1 (a one-time,
human-run terminal experiment used to decide between two code shapes) with
a single, permanent, defensive code path that is correct regardless of
which way real CMake actually behaves on this machine: if the plain
`file(GLOB CONFIGURE_DEPENDS ...)` auto-pickup alone would already have
been enough, this reconfigure is a harmless, fast (well under a second),
idempotent no-op; if it would NOT have been enough, this reconfigure is
exactly what makes the very next `cmake --build --target <Name>_Game` (the
EXISTING, unmodified Compile mechanism) succeed on its first try. This is a
pure-code decision (per this task's own governing instruction to focus on
code, not on manual verification ceremony) and removes an entire class of
"guessed wrong" risk. `ProjectAssemblyBuildRunner.cpp`'s own existing
`CreateProcessW()`/pipe-reading helper is reused for this (factored out as
a small, shared, synchronous "run one plain command and wait" helper — see
PHASE1) rather than a second, independently-written child-process
mechanism.

**LDD-CP3 — `NetworkRoutes.cpp`'s `ParseProjectNameQuery()` is hardened, in
place, to reuse the SAME new strict validator this campaign introduces,
inside this same campaign (PHASE2).** Finding 4 (BIGSTEP_01) flags this as
a real, pre-existing gap; the change is small (one function body, one
file), backward-compatible (every existing real project name already
satisfies the stricter rule trivially), and the one test file it can affect
(`tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`, if
that file exists — confirm at PHASE2 time) is cheap to re-run in isolation,
not a full regression pass. Doing this now (rather than "someday") closes a
real, currently-live security/robustness gap on the very same class of
input (a project name) this campaign is otherwise making brand new, much
more dangerous (WRITE, not just read) use of.

### 3.2 — Phase list and dependency graph

```
PHASE0 (this file)
    |
    v
PHASE1 — Live-Safe Reconfigure Helper & Source-Root Resolver
    (ResolveProjectAssemblySourceRootDirectory(); the shared synchronous
    "run one plain command and wait" helper LDD-CP2 needs, factored out of
    ProjectAssemblyBuildRunner.cpp's existing CreateProcessW() plumbing;
    Tier-1 tests for both)
    |
    v
PHASE2 — Strict Name Validator & Existing-Route Hardening
    (IsValidProjectAssemblyIdentifierName(), new dependency-free header;
    Tier-1 tests covering every required case; LDD-CP3's small,
    backward-compatible ParseProjectNameQuery() hardening)
    |
    v
PHASE3 — ActiveProjectAssemblyState, IProjectLifecycleCapability &
    EditorProjectLifecycleCapability
    (the shared singleton, LDD-CP1's full implementation; the new
    capability interface; the real CreateNewProjectAssembly() body wiring
    PHASE1+PHASE2's new functions together, plus LDD-CP2's reconfigure step;
    EditorHost.cpp wiring)
    |
    v
PHASE4 — HTTP Route, EditorContext Fields & the "Project" Menu / NewProjectWindow
    (NetworkServer's 9th constructor parameter; the new
    /project_assembly/create_project route; EditorContext's new status
    fields; NewProjectWindow; DockLayout.cpp's new "Project" top-level menu)
    |
    v
PHASE5 — Full Live Verification & Campaign Closeout
    (every Definition-of-Done item from the source document's own Step 8,
    end-to-end, live, via gte_send_request + the ImGui window; the ONE
    full build + full `ctest` regression pass this whole campaign is
    permitted to run; campaign closeout report)
```

Each phase's own Definition of Done is a strict subset gate for the next —
PHASE3 in particular must not begin until PHASE1/2's new functions
genuinely compile and pass their own Tier-1 tests, since `CreateNewProjectAssembly()`
calls both directly.

### 3.3 — Non-goals (explicit, so nobody "helpfully" expands scope)

- Does NOT implement "Open Project" (BIG-STEP 3), "Create Script Asset"
  (BIG-STEP 4), or the "Compile" menu item's real action (BIG-STEP 5) — the
  "Project" menu's own "Open Project..."/"Compile" items are added as
  **disabled placeholders only** (`ImGui::MenuItem(..., false)`), reserved
  by name for those later campaigns.
- Does NOT scaffold an `Assets/Editor/` folder for a new project — Game-only
  by default, exactly per Finding 3/the source document's own Step 4.
- Does NOT auto-compile a freshly created project — only marks it active;
  a human (or a later "Compile" menu / an HTTP call to the existing
  `/project_assembly/debug/compile_only`) still triggers the actual build.
- Does NOT open/attach any text editor to the newly created `.cpp` — no
  in-engine code editor exists or is added by this plan (this whole
  5-file series' own Non-Goal, restated here).
- Does NOT support renaming or deleting a project.
- Does NOT change `plugins/gte_plugin_abi/` or any EXISTING hot-reload
  file's own control flow — purely additive, alongside both.
- Does NOT unify `ActiveProjectAssemblyState` with any other "which project"
  concept — it is the ONE, single, new authority this whole 5-file series
  standardizes on (BIGSTEP_01 Section 4's own explicit warning).

### 3.4 — How to report progress

Each phase writes its own `PHASEn_COMPLETION_REPORT.md` in this same
folder: what was actually built, any real, mechanically-confirmed
deviation from that phase's own file (and why), and any NEW gap found the
same way this file's own Section 2.2 corrections were found. PHASE5 also
writes `CAMPAIGN_COMPLETION_REPORT.md`, summarizing the whole campaign.

Reading order for whoever implements this: this file, then `PHASE1_...md`
through `PHASE5_...md`, in numeric order — do not skip ahead, and re-read
each earlier phase's own completion report before starting the next.
