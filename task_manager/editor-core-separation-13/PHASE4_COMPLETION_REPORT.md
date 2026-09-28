# editor-core-separation-13 — PHASE4 COMPLETION REPORT

## Status: DONE

Implements PHASE4's own "Step 3: The Plan" in full, exactly as specified —
no more, no less. No later phase's work (PHASE5) was touched. PHASE3's own
shipped work (`ProjectAssemblyRegistrationLedger`) was confirmed present,
unmodified, before starting (`browse_dir`/`read_file` on
`src/Core/Plugins/ProjectAssemblyRegistrationLedger.h/.cpp`, both already
existing, per this session's own instruction). PHASE3_COMPLETION_REPORT.md
was read in full first — its answer to "is `ProjectAssemblyProbe`'s ledger
entry already non-empty before PHASE5 adds a custom component" is **yes**
(`panel_names`/`render_pass_names` populated, `component_type_names` empty)
— confirmed, unaffected, still true after this phase's own live
verification below (item 2).

---

## What was added / changed

### 1. `Renderer::WaitForGpuIdle()`'s doc comment (`src/Renderer/Renderer.h`)

Appended (no existing sentence removed) a paragraph naming
`ProjectAssemblyHost::UnloadProjectAssembly()` as a second sanctioned
caller, explaining it must run before any Project-Assembly-owned GPU
resource can be released via `FreeLibrary()`'s own static-destructor path
(Hazard 4), and restating this remains NOT safe to call from any per-frame
path.

### 2. `ProjectAssemblyHost.h`/`.cpp` (`src/Core/Plugins/`)

Added a forward declaration `class Renderer;` (this header still has zero
dependency on the complete `Renderer` type). Added two new public methods,
exact final signatures:

```cpp
void UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer);
std::vector<std::string> GetLoadedAssemblyFileNames() const;
```

`UnloadProjectAssembly()` implemented exactly as section 3.3 specifies: (1)
finds every `m_loadedAssemblies` entry whose `DeriveProjectNameFromDllFileName()`
matches `projectName` (reusing PHASE3's own existing helper, not
duplicating it) — an early return (logged INFO, no-op) if none match, so
`renderer.WaitForGpuIdle()` is never even called for a projectName that was
never loaded; (2) `renderer.WaitForGpuIdle()`; (3)
`ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor(projectName, core)`;
(4) `FreeLibrary()` on every matching handle, Editor entries first (sorted
via `dllFileName.ends_with("_Editor.dll")`), then erased from
`m_loadedAssemblies` in descending index order so earlier indices stay
valid while erasing. `GetLoadedAssemblyFileNames()` returns a plain
`std::vector<std::string>` copy of every `dllFileName`, never the raw
`HMODULE`. `#include "../../Renderer/Renderer.h"` added to the `.cpp`
(needed there for `Renderer::WaitForGpuIdle()`'s complete declaration);
`<algorithm>`/`<functional>` added for `std::sort`/`std::greater`.

**One bug caught and fixed during implementation, before ever compiling**:
my first pass at replacing this file's own top `#include` block
accidentally dropped the pre-existing `#include "ProjectAssemblyHost.h"`
line itself (the file's very own self-include, needed for the class
declaration used by every method defined in the `.cpp`). The very first
`gte_core` compile attempt caught this immediately and unambiguously (every
`ProjectAssemblyHost::` qualified definition failed with "'ProjectAssemblyHost'
has not been declared", and the free functions were parsed as bare `gte::`
namespace functions instead of member functions) — fixed by re-adding the
missing include, recompiled clean on the next attempt. Not a design
ambiguity, a pure editing mistake, caught by the compile check exactly as
intended.

### 3. `ProjectAssemblyBuildRunner.h`/`.cpp` (`src/Core/Plugins/`)

Three new functions, exact final signatures, all in `namespace gte`:

```cpp
std::filesystem::path ResolveProjectAssemblyOutputDirectory(const std::filesystem::path& executableDirectory);
bool BackupProjectAssemblyBinaries(const std::string& projectName, const std::filesystem::path& outputDirectory);
bool RestoreProjectAssemblyBinariesFromBackup(const std::string& projectName, const std::filesystem::path& outputDirectory);
```

`ResolveProjectAssemblyOutputDirectory()` is the one-line
`return executableDirectory / "project_assemblies";` the spec requires —
never calls `gte::ExecutableDirectory()` itself (layering constraint,
section 3.4, confirmed: this file compiles into `gte_core`,
`gte::ExecutableDirectory()` is `gte_editor`-tier). `BackupProjectAssemblyBinaries()`/
`RestoreProjectAssemblyBinariesFromBackup()` use a small internal anonymous
namespace of path helpers (`BackupDirectoryFor()`, `GameDllPath()`,
`EditorDllPath()`, `GameBackupPath()`, `EditorBackupPath()`) so the backup
slot naming convention (`<outputDirectory>/.hotreload_backup/<name>_Game.dll.bak`
/ `_Editor.dll.bak`) lives in exactly one place. Both use the non-throwing
`std::filesystem::copy_file(..., copy_options::overwrite_existing, std::error_code&)`
overload, never the throwing one. `_Editor.dll`/`.bak` is treated as
OPTIONAL on both the backup and restore side (only copied if it actually
exists at its source path) — `_Game.dll` missing is the ONE hard failure
condition for `BackupProjectAssemblyBinaries()` (returns `false`,
`GTE_LOG_ERROR`); a missing backup is the ONE hard failure condition for
`RestoreProjectAssemblyBinariesFromBackup()` (same). `create_directories()`
used (non-throwing overload) to ensure `.hotreload_backup/` exists before
the first copy.

`src/Editor/EditorHost.cpp`'s own `LoadProjectAssemblies()` call site
(previously line 278) updated from
`m_core.LoadProjectAssemblies(gte::ExecutableDirectory() / "project_assemblies", this);`
to
`m_core.LoadProjectAssemblies(ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory()), this);`
— still a call on `m_core` (a member function), exactly as the spec
requires. `#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"` added
to `EditorHost.cpp`.

### 4. `Core::GetProjectAssemblyHost()` (`src/Core/Core.h`)

New public accessor, mirroring `GetRenderer()`'s own existing precedent
exactly:

```cpp
ProjectAssemblyHost& GetProjectAssemblyHost() noexcept { return m_projectAssemblyHost; }
```

`Core.h` already `#include`s `Plugins/ProjectAssemblyHost.h` (confirmed
before adding this — no new include needed for this one change).

### 5. `EditorHotReloadDebugCapability.h`/`.cpp` (`src/Editor/`)

Forward-declares `class ProjectAssemblyHost;` in the header (a pointer
member needs no complete type). Added a new private member and public
setter, exact final signature:

```cpp
void SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept;
private:
    ProjectAssemblyHost* m_projectAssemblyHost = nullptr;
```

`IHotReloadDebugCapability`'s own interface (`Core/EditorCapabilities.h`)
was NOT touched — confirmed by re-reading it after this phase's edits;
`GetLoadedAssemblyFileNames()`'s own override signature is unchanged. The
class's default, no-argument constructor is also unchanged — `SetProjectAssemblyHost()`
is a plain setter, never a constructor parameter (impossible here — see
Step 2's own reasoning, restated in this method's own doc comment: the
class it belongs to is a namespace-scope static constructed before `main()`,
strictly before any `Core`/`ProjectAssemblyHost` object exists).

`.cpp`: added `#include "../Core/Plugins/ProjectAssemblyHost.h"`.
`GetLoadedAssemblyFileNames()`'s placeholder body (`return {};`) replaced
with the real
`return m_projectAssemblyHost->GetLoadedAssemblyFileNames();` (still inside
the existing `std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());`).
`SetProjectAssemblyHost()`'s body is a plain `m_projectAssemblyHost = &projectAssemblyHost;`.

### 6. `EditorHost.cpp`'s constructor body wiring

Immediately after the existing `m_core.SetEditorLayerHook(m_editorLayer.get());`
call and strictly before `m_networkServer.Start(8080)` (still further down,
inside `#if GTE_ENABLE_NETWORK`), added:

```cpp
s_editorHotReloadDebugCapability.SetProjectAssemblyHost(m_core.GetProjectAssemblyHost());
```

Placed UNCONDITIONALLY (not inside the `#if GTE_ENABLE_PROJECT_ASSEMBLIES`
guard around `m_core.LoadProjectAssemblies(...)` a few lines below it) —
`Core::m_projectAssemblyHost` is an unconditional `Core` member, so this
setter call is always safe/correct regardless of that build flag, exactly
as the spec requires.

### 7. New Tier-1 test files

- `tests/Core/Plugins/ProjectAssemblyHostTests.cpp` (new file) — 2 tests:
  `GetLoadedAssemblyFileNamesOnAFreshlyConstructedHostIsEmpty` (no `Core`/
  `Renderer` needed at all) and
  `UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp` (reuses
  `CoreHeadlessConstructionTests.cpp`'s own `HeadlessSurfaceProvider` +
  `NoopHostServices` fixture precedent, `GTEST_SKIP()`-ing identically on
  this machine's known `VK_EXT_headless_surface` gap).
- `tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`
  (new file) — 6 tests, using a throwaway `std::filesystem::temp_directory_path()`
  subfolder (never the real `project_assemblies/` folder), covering every
  case section 3.6 lists: both-files backup+byte-identity check, delete +
  restore round-trip, Editor-dll-absent success case, missing-`_Game.dll`
  failure case, missing-backup restore failure case, plus one extra
  `ResolveProjectAssemblyOutputDirectory()` pure-function check.

Both new file paths added to `tests/CMakeLists.txt`'s explicit list
(immediately after `Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp`),
since this repo's test build does not glob sources.

---

## Compile check (incremental, exactly the three commands PHASE4 section 3.7 specifies — no full build, no ctest)

```
cmake --build build --target gte_core
cmake --build build --target gte_editor
cmake --build build --target GreatTamanaEngineTests
```

All three succeeded cleanly on the second attempt (see the self-include bug
noted above, item 2, caught and fixed on the first attempt — zero remaining
errors/warnings on the second):

```
[2/3] Linking CXX static library libgte_core.a
```
```
[22/23] Linking CXX static library libgte_editor.a
```
```
[6/7] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

Filtered test run
(`build\tests\GreatTamanaEngineTests.exe --gtest_filter=ProjectAssemblyHostTest*:ProjectAssemblyBuildRunnerBackupRestoreTest*`):

```
[==========] Running 8 tests from 2 test suites.
[ RUN      ] ProjectAssemblyBuildRunnerBackupRestoreTest.BackupCopiesBothGameAndEditorDllsWithIdenticalByteContent    [OK]
[ RUN      ] ProjectAssemblyBuildRunnerBackupRestoreTest.RestoreBringsBackTheOriginalGameDllAfterItWasDeleted         [OK]
[ RUN      ] ProjectAssemblyBuildRunnerBackupRestoreTest.BackupSucceedsWithOnlyGameDllPresentNoEditorDll              [OK]
[ RUN      ] ProjectAssemblyBuildRunnerBackupRestoreTest.BackupFailsWhenGameDllItselfIsMissing                       [OK]
[ RUN      ] ProjectAssemblyBuildRunnerBackupRestoreTest.RestoreFailsForAProjectWithNoExistingBackup                  [OK]
[ RUN      ] ProjectAssemblyBuildRunnerBackupRestoreTest.ResolveProjectAssemblyOutputDirectoryAppendsTheExpectedSubfolder [OK]
[ RUN      ] ProjectAssemblyHostTest.GetLoadedAssemblyFileNamesOnAFreshlyConstructedHostIsEmpty                      [OK]
[ RUN      ] ProjectAssemblyHostTest.UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp                              [SKIPPED]
[  PASSED  ] 7 tests.
[  SKIPPED ] 1 test.
```

The 1 skip is the same, confirmed, pre-existing MACHINE limitation
PHASE3_COMPLETION_REPORT.md already documented: this development machine's
Vulkan driver/loader does not report `VK_EXT_headless_surface`
(`vkCreateInstance failed (VkResult=-7)`) — not a bug in this phase's new
code. All 6 file-only (no-`Core`-needed) backup/restore tests, which need
no Vulkan at all, passed unconditionally.

Also re-ran the two directly-adjacent, already-existing suites
(`ProjectAssemblyRegistrationLedgerTest*`, `CoreHeadlessConstructionTest*`)
to confirm zero regression from this phase's own edits to shared files
(`Core.h`, `EditorHost.cpp`, `ProjectAssemblyHost.h/.cpp`): 4 passed, 3
skipped (identical, pre-existing Vulkan-capability skips, same as PHASE3's
own report), none newly broken.

`cmake --build build --target GreatTamanaEditor` was also run (not part of
this phase's own mandated section 3.7 list, but needed for the live
verification below — mirrors PHASE1/2/3's own identical precedent, each
documented doing the same thing for the same reason): succeeded cleanly.

---

## Live verification

`run_app_background`'d the freshly-rebuilt `GreatTamanaEditor.exe` (PID
13148), then via `gte_send_request`:

1. **`GET /project_assembly/debug/loaded_assemblies`** →
   ```json
   {"dll_file_names":["ProjectAssemblyProbe_Editor.dll","ProjectAssemblyProbe_Game.dll"]}
   ```
   Real, live data — no longer the placeholder `{"dll_file_names":[]}`.
   Confirms the whole wiring chain end-to-end: `EditorHost`'s constructor
   body → `Core::GetProjectAssemblyHost()` → `SetProjectAssemblyHost()` →
   `EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()` →
   `ProjectAssemblyHost::GetLoadedAssemblyFileNames()` → the real
   `m_loadedAssemblies` list populated by `LoadProjectAssemblies()` at
   startup.
2. **`GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe`** →
   ```json
   {"component_type_names":[],"panel_names":["Probe Panel"],"project_name":"ProjectAssemblyProbe","render_pass_names":["ProjectAssemblyProbe.FillTexture"]}
   ```
   Unaffected by this phase, byte-for-byte identical to PHASE3's own
   documented baseline — confirms this phase added capability without
   touching the ledger's own recorded state.
3. **`GET /list_tabs`** →
   `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel","Probe Panel"]}`
   — unchanged from before this phase.
4. **`GET /get_swapchain`** → rendered screenshot confirmed, visually,
   identical to PHASE3's own documented appearance: "Probe Panel" tab
   active, "Hello from a real Project Assembly Editor panel." + "Click me"
   button + "Clicked 0 time(s)" all present and rendering normally.
5. **`GET /get_logs?category=ProjectAssembly`** — 5 entries, all
   pre-existing/expected (identical to PHASE3's own documented set) — no
   new warning/error.
6. **`GET /get_logs`** (full, 36 entries) — only pre-existing, unrelated
   `PluginHost`/`RenderFeatureCompositor` informational/warning entries
   (the same known multi-render-feature-plugin condition PHASE1/2/3 already
   documented) — nothing attributable to this phase's new code.

**`UnloadProjectAssembly()` was deliberately NEVER called against this
live, running engine in this phase** — per this phase's own explicit
instruction, that full live isolation test (with the probe fixture's future
custom component type in place) is PHASE5's own job.

`stop_app_background(pid: 13148)` cleanly terminated the process afterward.

---

## Deviations found versus this phase file's own instructions

1. **A self-inflicted, immediately-self-corrected editing mistake** (see
   "What was added, item 2" above) — my first pass at splicing new
   `#include` lines into `ProjectAssemblyHost.cpp`'s top accidentally
   dropped the file's own pre-existing `#include "ProjectAssemblyHost.h"`
   self-include. Caught by the very first `gte_core` compile attempt
   (unambiguous "class has not been declared" errors), fixed immediately,
   recompiled clean. Not a design ambiguity, a pure line-editing slip,
   caught exactly as this session's own compile-check workflow is designed
   to catch.
2. `cmake --build build --target GreatTamanaEditor` is not part of this
   phase's own mandated section 3.7 compile-check list (which lists only
   `gte_core`/`gte_editor`/`GreatTamanaEngineTests`) — run anyway, once,
   purely so the live-verification step (section 3.8) reflects the actual
   current code, mirroring PHASE1/2/3's own identical, already-documented
   precedent.
3. No other deviation — every other section (3.1-3.6, 3.8) implemented
   exactly as specified, including the exact wording/structure of every doc
   comment the spec provides verbatim, and the exact function/parameter
   names/order given in the spec's own code sketches.

---

## Definition of Done — checked off

- [x] `Renderer::WaitForGpuIdle()`'s doc comment lists this feature as a
      second sanctioned caller.
- [x] `ProjectAssemblyHost::UnloadProjectAssembly()` exists, calls
      `WaitForGpuIdle()` → ledger teardown → `FreeLibrary()` in that exact
      order, Editor-before-Game.
- [x] `ProjectAssemblyHost::GetLoadedAssemblyFileNames()` exists.
- [x] `BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`
      exist in `ProjectAssemblyBuildRunner`, both proven by isolated
      (temp-directory) tests, including the "no `_Editor.dll`" and
      "missing `_Game.dll`" edge cases.
- [x] `EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()`'s body
      is real, not a placeholder; `IHotReloadDebugCapability`'s own
      interface signature is unchanged.
- [x] Live `GET /project_assembly/debug/loaded_assemblies` shows the real,
      current `.dll` file set
      (`["ProjectAssemblyProbe_Editor.dll","ProjectAssemblyProbe_Game.dll"]`).
- [x] `PHASE4_COMPLETION_REPORT.md` exists (this file); changes committed
      to git (see commit following this report).
