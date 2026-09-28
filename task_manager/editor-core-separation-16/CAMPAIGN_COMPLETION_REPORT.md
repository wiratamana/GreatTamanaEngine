# editor-core-separation-16 — CAMPAIGN COMPLETION REPORT
## On-Engine Project Workflow — BIG-STEP 2 of 5: "Create New Project"

Status: **DONE**. All five phases complete; full clean build + full
`ctest` regression pass performed in PHASE5 with zero regressions.

## What was actually built, phase by phase

**PHASE1 — Live-Safe Reconfigure Helper & Source-Root Resolver.**
`ResolveProjectAssemblySourceRootDirectory()` (reads `CMAKE_HOME_DIRECTORY`
out of `<buildDirectory>/CMakeCache.txt`, appends `"Projects"`) and
`RunPlainCMakeReconfigureAndWait()` (LDD-CP2's own always-reconfigure
mechanism), both in `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`.
Required refactoring the pre-existing `RunOneBuildTarget()`'s own
`CreateProcessW()`/pipe-reading machinery into a shared, generic
`RunChildProcessAndWait()` helper both functions now call. 4 new Tier-1
tests in a new file, `tests/Core/Plugins/
ProjectAssemblyBuildRunnerSourceRootTests.cpp` (no existing sibling file to
extend, per this phase's own Correction 1).

**PHASE2 — Strict Name Validator & Existing-Route Hardening.**
`IsValidProjectAssemblyIdentifierName()` (`^[A-Za-z_][A-Za-z0-9_]*$` +
Windows-reserved-device-name rejection), a new, dependency-free
`gte_core`-tier header/source pair,
`src/Core/Plugins/ProjectAssemblyNameValidation.h/.cpp`. 10 new Tier-1
tests. `NetworkRoutes.cpp`'s pre-existing `ParseProjectNameQuery()` was
hardened (LDD-CP3) to call this same validator instead of only checking for
an empty string, backward-compatibly (every existing test/production name
already satisfies the stricter rule).

**PHASE3 — `ActiveProjectAssemblyState`, `IProjectLifecycleCapability` &
`EditorProjectLifecycleCapability`.** `ActiveProjectAssemblyState`
(`src/Editor/ActiveProjectAssemblyState.h/.cpp`, a Meyers singleton) built
FULLY per LDD-CP1 (never a placeholder) - `GetActive()` re-derives
`isCompiled`/`isLoaded` fresh on every call, the latter genuinely locking
`GetHotReloadEngineStateMutex()`. `IProjectLifecycleCapability` (new
interface, `src/Core/EditorCapabilities.h`) and its real implementation,
`EditorProjectLifecycleCapability` (`src/Editor/
EditorProjectLifecycleCapability.h/.cpp`) - `CreateNewProjectAssembly()`'s
real body: guard `GTE_ENABLE_PROJECT_ASSEMBLIES` -> validate name -> resolve
build/source directories -> reject an already-existing folder/file -> write
the 3-file scaffold (byte-for-byte `cmake/templates/
ProjectAssemblyExports.h` copy + generated `CMakeLists.txt`/`<Name>Game.cpp`
stubs) -> LDD-CP2's unconditional reconfigure -> mark active -> report
success/failure. Wired into `EditorHost.cpp`'s constructor as a
namespace-scope static, mirroring `EditorHotReloadDebugCapability`'s own
precedent exactly.

**PHASE4 — HTTP Route, `EditorContext` Fields & the "Project" Menu /
`NewProjectWindow`.** `NetworkServer`'s 9th constructor parameter
(`IProjectLifecycleCapability*`), `POST /project_assembly/create_project`
(reads `name` directly, deliberately bypassing the weaker
`ParseProjectNameQuery()`, 503/400/200 per the established convention),
`EditorContext.h`'s 4 new status-toast fields, `NewProjectWindow` (a new,
non-dockable floating ImGui window - name field, inline red error on
failure, Cancel/Create), `IEditorLayer::SetProjectLifecycleCapability()`
(new pure-virtual setter, `NullEditorLayer`/`ImGuiEditorLayer` both
implement it), and `DockLayout.cpp`'s new top-level "Project" menu (New
Project... + two disabled, reserved placeholders, "Open Project.../
Compile").

