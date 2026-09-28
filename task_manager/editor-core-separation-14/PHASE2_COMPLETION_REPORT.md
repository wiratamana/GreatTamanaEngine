# PHASE2 — Shared Synchronous Build Helper & Poll-Based Message Pump Hook — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file implemented:
`PHASE2_SHARED_SYNCHRONOUS_BUILD_HELPER_AND_MESSAGE_PUMP.md`.

## What was actually done

Re-verified every file/line/function-name citation the phase file makes
against the real, current source tree before touching anything
(`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`,
`tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`) —
every citation (line ranges for `g_inFlightMutex`/`RunOneBuildTarget`/
`RunBuildThreadBody`/`TriggerProjectAssemblyCompile`) was confirmed
byte-for-byte accurate against the live `.cpp` file (137-240, 250-285,
289-306 respectively), matching PHASE0_DOUBLECHECK_REPORT.md's own verdict.
Also re-read `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE0_DOUBLECHECK_REPORT.md`, and `PHASE1_COMPLETION_REPORT.md` first, per
the task's instructions — PHASE1 touches a different file
(`ProjectAssemblyHost.h/.cpp`) and reported no gaps or deviations relevant
to this phase's own scope.

### 1. `src/Core/Plugins/ProjectAssemblyBuildRunner.h`

- Added `#include <functional>`.
- Added the new `BuildOutcome` struct (`success`, `exitCode`,
  `finalSummaryLine` — the last field reserved/unused, exactly as the phase
  file specifies) immediately after the `namespace gte {` opening, before
  `TriggerProjectAssemblyCompile()`'s own declaration.
- Declared `BuildOutcome RunProjectAssemblyBuildAndWait(const std::string&
  projectName, const std::string& buildDirectory, const
  std::function<void()>& onIdleTick = {})` and `bool
  TryRunProjectAssemblyBuildSynchronously(const std::string& projectName,
  const std::string& buildDirectory, BuildOutcome& outOutcome, const
  std::function<void()>& onIdleTick = {})`, both with doc comments copied
  (with minor wording touch-ups) from the phase file's own text.
- `TriggerProjectAssemblyCompile()`'s own declaration/doc-comment was left
  completely unchanged, just relocated a few lines down from where the new
  declarations were inserted.

### 2. `src/Core/Plugins/ProjectAssemblyBuildRunner.cpp`

- `RunOneBuildTarget()` gained a new trailing parameter, `const
  std::function<void()>& onIdleTick = {}` (default argument supplied at its
  one definition site, since it has no separate forward declaration in this
  file — anonymous-namespace, single-definition function).
- Replaced the old, plain blocking `while (ReadFile(...) && bytesRead > 0)`
  loop with the exact `PeekNamedPipe()`-driven poll loop the phase file's
  own Section 3.2 specifies: `PeekNamedPipe()` reports 0 bytes available ->
  invoke `onIdleTick` (if non-empty) -> `Sleep(50)` -> `continue`;
  otherwise `ReadFile()` the available bytes and run the UNCHANGED per-line
  classification body. A `PeekNamedPipe()` failure (pipe closed / genuine
  error) breaks the loop exactly like the old `ReadFile()` returning false
  used to. Everything after the loop (`CloseHandle(readPipe)`,
  `WaitForSingleObject()`, exit-code retrieval) is byte-for-byte unchanged.
- Extracted `RunBuildThreadBody`'s own former inline Game-then-Editor
  sequencing body into the new, non-anonymous, header-declared
  `RunProjectAssemblyBuildAndWait()` — identical sequencing/target-missing
  heuristic, byte-for-byte, now returning a `BuildOutcome` instead of a bare
  `int`. Defined OUTSIDE the file's own anonymous namespace (after its
  closing `} // namespace`), matching the header's declaration — it can
  still call the anonymous-namespace `RunOneBuildTarget()` (anonymous-
  namespace names remain visible in the enclosing `gte` namespace scope for
  the rest of the translation unit).
