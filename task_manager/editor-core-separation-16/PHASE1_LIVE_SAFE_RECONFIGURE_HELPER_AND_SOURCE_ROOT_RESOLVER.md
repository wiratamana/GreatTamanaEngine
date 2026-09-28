# PHASE1 — Live-Safe Reconfigure Helper & Source-Root Resolver

Parent: `PHASE0_MASTER_STRATEGY.md` (read first — LDD-CP2, Section 2.1/2.2
of that file are load-bearing context for this phase).

Depends on: nothing (first implementation phase).
Blocks: PHASE3 (needs both new functions this phase adds to exist and
compile cleanly before `CreateNewProjectAssembly()` can call them).

End state of this phase: `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`
gains TWO new, independent, Tier-1-tested pieces:
1. `ResolveProjectAssemblySourceRootDirectory()` — finds the real
   `Projects/` folder at runtime.
2. `RunPlainCMakeReconfigureAndWait()` — the small, shared, synchronous
   "run a bare `cmake -S <src> -B <build>` and wait" helper LDD-CP2's
   "always reconfigure" design needs, factored out of this same file's own
   existing `CreateProcessW()`/pipe-reading machinery (never a second,
   independently-written child-process mechanism).

---

## STEP 1 — `ResolveProjectAssemblySourceRootDirectory()`

Add this declaration to `src/Core/Plugins/ProjectAssemblyBuildRunner.h`,
placed immediately after `ResolveCMakeBuildDirectory()`'s own declaration
(confirmed current line 117 — they are always used together: the caller
resolves the build dir first, then hands it here):

```cpp
// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE1. Reads CMAKE_HOME_DIRECTORY straight out of
// `buildDirectory`'s own CMakeCache.txt (the SAME file
// ResolveCMakeBuildDirectory() just proved exists at this exact path) and
// appends "Projects" onto it - the one, authoritative, always-correct way
// to find the real Project Assembly SOURCE tree at runtime, without ever
// hardcoding a path or inventing a new build-time #define. Returns an
// empty path (after one GTE_LOG_ERROR) if CMakeCache.txt is missing/
// unreadable, or the CMAKE_HOME_DIRECTORY line itself is missing/
// malformed - never throws.
std::filesystem::path ResolveProjectAssemblySourceRootDirectory(const std::filesystem::path& buildDirectory);
```