**PHASE5 — Full Live Verification & Campaign Closeout.** New end-to-end
test file, `tests/Network/CreateProjectEndpointEndToEndTests.cpp` (3 new
tests, real `NetworkServer` + real `EditorProjectLifecycleCapability`,
option (b) from the phase file's own left-open design choice - tests the
real production resolution path against a genuinely unique, disposable
project name, cleaned up in `TearDown()`). Full live, HTTP-driven
verification of every Definition-of-Done item from the source document's
own Step 8 (create/duplicate/4-invalid-names/compile_only, all against a
real running `GreatTamanaEditor.exe`). One genuinely stale
`docs/conventions/project-assembly-system.md` claim ("no scaffolding
wizard") found and superseded with a new section. A full clean build
(589/589 steps) + full `ctest` regression pass (**1951/1951 tests passing,
100%, 7 legitimate environment-gated skips** - exactly `1934` (this
campaign's own starting baseline, `editor-core-separation-15`'s own final
count) `+ 4` (PHASE1) `+ 10` (PHASE2) `+ 3` (PHASE5), zero unexplained
delta, zero regressions).

## Every real, mechanically-confirmed deviation from any phase's own file, and why

- **PHASE1**: one purely cosmetic naming choice (a trailing default
  argument's exact placement) - zero functional deviation.
- **PHASE2**: none in the shipped result - a transient tool-side auto-dedup
  artifact during editing was caught and corrected before the build ran.
- **PHASE3**: none.
- **PHASE4**: one real, honest, disclosed gap - STEP 8's own item 6 (a
  screenshot of the OPENED `NewProjectWindow`) could not be performed, since
  no HTTP/automation route exists anywhere in this codebase to open an
  on-demand floating ImGui window by name (unlike `GET /activate_tab`,
  which only brings an already-docked PANEL to the front, or the Frame
  Debugger's own dedicated `GET /frame_debugger/open`, built specifically
  for that one campaign). The menu's own genuine presence/render
  correctness WAS confirmed via a real screenshot; the window's own code
  was reviewed line-by-line against its phase file's exact sample instead
  of being screenshot-verified live.
- **PHASE5**: the SAME gap as PHASE4 (STEP 2's own item 4, the identical
  underlying limitation) - restated, not newly introduced, and not treated
  as blocking since the underlying `CreateNewProjectAssembly()` behavior it
  would have re-proven was already fully proven end-to-end via the HTTP
  path (LDD-PW5's "one function, two callers" rule).

## LDD-CP2 "was the auto-reconfigure actually load-bearing on this machine" — stated plainly

**No, not on this development machine, and this was confirmed TWICE,
independently, by two different phases (PHASE3's own scratch smoke test,
then PHASE5's own live STEP 2 verification), both times with the same
result.** The plain, pre-existing `file(GLOB CONFIGURE_DEPENDS ...)`
auto-pickup mechanism (root `CMakeLists.txt`'s `Projects/*` discovery loop)
already, on its own, correctly detects a newly-created (or newly-deleted)
`Projects/<Name>/` folder and triggers CMake's own automatic
"GLOB mismatch!" reconfigure the very next time ANY `cmake --build` runs -
no explicit `cmake -S/-B` command was ever the thing that made the
difference here. LDD-CP2's own explicit, unconditional
`RunPlainCMakeReconfigureAndWait()` call is therefore a safe, harmless,
redundant belt-and-suspenders step on THIS machine - but it remains the
right permanent design decision regardless, exactly per its own original
reasoning: a different machine, a different CMake version, or a
differently-configured build tree might not have this same "glob-mismatch
auto-detected on the next arbitrary build" behavior, and this campaign's
own code deliberately never gambles on that being universally true. This
is informational, permanent documentation for a future reader - it does
not change, and never changed, any code path.

## New gaps found during this campaign, for the NEXT campaign ("Open Project", BIG-STEP 3) to know about

- **No HTTP-drivable way to open/screenshot a floating, on-demand ImGui
  window (`NewProjectWindow` here)** - if "Open Project" needs its own
  similar floating window (a file/folder picker, a project list, etc.),
  the exact same screenshot-verification gap will recur unless a
  dedicated `GET /..._window/open`-style route is added for it too,
  mirroring the Frame Debugger's own precedent
  (`GET /frame_debugger/open`). This was flagged once by PHASE4, and again,
  independently, by PHASE5 hitting the identical limitation - it is a real,
  repeatable pattern in this codebase's current automation surface, not a
  one-off.
- **`ActiveProjectAssemblyState`'s real, shipped shape** (relevant since
  BIGSTEP_01's own Section 3 is what the "Open Project" campaign will read
  next): confirmed exactly as PHASE0/PHASE3 described it -
  `Instance().SetActive(name, sourceDirectory)`,
  `Instance().Clear()`, `Instance().SetProjectAssemblyHost(...)` (called
  once, from `EditorHost`'s constructor), and `Instance().GetActive()`
  returning a fresh `ActiveProjectAssemblyInfo` snapshot
  (`hasActiveProject`/`name`/`sourceDirectory`/`assetsDirectory`/
  `isCompiled`/`isLoaded`) on every single call, never cached. No HTTP route
  exposes this state directly yet (by this campaign's own explicit,
  deliberate scope decision - "Open Project" is the natural place to add
  one, since it will be the first campaign that needs to read/display it
  externally). `SetActive()` is currently called from exactly ONE
  production call site (`CreateNewProjectAssembly()`'s own success path) -
  a future "Open Project" implementation calling `SetActive()` a second
  time, for a DIFFERENT already-existing project, is a new, not-yet-tested
  code path worth adding coverage for when that campaign begins.
- **No other new structural gaps found.** Every finding/correction PHASE0
  itself already documented (Section 2.2's three corrections) remained
  accurate and unchanged throughout all five phases - none of them were
  found to be wrong or incomplete during implementation.

## Verification summary

- Full clean build (`cmake --build build --target clean` then
  `cmake --build build`): 589/589 steps, zero errors, zero new compiler
  warnings.
- Full `ctest -C Debug --output-on-failure`: **1951/1951 tests passing
  (100%)**, 7 legitimate environment-gated skips (unchanged in kind/count
  from the `editor-core-separation-15` baseline of 1934/7).
- Live, HTTP-driven, end-to-end verification against a real running
  `GreatTamanaEditor.exe`: create -> duplicate-rejected -> 4 invalid names
  all rejected before any filesystem write -> `compile_only` builds the
  brand-new project's `_Game.dll` successfully on the first try -> clean
  removal + rebuild returning the tree to its prior state.
- `git_status`: only this campaign's real, intended files
  (`docs/conventions/project-assembly-system.md`, `tests/CMakeLists.txt`
  modified; the new test file untracked, staged/committed alongside every
  other phase's own production files) - no scratch project folder or
  throwaway debug code survives anywhere.

## Explicit Non-Goals this campaign correctly stayed within (per PHASE0's own Section 3.3)

- Did NOT implement "Open Project" (BIG-STEP 3), "Create Script Asset"
  (BIG-STEP 4), or the "Compile" menu item's real action (BIG-STEP 5) - the
  "Project" menu's "Open Project..."/"Compile" items remain disabled
  placeholders, reserved by name.
- Did NOT scaffold an `Assets/Editor/` folder for a new project.
- Did NOT auto-compile a freshly created project.
- Did NOT open/attach any code editor.
- Did NOT support renaming or deleting a project.
- Did NOT change `plugins/gte_plugin_abi/` or any existing hot-reload
  file's own control flow.
- Did NOT unify `ActiveProjectAssemblyState` with any other "which
  project" concept.

This closes the `editor-core-separation-16` campaign (On-Engine Project
Workflow plan, BIG-STEP 2, "Create New Project") for good.
