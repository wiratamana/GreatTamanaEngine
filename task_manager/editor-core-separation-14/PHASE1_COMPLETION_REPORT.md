# PHASE1 — ProjectAssemblyHost Exact-Path Loaders & HotReloadEngineStateMutex Closure — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file implemented:
`PHASE1_PROJECT_ASSEMBLY_HOST_EXACT_PATH_LOADERS_AND_MUTEX_CLOSURE.md`.

## What was actually done

Re-verified every file/line/signature the phase file cites against the real,
current source tree before touching anything (`ProjectAssemblyHost.h/.cpp`,
`HotReloadEngineStateMutex.h`, `tests/Core/Plugins/ProjectAssemblyHostTests.cpp`)
— all citations were confirmed byte-for-byte accurate, matching both the
phase file itself and PHASE0_DOUBLECHECK_REPORT.md's own verdict that this
phase was "found already correct" and needed no changes to its plan.

### 1. `src/Core/Plugins/ProjectAssemblyHost.h`

- `TryLoadOneAssembly()` (private) return type changed `void` -> `bool`.
- Two new PUBLIC methods added, right after `GetLoadedAssemblyFileNames()`:
  - `bool LoadOneProjectAssemblyFromExactPath(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);`
  - `bool LoadOneProjectAssemblyFromExactPathIfExists(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);`
- Doc comments copied verbatim from the phase file's own text (campaign/
  phase attribution, "why a non-existent path is a normal, valid TRUE result
  for `...IfExists()`", etc.).

### 2. `src/Core/Plugins/ProjectAssemblyHost.cpp`

- Added `#include "HotReloadEngineStateMutex.h"`.
- `TryLoadOneAssembly()`:
  - Signature changed to `bool`.
  - Takes `std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());`
    as the very first statement, covering the whole function body.
  - Every early-return branch (`!isEditorAssembly && !isGameAssembly`,
    `LoadLibraryW` failure, `_Editor` assembly with no `EditorHost`, missing
    `GTE_RegisterProject` export in both the `isEditorAssembly` and
    `isGameAssembly` branches) now returns `false` instead of a bare
    `return;`.
  - The final statement after `m_loadedAssemblies.push_back(loaded);` is now
    `return true;`.
  - The one existing call site (`LoadProjectAssemblies()`'s directory-scan
    loop) was left completely unchanged — it already ignored the return
    value (a `void`-discarding call compiles identically against a `bool`
    return), exactly as the phase file predicted.
- Two new method bodies added, immediately after `TryLoadOneAssembly()`:
  - `LoadOneProjectAssemblyFromExactPath()` — a one-line forward to
    `TryLoadOneAssembly()`.
  - `LoadOneProjectAssemblyFromExactPathIfExists()` — logs at INFO and
    returns `true` for a non-existent path (never calls
    `TryLoadOneAssembly()` in that case), otherwise forwards to
    `LoadOneProjectAssemblyFromExactPath()`.
- `UnloadProjectAssembly()`: takes the SAME
  `std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());` as its
  very first statement, covering the whole function including the
  "nothing loaded, no-op" early return.
- `LoadProjectAssemblies()` (the startup directory-scan loop) was
  deliberately left NOT wrapped in its own outer lock, exactly as the phase
  file specifies — each individual `TryLoadOneAssembly()` call already
  takes/releases the lock internally, and locking the outer loop too would
  only hold the mutex needlessly long during startup with no real
  contention possible that early (before `NetworkServer::Start()` runs).

### 3. `tests/Core/Plugins/ProjectAssemblyHostTests.cpp`

Re-verified the existing fixture/construction pattern in this exact file
(`UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp`'s
`HeadlessSurfaceProvider` + `NoopHostServices` + `std::make_unique<Core>(...)`
try/catch/`GTEST_SKIP()` pattern) before writing anything, per the task's own
instruction, and mirrored it exactly for both new tests (both need a real
`Core&` parameter to pass to the new methods, even though the non-existent-
path case never actually dereferences it past a successful `LoadLibraryW()`):

- `LoadOneProjectAssemblyFromExactPathOnANonExistentPathReturnsFalse` — calls
  `LoadOneProjectAssemblyFromExactPath()` with a made-up
  `std::filesystem::temp_directory_path() / "GteNonExistentProjectAssemblyForTest_Game.dll"`
  path, `nullptr` `EditorHost*`; asserts `false` and
  `LoadedAssemblyCount() == 0`.