- `RunBuildThreadBody()` is now a thin wrapper: calls
  `RunProjectAssemblyBuildAndWait(projectName, buildDirectory)` (no
  `onIdleTick` — a background thread owns no window to pump messages for),
  then reproduces the EXACT original final log line text ("Build finished
  with exit code N - relaunch GreatTamanaEditor.exe to use the result
  (Project Assemblies are not hot-reloaded, see PHASE0_MASTER_STRATEGY.md,
  LDD4).") using `outcome.exitCode`, then `ClearInFlight()` +
  `completionFlag->store()`, unchanged.
- Added `TryRunProjectAssemblyBuildSynchronously()` exactly as specified:
  `TryMarkInFlight()` -> on rejection, logs a warning and returns `false`
  (`outOutcome` left untouched); on success, calls
  `RunProjectAssemblyBuildAndWait(projectName, buildDirectory, onIdleTick)`,
  stores the result into `outOutcome`, `ClearInFlight()`, returns `true`.
  Shares the exact same `g_inFlightProjects` guard `TriggerProjectAssemblyCompile()`
  already uses (defined in the same file's anonymous namespace, untouched).
- `TriggerProjectAssemblyCompile()` itself is completely unchanged.

### 3. `tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`

Added the new concurrency test to this EXISTING, already-registered file
(no `tests/CMakeLists.txt` change needed), per PHASE0_DOUBLECHECK_REPORT.md's
own correction #6 and this phase file's own Section 3.5:

- `ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive` proves
  the shared in-flight guard rejects a second synchronous call for the SAME
  project name while the first is still genuinely running. Deterministic
  design (chosen over a pure-luck "launch two threads and hope they race"
  shape, which this phase file's own text explicitly leaves up to the
  implementer): `buildDirectory` is a real, EXISTING `TempOutputDirectory`
  (so `CreateProcessW()` genuinely succeeds and spawns a real `cmake.exe`
  child) with no `CMakeCache.txt` in it, so the build fails fast (cmake
  reports it cannot load the cache) without touching any real project. A
  freshly spawned child process always needs non-zero wall-clock time to
  even begin executing, so the outer call's own FIRST `PeekNamedPipe()`
  poll is, in practice, guaranteed to see zero bytes available and invoke
  `onIdleTick` at least once — the test uses that one guaranteed callback
  to synchronously launch and `join()` a SECOND
  `TryRunProjectAssemblyBuildSynchronously()` call for the identical
  project name, on a second thread, from strictly inside the window where
  the outer call's own `TryMarkInFlight()` has already succeeded and not
  yet been cleared. Asserts: the outer call was accepted (`true`) and
  failed to build (`success == false`, since neither directory is a real
  CMake build tree); `onIdleTick` genuinely fired (a hard `EXPECT_TRUE`,
  not a silent skip, so a future timing regression is visible instead of
  silently un-exercised); and the nested, genuinely-overlapping call was
  rejected (`false`).

## Build / test verification (incremental only, per this phase's scope)

- `cmake --build build --target gte_core` — succeeded (1 file recompiled:
  `ProjectAssemblyBuildRunner.cpp`, relink of `libgte_core.a`), zero new
  warnings.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded
  (`ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` recompiled,
  `gte_editor`/`GreatTamanaEngineTests.exe` relinked as a normal downstream
  consequence), zero new warnings.
- `ctest -C Debug --output-on-failure -R ProjectAssemblyBuildRunner` — 7/7
  tests passed (the 6 pre-existing backup/restore tests plus the new
  concurrency test, which completed in 0.38s, confirming the deterministic
  design worked exactly as intended on the first real run — no flake).
- Live smoke check (Definition of Done's own required check): built and
  launched `GreatTamanaEditor.exe`
  (`cmake --build build --target GreatTamanaEditor`, `run_app_background`),
  then `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
  (`{"started":true}`) followed by polling
  `GET /get_logs?category=ProjectAssemblyBuild`. Observed EXACTLY the
  expected shape: `"Starting build for Project Assembly 'ProjectAssemblyProbe'
  (build directory: ...)..."`, per-line `ninja`/`cmake` build output, a
  build failure (the linker could not overwrite `ProjectAssemblyProbe_Game.dll`
  because THIS SAME running instance already has it `LoadLibraryW()`'d — the
  pre-existing, documented, permanent "cannot recompile an already-loaded
  Project Assembly in the same instance" limitation from `AGENTS.md`'s own
  "Project Assembly System" section, not a new bug), `"'ProjectAssemblyProbe_Game'
  target build failed (exit code 1) - skipping the '_Editor' target
  attempt."`, and the final `"Build finished with exit code 1 - relaunch
  GreatTamanaEditor.exe to use the result (Project Assemblies are not
  hot-reloaded, see PHASE0_MASTER_STRATEGY.md, LDD4)."` line — byte-for-byte
  the same shape `editor-core-separation-12`'s own documented check #7
  describes, confirming the async path's own observable behavior is
  completely unchanged by this phase's refactor. Stopped the background
  process afterward.

No full build and no full regression `ctest` run were performed, per this
phase's own explicit scope (PHASE5's job).

## Confirmed deviations from the phase file

None in substance. Two purely cosmetic/organizational choices, both within
the phase file's own explicitly-left-open latitude:

- The phase file's own Section 3.5 pseudocode sketch suggested a
  non-existent build directory as the "fails fast" scenario, but explicitly
  left "the exact assertion shape... to the implementer" and flagged this as
  "inherently a timing-sensitive test." A non-existent directory makes
  `CreateProcessW()` itself fail immediately (no child ever spawned, so
  `onIdleTick` never fires), which would make the two-thread race window a
  matter of pure OS scheduling luck (typically far too short to reliably
  observe). This phase instead used a real, EXISTING (but cache-less) temp
  directory specifically so a real child process spawns and `onIdleTick`
  fires deterministically, then used that one guaranteed callback to force
  genuine overlap rather than hoping for it — this is the same
  "deterministic version... e.g. by having the first thread hold a
  manually-inserted small Sleep() proxy" latitude the phase file's own text
  grants, just implemented via a real (but harmless, fast, temp-directory-only)
  child process instead of a literal `Sleep()` stand-in, since
  `TryMarkInFlight()`/the in-flight guard are not exposed via the header for
  a test to manipulate directly.
- Minor doc-comment wording differences from the phase file's own literal
  text (e.g. `BuildOutcome`'s `finalSummaryLine` comment references "this
  phase" instead of naming a specific future PHASE4 caller, since PHASE4
  hasn't run yet as of this phase) — meaning-preserving only.

## New gaps found

None. No new architecture/layering/deadlock hazard was found during this
phase's own implementation beyond what PHASE0/PHASE0_DOUBLECHECK_REPORT.md
had already identified and resolved in the strategy documents themselves.
This phase touches only `ProjectAssemblyBuildRunner.h/.cpp` (as its own
"Depends on" line states) and does not interact with
`HotReloadEngineStateMutex`/`ProjectAssemblyHost` at all.

## Definition of Done — verified

- [x] `BuildOutcome` exists in the header; `RunProjectAssemblyBuildAndWait()`
      and `TryRunProjectAssemblyBuildSynchronously()` exist, are declared in
      the header, defined in the `.cpp`.
- [x] `RunOneBuildTarget()`'s read loop is poll-based
      (`PeekNamedPipe()`-driven); `RunBuildThreadBody()` behaves IDENTICALLY
      to before (same final log line text, same exit-code semantics) —
      confirmed via the live `compile_only`/`get_logs` smoke check above.
- [x] The new concurrency test (3.5) passes.
- [x] Incremental build (`gte_core` + `GreatTamanaEngineTests` targets only)
      succeeds, zero new warnings.