Implementation, `ProjectAssemblyBuildRunner.cpp` (placed right after
`ResolveCMakeBuildDirectory()`'s own definition):

```cpp
std::filesystem::path ResolveProjectAssemblySourceRootDirectory(const std::filesystem::path& buildDirectory)
{
    const std::filesystem::path cachePath = buildDirectory / "CMakeCache.txt";
    std::ifstream file(cachePath);
    if (!file.is_open()) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "ResolveProjectAssemblySourceRootDirectory: could not open " + cachePath.string());
        return {};
    }
    constexpr const char* kPrefix = "CMAKE_HOME_DIRECTORY:INTERNAL=";
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind(kPrefix, 0) == 0) {
            std::filesystem::path sourceRoot(line.substr(std::string(kPrefix).length()));
            return sourceRoot / "Projects";
        }
    }
    GTE_LOG_ERROR("ProjectAssemblyBuild",
        "ResolveProjectAssemblySourceRootDirectory: no CMAKE_HOME_DIRECTORY line found in " + cachePath.string());
    return {};
}
```

Confirm `<fstream>` is already `#include`d in `ProjectAssemblyBuildRunner.cpp`
(it must be, for `RunOneBuildTarget()`'s own pipe-reading code) — add it if
somehow missing, do not assume.

## STEP 2 — `RunPlainCMakeReconfigureAndWait()`

Declaration, `ProjectAssemblyBuildRunner.h`, placed right after
`RunProjectAssemblyBuildAndWait()`'s own declaration (they share the same
"blocks the calling thread, streams output into GTE_LOG_*" shape):

```cpp
// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE1 (LDD-CP2, PHASE0_MASTER_STRATEGY.md). Runs a bare,
// no-target `cmake -S <sourceDirectory> -B <buildDirectory>` as a real
// child process and BLOCKS THE CALLING THREAD until it exits - reuses the
// EXACT SAME child-process-spawn/pipe-drain primitive
// RunOneBuildTarget() already uses internally (factored out below as
// RunChildProcessAndWait(), never a second, independently-written
// mechanism). Streams stdout/stderr into GTE_LOG_INFO/GTE_LOG_ERROR
// ("ProjectAssemblyBuild" category), exactly like every other child
// process this file spawns. Returns true (exit code 0) or false (any
// non-zero exit code, or a failure to even launch the process at all -
// both logged via GTE_LOG_ERROR before returning). Safe to call from ANY
// thread - this campaign's own real call site (PHASE3's
// CreateNewProjectAssembly()) calls it synchronously, inline, since
// "Create" itself is already a plain, synchronous filesystem operation
// with no cross-thread bridge of its own (see IProjectLifecycleCapability's
// own class comment, PHASE3, for why this is safe).
bool RunPlainCMakeReconfigureAndWait(const std::filesystem::path& sourceDirectory, const std::filesystem::path& buildDirectory);
```

Implementation approach, `ProjectAssemblyBuildRunner.cpp`:

1. Read the existing `RunOneBuildTarget()` (or whatever the real, current,
   internal function name is — re-verify by reading the file fresh, the
   header comments above only describe its documented behavior, not its
   exact internal name/signature) end to end first. Identify the smallest
   reusable unit: the part that calls `CreateProcessW()`, wires up
   anonymous pipes for stdout/stderr, and drains them into
   `GTE_LOG_INFO`/`GTE_LOG_ERROR` line-by-line until the child exits and
   `GetExitCodeProcess()` is read.
2. Extract that unit into a new, private (anonymous-namespace or static),
   generic helper, e.g.:
   ```cpp
   namespace {
   // Generic "spawn this exact command line, stream its combined
   // stdout/stderr into GTE_LOG_INFO/GTE_LOG_ERROR (logCategory), block
   // until it exits" - the ONE real child-process mechanism this whole
   // file uses, now shared by RunOneBuildTarget() (existing) and
   // RunPlainCMakeReconfigureAndWait() (new, PHASE1).
   bool RunChildProcessAndWait(const std::wstring& commandLine, const std::filesystem::path& workingDirectory,
       const char* logCategory, const std::function<void()>& onIdleTick);
   }
   ```
   `RunOneBuildTarget()` itself is updated to call this shared helper
   instead of duplicating the `CreateProcessW()`/pipe code inline — this is
   a pure, behavior-preserving refactor of EXISTING code; the existing
   `ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`
   `ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive` test
   (and any other existing test exercising a real child-process spawn)
   MUST still pass unchanged after this extraction — this is this phase's
   own concrete proof the refactor did not change observable behavior.
3. `RunPlainCMakeReconfigureAndWait()`'s own body builds the command line
   `cmake -S "<sourceDirectory>" -B "<buildDirectory>"` (both paths
   quoted, exactly like `RunOneBuildTarget()`'s own existing
   `--target` command line already quotes its own paths — re-verify the
   exact quoting convention used there and match it byte-for-byte, do not
   invent a new one) and calls the shared helper with `onIdleTick` empty
   (this is only ever called from a context with no message pump to
   service, per PHASE3's own design).

## STEP 3 — Tier-1 tests (new file)

Per PHASE0's own Correction 1: no existing sibling test file for
`ResolveCMakeBuildDirectory()`/`ResolveProjectAssemblyOutputDirectory()`
exists to "extend" — create a genuinely new file,
`tests/Core/Plugins/ProjectAssemblyBuildRunnerSourceRootTests.cpp`, mirroring
`ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`'s own exact style
(`TempOutputDirectory` fixture pattern, `WriteFile()`/`ReadFile()` helpers —
copy that file's own small helpers rather than re-inventing them, or share
them via a small common test-only header if you prefer, either is
acceptable):

```cpp
TEST(ProjectAssemblyBuildRunnerSourceRootTest, ResolvesProjectsFolderFromARealCMakeHomeDirectoryLine)
{
    TempOutputDirectory buildDirectory("SourceRoot_Basic");
    WriteFile(buildDirectory.Path() / "CMakeCache.txt",
        "// some comment\nCMAKE_HOME_DIRECTORY:INTERNAL=C:/fake/repo/root\nOTHER:VALUE=1\n");

    const std::filesystem::path result = ResolveProjectAssemblySourceRootDirectory(buildDirectory.Path());
    EXPECT_EQ(result, std::filesystem::path("C:/fake/repo/root") / "Projects");
}

TEST(ProjectAssemblyBuildRunnerSourceRootTest, ReturnsEmptyPathWhenCMakeCacheIsMissing)
{
    TempOutputDirectory buildDirectory("SourceRoot_NoCache");
    // Deliberately writes no CMakeCache.txt at all.
    EXPECT_TRUE(ResolveProjectAssemblySourceRootDirectory(buildDirectory.Path()).empty());
}

TEST(ProjectAssemblyBuildRunnerSourceRootTest, ReturnsEmptyPathWhenCMakeHomeDirectoryLineIsMissing)
{
    TempOutputDirectory buildDirectory("SourceRoot_NoLine");
    WriteFile(buildDirectory.Path() / "CMakeCache.txt", "// nothing useful here\nSOME_OTHER_VAR:STRING=x\n");
    EXPECT_TRUE(ResolveProjectAssemblySourceRootDirectory(buildDirectory.Path()).empty());
}

TEST(ProjectAssemblyBuildRunnerSourceRootTest, PlainReconfigureFailsCleanlyAgainstANonExistentSourceDirectory)
{
    // A real cmake.exe genuinely runs here (this test needs cmake on PATH,
    // exactly like ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive
    // already requires it), pointed at a source directory that does not
    // exist - proves the false-return path is reachable and clean (no
    // crash, no thrown exception) without needing a real, multi-second
    // successful reconfigure to also be tested here (that is proven live,
    // for real, in PHASE5's own end-to-end verification instead).
    TempOutputDirectory buildDirectory("Reconfigure_BadSource");
    const std::filesystem::path bogusSource = buildDirectory.Path() / "does_not_exist_at_all";
    EXPECT_FALSE(RunPlainCMakeReconfigureAndWait(bogusSource, buildDirectory.Path()));
}
```

Add the new file to `tests/CMakeLists.txt`'s hand-maintained list,
immediately after the existing
`Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` line (line
2166 today), with a short comment mirroring every neighboring entry's own
style.

## STEP 4 — Verification (incremental build + targeted test run only —
no full `ctest`, per this whole campaign's own Note 4/5)

1. `cmake --build build --target GreatTamanaTests` (or whatever this
   repo's real, current test-target name is — confirm by reading
   `tests/CMakeLists.txt`'s own `add_executable(...)` line if unsure).
2. Run ONLY the new test binary/filter for
   `ProjectAssemblyBuildRunnerSourceRootTest.*` and re-run
   `ProjectAssemblyBuildRunnerBackupRestoreTest.*` in full (proves STEP 2's
   refactor of `RunOneBuildTarget()` did not regress anything it already
   covered) — both via `ctest -R` with a name filter, or the test binary's
   own `--gtest_filter`, whichever this repo's existing convention already
   uses.
3. Confirm zero new compiler warnings from the two edited/new files.

## Definition of Done — this phase only

- [ ] `ResolveProjectAssemblySourceRootDirectory()` exists, compiles, and
      its 3 new tests pass.
- [ ] `RunPlainCMakeReconfigureAndWait()` exists, compiles, and its 1 new
      test passes.
- [ ] `RunOneBuildTarget()`'s own child-process-spawn code has been
      refactored to share `RunChildProcessAndWait()` with the new
      function — confirmed by re-reading the diff, not merely assumed.
- [ ] Every EXISTING test in
      `ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` still passes,
      unchanged pass/fail outcome, after the refactor.
- [ ] The new test file is registered in `tests/CMakeLists.txt`.

## What this phase does NOT do

- Does NOT call either new function from anywhere in production code yet —
  that is PHASE3's job (`CreateNewProjectAssembly()`).
- Does NOT touch the name-validation problem at all (PHASE2's job).
- Does NOT run a real, successful reconfigure against the REAL repo build
  tree as part of this phase's own automated tests (that would be slow and
  mutate real build state on every test run) — the real, live, successful
  case is proven exactly once, for real, in PHASE5's end-to-end
  verification, immediately after a real "Create" call.