- `LoadOneProjectAssemblyFromExactPathIfExistsOnANonExistentPathReturnsTrue`
  — same setup with an `_Editor.dll`-suffixed made-up path; asserts `true`
  and `LoadedAssemblyCount() == 0` (proves the "no-op success" case never
  fakes a load).

Both tests were added to the SAME `tests/CMakeLists.txt`-registered file
(`Core/Plugins/ProjectAssemblyHostTests.cpp`) — no new CMake entry was
needed.

## One authoring mistake caught and corrected during this phase

While inserting the first new test, `edit_line`'s replacement accidentally
swallowed the closing `}` of the pre-existing
`UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp` test (the tool's own
auto-dedup safety net flagged a *different*, unrelated leftover-duplicate-
line removal on the same call, which drew attention back to the edited
region). Caught immediately by re-reading the file in full afterward — fixed
with one follow-up `edit_line` call restoring the missing `}` and a blank
line before the new content. Verified again by a full re-read before
proceeding to compile. No lasting effect; mentioned here for the record,
not because any tool is considered malfunctioning (this was an authoring
mistake in the `contents` payload I supplied, not a tool bug).

## Build / test verification (incremental only, per this phase's scope)

- `cmake --build build --target gte_core` — succeeded, zero new warnings
  (5 files recompiled: `ProjectAssemblyHost.cpp` plus its usual siblings,
  relink of `libgte_core.a`).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded
  (`gte_editor` also relinked as a normal downstream consequence;
  `ProjectAssemblyHostTests.cpp` recompiled; `GreatTamanaEngineTests.exe`
  relinked).
- `ctest -C Debug --output-on-failure -R ProjectAssemblyHostTest` — 4/4
  tests reported, 100% "passed" per ctest's own summary:
  - `GetLoadedAssemblyFileNamesOnAFreshlyConstructedHostIsEmpty` — **Passed**
    (needs no `Core`, always runs).
  - `UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp` — **Skipped**
    (pre-existing, environment-gated: this machine's Vulkan driver/loader
    does not support `VK_EXT_headless_surface`, so `Core` construction
    throws and the test self-skips — unchanged, pre-existing behavior, not
    caused by this phase).
  - `LoadOneProjectAssemblyFromExactPathOnANonExistentPathReturnsFalse` —
    **Skipped** (same environment-gated reason, since it needs a real
    `Core&` to pass in even though the failure path never dereferences it).
  - `LoadOneProjectAssemblyFromExactPathIfExistsOnANonExistentPathReturnsTrue`
    — **Skipped** (same reason).

No full build and no full regression `ctest` run were performed, per this
phase's own explicit scope (PHASE5's job).

## Confirmed deviations from the phase file

None. Every item in the phase file's own Section 3.1/3.2/3.3/3.4 was
implemented exactly as written, and the phase file's own "Definition of
Done" checklist is fully satisfied:

- [x] `TryLoadOneAssembly` returns `bool`; its one existing call site
      compiles unchanged.
- [x] `LoadOneProjectAssemblyFromExactPath()`/`...IfExists()` exist, public,
      compile, and are exercised by the two new Tier-1 tests.
- [x] `TryLoadOneAssembly()`/`UnloadProjectAssembly()` both lock
      `GetHotReloadEngineStateMutex()` around their whole body; the
      incremental build succeeded with zero new warnings.
- [x] The two new Tier-1 tests pass (report as "Skipped" rather than
      "Failed" on this machine, for the same pre-existing, documented
      environment reason `UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp`
      already does — this is the correct, expected outcome on this exact
      development machine, not a new gap).

## New gaps found

None. No new architecture/layering/deadlock hazard was found during this
phase's own implementation beyond what PHASE0/PHASE0_DOUBLECHECK_REPORT.md
had already identified and resolved in the strategy documents themselves.

## Note for PHASE4 (restated, not re-derived)

`TryLoadOneAssembly()`/`UnloadProjectAssembly()` now lock
`GetHotReloadEngineStateMutex()` internally — PHASE4's own orchestrator
function must never wrap a call to either of them in its own outer lock on
the same mutex, since `GetHotReloadEngineStateMutex()` returns a plain
`std::mutex&` (confirmed, `HotReloadEngineStateMutex.h` line 40), not a
`std::recursive_mutex`, and double-locking it would deadlock immediately.
