# PHASE1 — ProjectAssemblyHost Exact-Path Loaders & HotReloadEngineStateMutex Closure

Parent: `PHASE0_MASTER_STRATEGY.md` (read first).
Depends on: nothing new — builds directly on `editor-core-separation-13`'s
already-shipped `ProjectAssemblyHost`/`ProjectAssemblyRegistrationLedger`/
`HotReloadEngineStateMutex`.
Blocks: PHASE4 (the orchestrator's success/rollback load calls need these
two new public methods to exist).

---

## STEP 1 — The Goal

`ProjectAssemblyHost` gains two new PUBLIC methods that load ONE exact
`.dll` path on demand (no directory scan), reusable for BOTH "load the
freshly-compiled binary" and "load the backed-up binary on rollback" — and
every mutating `ProjectAssemblyHost` entry point (`TryLoadOneAssembly`,
`UnloadProjectAssembly`) locks `GetHotReloadEngineStateMutex()` around its
own body, closing the obligation that mutex's own header comment already
requires of "a future BIG-STEP 2/3 campaign."

## STEP 2 — The Situation

Confirmed, current, `src/Core/Plugins/ProjectAssemblyHost.h`:

```cpp
void LoadProjectAssemblies(const std::filesystem::path& outputDirectory, Core& core, EditorHost* editorHost);
std::size_t LoadedAssemblyCount() const noexcept { ... }
void UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer);
std::vector<std::string> GetLoadedAssemblyFileNames() const;

private:
    void TryLoadOneAssembly(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);
```

`TryLoadOneAssembly` (`.cpp` lines 90-161) already does everything a fresh
load needs: derives `_Game`/`_Editor` from the filename suffix,
`LoadLibraryW()`s it, resolves `GTE_RegisterProject` with the correct
1-arg/2-arg signature for that suffix, brackets the call with
`ProjectAssemblyRegistrationLedger::Instance().BeginRecordingFor()`/
`EndRecording()`, and on success appends a `LoadedAssembly{moduleHandle,
dllFileName}` to `m_loadedAssemblies`. It returns `void` and is `private`.

`UnloadProjectAssembly` (`.cpp` lines 167-206) does, in this exact
non-negotiable order: `renderer.WaitForGpuIdle()` ->
`ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor()`
-> sorted `FreeLibrary()` (Editor first) -> erase from `m_loadedAssemblies`.
**Confirmed: zero `std::mutex`/`GetHotReloadEngineStateMutex` usage
anywhere in this whole file today.**

`src/Core/Plugins/HotReloadEngineStateMutex.h`'s own header comment states:
*"A FUTURE BIG-STEP 2/3 CAMPAIGN MUST lock this same mutex around every ...
ProjectAssemblyHost load/unload call it makes during a reload cycle."*
`ProjectAssemblyRegistrationLedger.h`'s own header comment states the
matching other half of this contract: it must NEVER itself lock
`GetHotReloadEngineStateMutex()` (confirmed: `BeginRecordingFor`/
`EndRecording`/`RecordX()`/`UnregisterEverythingFor()`/`PeekEntry()` all
only ever take `ProjectAssemblyRegistrationLedger`'s OWN internal
`m_mutex`, never the shared one) — so nesting
`GetHotReloadEngineStateMutex()` around a `ProjectAssemblyHost` call that
itself calls into the ledger is safe, non-recursive, no deadlock risk.

## STEP 3 — The Plan

### 3.1 — `TryLoadOneAssembly` returns `bool`

In `ProjectAssemblyHost.h`, change the private declaration:

```cpp
bool TryLoadOneAssembly(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);
```

In `ProjectAssemblyHost.cpp`, change every early `return;` inside this
function (missing suffix match is already handled by the caller loop, but
the three failure branches — `LoadLibraryW` failure, `_Editor` with no
`editorHost`, missing `GTE_RegisterProject` export, for BOTH the
`isEditorAssembly` and `isGameAssembly` branches) to `return false;`, and
the function's final statement to `return true;` (right after
`m_loadedAssemblies.push_back(loaded);`). The one existing call site inside
`LoadProjectAssemblies()` (`entry.path()` loop) ignores the return value —
zero behavior change there (startup scan already treats every failure as a
logged warning and keeps scanning).

### 3.2 — Two new public methods

Header addition (`ProjectAssemblyHost.h`, public section, after
`GetLoadedAssemblyFileNames()`):

```cpp
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE1. Loads exactly ONE .dll at an EXACT, caller-supplied
// path (never scanning a directory) and calls its GTE_RegisterProject
// export - reused by PerformProjectAssemblyHotReload() (PHASE4) for BOTH
// the success-path fresh-compile load and the failure-path backup-restore
// load, since both are mechanically identical: "load this exact .dll file
// and call its export". Thin wrapper over the now-public-facing
// TryLoadOneAssembly() - same suffix-based _Game/_Editor dispatch, same
// ProjectAssemblyRegistrationLedger bracketing, same LoadedAssembly
// bookkeeping. Returns false (logged) if LoadLibraryW fails or the export
// is missing - never crashes.
bool LoadOneProjectAssemblyFromExactPath(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

// Same as above, but a NON-EXISTENT dllPath is a normal, valid, TRUE
// ("nothing to do here") result, not an error - mirrors
// ProjectAssemblyBuildRunner's own "no Editor target is a normal, valid
// case" precedent, for a project with no _Editor.dll at all.
bool LoadOneProjectAssemblyFromExactPathIfExists(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);
```

