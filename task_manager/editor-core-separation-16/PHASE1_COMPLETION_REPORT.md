# PHASE1 — Live-Safe Reconfigure Helper & Source-Root Resolver — COMPLETION REPORT

Status: **DONE**. All Definition-of-Done items satisfied.

## What was actually built

### 1. `ResolveProjectAssemblySourceRootDirectory()`
Added to `src/Core/Plugins/ProjectAssemblyBuildRunner.h` (declaration, right
after `ResolveCMakeBuildDirectory()`) and `.cpp` (implementation, right after
`ResolveCMakeBuildDirectory()`'s own definition), exactly per the phase file's
own spec, byte-for-byte matching the code sample given. Reads
`CMAKE_HOME_DIRECTORY:INTERNAL=` out of `<buildDirectory>/CMakeCache.txt` and
appends `"Projects"`. Returns an empty path (after one `GTE_LOG_ERROR`) if the
cache file is missing/unreadable, or the line itself is missing.

### 2. `RunPlainCMakeReconfigureAndWait()` + shared `RunChildProcessAndWait()`
Per LDD-CP2, this required refactoring the existing `RunOneBuildTarget()`
(the pre-existing `CreateProcessW()`/`PeekNamedPipe()`-poll/pipe-drain
machinery) to extract a generic, reusable helper:

- New anonymous-namespace function `RunChildProcessAndWait(commandLine,
  workingDirectory, logCategory, onIdleTick, onLine = {})` — takes an
  already-built wide command line and working directory, spawns the child via
  `CreateProcessW()`, drains its combined stdout/stderr via the exact same
  `PeekNamedPipe()`-driven poll loop `RunOneBuildTarget()` used to have
  inline, classifies each completed line as ERROR/WARNING/INFO by a simple
  keyword substring check, and additionally invokes an optional `onLine`
  callback per line (never replacing the internal logging, only supplementing
  it). Returns the child's real exit code, or `-1` if the process could not
  even be created/piped.
- `RunOneBuildTarget()` itself was rewritten to build its
  `cmake --build <dir> --target <name>` command line and then call
  `RunChildProcessAndWait()`, with its own `onLine` lambda checking the 3
  target-missing substrings (`"unknown target"`, `"no rule to make target"`,
  `"targets not built"`) and setting `targetMissingHeuristicHit` — it no
  longer re-logs those lines itself (the generic helper's own
  error/warning/info classification already logged every line exactly once).
- `RunPlainCMakeReconfigureAndWait(sourceDirectory, buildDirectory)` builds
  `cmake -S <sourceDirectory> -B <buildDirectory>` (both paths quoted via the
  pre-existing `QuoteWindowsArgument()`, reused unchanged) and calls the same
  shared helper with `onIdleTick`/`onLine` both left empty. Returns `true`
  only on exit code `0`.

### 3. Tests
New file `tests/Core/Plugins/ProjectAssemblyBuildRunnerSourceRootTests.cpp`
(per PHASE0's own Correction 1 — no existing sibling file to extend), 4 new
tests, all passing:
- `ResolvesProjectsFolderFromARealCMakeHomeDirectoryLine`
- `ReturnsEmptyPathWhenCMakeCacheIsMissing`
- `ReturnsEmptyPathWhenCMakeHomeDirectoryLineIsMissing`
- `PlainReconfigureFailsCleanlyAgainstANonExistentSourceDirectory`

Registered in `tests/CMakeLists.txt` immediately after the existing
`Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` line, mirroring
its comment style.

## Verification performed

- `cmake --build build --target GreatTamanaEngineTests` — clean incremental
  build, zero errors, zero new compiler warnings (re-confirmed via a forced
  `-v` rebuild of the `gte_core` target after touching the `.cpp`'s
  timestamp — single clean compile line, no warning output).
- `tests\GreatTamanaEngineTests.exe --gtest_filter=ProjectAssemblyBuildRunnerSourceRootTest.*:ProjectAssemblyBuildRunnerBackupRestoreTest.*`
  — **11/11 tests passed** (4 new + all 7 pre-existing
  `ProjectAssemblyBuildRunnerBackupRestoreTest.*`, including the
  concurrency-guard test `ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive`,
  which exercises a real child-process spawn through the now-shared
  `RunChildProcessAndWait()` path — proving the refactor did not regress the
  existing async/sync build machinery).
- No full `ctest`/full regression run performed, per this campaign's own
  Note 4/5 (only PHASE5 runs that).

## Deviations from the phase file (and why)

One small, deliberate, behavior-neutral naming choice not spelled out
verbatim in the phase file: the shared helper's signature in the actual code
uses a trailing default argument (`onLine = {}`) directly on the function
definition inside the anonymous namespace (there is no separate forward
declaration elsewhere in the file, so this is legal and is the natural,
minimal place to put the default). The phase file's own pseudocode showed the
same signature; no functional deviation exists.

No other deviations. Every phase-file code sample (the two new declarations,
the resolver implementation, the reconfigure implementation, the 4 tests) was
implemented as specified.

## New gaps found (honest, none load-bearing for this phase)

- None beyond what PHASE0 already documented. The refactor confirmed, by
  direct inspection, that `RunOneBuildTarget()`'s only consumer of
  `targetMissingHeuristicHit` (`RunProjectAssemblyBuildAndWait()`) is
  unaffected by moving the keyword-based logging into the generic helper —
  the flag's own semantics are set identically to before.
- One micro-observation for whoever writes PHASE3: `RunChildProcessAndWait()`'s
  own internal classification no longer gives the 3 "target missing" keyword
  matches a forced `WARNING` log level (previously true only for
  `RunOneBuildTarget()`'s call site) — those lines are now classified purely
  by whether they also happen to contain the substring `"error"` or
  `"warning"` (e.g. a real Ninja "unknown target" line typically also contains
  the word `"error"`, so it now logs as `GTE_LOG_ERROR` instead of the old
  `GTE_LOG_WARNING`). This is a deliberate, phase-file-mandated consequence
  of "the generic helper's own internal classification already logged it
  once" (STEP 2, item 2) — flagged here for transparency since it is a real,
  observable log-level change for that one line class, even though it does
  not change `targetMissingHeuristicHit`'s own correctness (still detected
  via the dedicated `onLine` substring check) or the pre-existing test's
  own success/failure assertions (which never inspected log level).

## Definition of Done — checked

- [x] `ResolveProjectAssemblySourceRootDirectory()` exists, compiles, and its
      3 new tests pass.
- [x] `RunPlainCMakeReconfigureAndWait()` exists, compiles, and its 1 new
      test passes.
- [x] `RunOneBuildTarget()`'s own child-process-spawn code has been
      refactored to share `RunChildProcessAndWait()` with the new function —
      confirmed by re-reading the diff.
- [x] Every EXISTING test in `ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`
      still passes, unchanged pass/fail outcome, after the refactor.
- [x] The new test file is registered in `tests/CMakeLists.txt`.

Ready for PHASE2.