`.cpp` implementation:

```cpp
bool ProjectAssemblyHost::LoadOneProjectAssemblyFromExactPath(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    return TryLoadOneAssembly(dllPath, core, editorHost);
}

bool ProjectAssemblyHost::LoadOneProjectAssemblyFromExactPathIfExists(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    if (!std::filesystem::exists(dllPath)) {
        GTE_LOG_INFO("ProjectAssembly",
            "LoadOneProjectAssemblyFromExactPathIfExists: " + dllPath.string() +
            " does not exist - treating as a normal 'no Editor assembly for this project' case.");
        return true;
    }
    return LoadOneProjectAssemblyFromExactPath(dllPath, core, editorHost);
}
```

### 3.3 — Close the `GetHotReloadEngineStateMutex()` obligation

Add `#include "HotReloadEngineStateMutex.h"` to `ProjectAssemblyHost.cpp`.

In `TryLoadOneAssembly()`: take the lock as the VERY FIRST statement of the
function body (before the `fileName`/suffix computation), covering the
WHOLE function including the `GTE_RegisterProject` call and the
`m_loadedAssemblies.push_back()` — a comment at the lock site must state
explicitly that `ProjectAssemblyRegistrationLedger`'s own methods (called
from inside this same locked region) never themselves lock this mutex
(confirmed above), so this is safe, non-recursive nesting:

```cpp
bool ProjectAssemblyHost::TryLoadOneAssembly(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    // editor-core-separation-14 campaign (BIG-STEP 3), PHASE1 - closes the
    // obligation HotReloadEngineStateMutex.h's own header comment already
    // states ("a future BIG-STEP 2/3 campaign MUST lock this... around
    // every ... ProjectAssemblyHost load/unload call"). Safe to nest:
    // ProjectAssemblyRegistrationLedger's own methods (called below, via
    // GTE_RegisterProject's own registration calls) only ever take THEIR
    // OWN internal mutex, never this one (confirmed,
    // ProjectAssemblyRegistrationLedger.h's own header comment) - no
    // deadlock risk.
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());

    const std::string fileName = dllPath.filename().string();
    ... (unchanged body) ...
}
```

In `UnloadProjectAssembly()`: take the SAME lock as the very first
statement, covering the whole function (the "nothing loaded, no-op"
early-return included, so a concurrent `GetLoadedAssemblyFileNames()` read
can never straddle that decision):

```cpp
void ProjectAssemblyHost::UnloadProjectAssembly(const std::string& projectName, Core& core, Renderer& renderer)
{
    std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
    ... (unchanged body) ...
}
```

**Deliberately NOT locked here**: `LoadProjectAssemblies()` itself (the
startup directory scan) — it calls `TryLoadOneAssembly()` once per file,
and each individual call already takes/releases the lock internally now;
locking the OUTER loop too would just hold the mutex needlessly long during
startup (before `NetworkServer::Start()` even runs — no real contention
possible there anyway, matching this mutex's own documented "no real race
yet, added proactively" rationale).

**A note for PHASE4 to read again, not re-derive**: because
`TryLoadOneAssembly`/`UnloadProjectAssembly` now lock internally, PHASE4's
own orchestrator function must NEVER wrap a call to either of them in its
OWN outer lock on the same mutex (that would double-lock a non-recursive
`std::mutex` and deadlock immediately, since `std::mutex` is not
`std::recursive_mutex`). Confirm the type of `GetHotReloadEngineStateMutex()`
during PHASE4 implementation (`HotReloadEngineStateMutex.h` line 40 — a
plain `std::mutex&`) if this needs re-checking.

### 3.4 — Tier-1 tests

Locate the existing `ProjectAssemblyHostTest` file (search `tests/` for
`ProjectAssemblyHostTest` — `editor-core-separation-13`'s own
`CAMPAIGN_COMPLETION_REPORT.md` references
`ProjectAssemblyHostTest.UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp`,
so the file already exists) and add:
- `LoadOneProjectAssemblyFromExactPathOnANonExistentPathReturnsFalse` —
  construct a bare `ProjectAssemblyHost`, call
  `LoadOneProjectAssemblyFromExactPath()` with a made-up temp path, a
  default-constructed `Core`/`nullptr` `EditorHost*` if the existing test
  file's own fixture allows it (mirror whatever construction pattern the
  existing `UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp` test
  already uses for `Core`), assert `false`.
- `LoadOneProjectAssemblyFromExactPathIfExistsOnANonExistentPathReturnsTrue`
  — same setup, assert `true`, assert `LoadedAssemblyCount() == 0`
  afterward (proves the "no-op success" case never fakes a load).

## Definition of Done — this phase only

- [ ] `TryLoadOneAssembly` returns `bool`; its one existing call site
      compiles unchanged.
- [ ] `LoadOneProjectAssemblyFromExactPath()`/`...IfExists()` exist, public,
      compile, and are exercised by the two new Tier-1 tests above.
- [ ] `TryLoadOneAssembly()`/`UnloadProjectAssembly()` both lock
      `GetHotReloadEngineStateMutex()` around their whole body; a quick
      incremental build (`cmake --build build`, targeting `gte_core` and
      `GreatTamanaEngineTests` only — no full rebuild needed this phase)
      succeeds with zero new warnings.
- [ ] The two new Tier-1 tests pass (`ctest -C Debug --output-on-failure
      -R ProjectAssemblyHostTest`, an INCREMENTAL/filtered run only — the
      full suite runs in PHASE5, not here).
