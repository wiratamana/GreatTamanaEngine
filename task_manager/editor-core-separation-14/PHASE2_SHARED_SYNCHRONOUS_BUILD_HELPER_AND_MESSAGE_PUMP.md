# PHASE2 — Shared Synchronous Build Helper & Poll-Based Message Pump Hook

Parent: `PHASE0_MASTER_STRATEGY.md`. Read PHASE1 first (independent code
area, but same campaign ordering).
Depends on: nothing new from PHASE1 — this phase touches
`ProjectAssemblyBuildRunner.h/.cpp` only.
Blocks: PHASE4 (the orchestrator's synchronous compile step needs
`TryRunProjectAssemblyBuildSynchronously()`/`BuildOutcome` to exist).

---

## STEP 1 — The Goal

One shared function, `RunProjectAssemblyBuildAndWait()`, implements the
real `_Game`-then-`_Editor` build sequencing exactly once; the EXISTING
asynchronous path (`TriggerProjectAssemblyCompile()`, the original
"Compile" button, and `compile_only`'s debug route) keeps calling it from a
background thread with **zero observable behavior change**; a NEW
synchronous entry point, `TryRunProjectAssemblyBuildSynchronously()`, calls
the SAME function directly, blocking the CALLING thread, sharing the
EXACT SAME per-project in-flight guard as the async path. The underlying
pipe-read loop becomes poll-based (`PeekNamedPipe()`-driven) so an optional
idle-tick callback runs on a genuinely fixed cadence — this is what PHASE4
uses to keep Windows' message queue serviced during a long synchronous
compile, without touching game/render state.

## STEP 2 — The Situation

Confirmed, current, `src/Core/Plugins/ProjectAssemblyBuildRunner.cpp`:

- `g_inFlightMutex`/`g_inFlightProjects`/`TryMarkInFlight()`/
  `ClearInFlight()` — anonymous-namespace, file-local (lines 30-47).
- `RunOneBuildTarget(buildDirectory, targetName, bool& targetMissingHeuristicHit)`
  (lines 137-240) — spawns `cmake --build <dir> --target <name>` via
  `CreateProcessW()`, reads its combined stdout/stderr via a **plain,
  blocking** `while (ReadFile(readPipe, ...))` loop (line 196), classifies
  each line (error/warning/target-missing/info) into `GTE_LOG_*`, then
  `WaitForSingleObject()`s the child and returns its exit code.
- `RunBuildThreadBody(projectName, buildDirectory, completionFlag)` (lines
  250-285) — the actual background-thread body: builds `_Game`, then (only
  if that succeeded) `_Editor` (a missing `_Editor` target — detected via
  `targetMissingHeuristicHit` — is coerced to success), logs the final
  `"Build finished with exit code N - relaunch GreatTamanaEditor.exe..."`
  line, calls `ClearInFlight()`, sets `completionFlag`.
- `TriggerProjectAssemblyCompile(projectName, buildDirectory)` (lines
  289-306) — `TryMarkInFlight()` -> spawns `std::thread(&RunBuildThreadBody, ...)`
  -> `JobSystem::Instance().RegisterBackgroundThread(...)`.

**No timeout exists anywhere in `RunOneBuildTarget`'s read loop** — `ReadFile()`
blocks until either data arrives or the pipe closes. A literal "pump
messages once per line" insertion would leave arbitrarily long silent gaps
whenever the child produces no output for a stretch (a real risk during a
slow link step) — PHASE0's own Correction (c) already flags this; this
phase is where it gets fixed for real.

## STEP 3 — The Plan

### 3.1 — `BuildOutcome`, declared in the header

`ProjectAssemblyBuildRunner.h`, new struct + two new declarations (place
right before `TriggerProjectAssemblyCompile()`'s own declaration):

```cpp
#include <functional> // std::function<void()> - new, for onIdleTick below.

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE2. The value-type result of one full build attempt
// (both TARGETS: _Game, then _Editor if present) - shared by BOTH the
// existing async path and the new synchronous path below.
struct BuildOutcome {
    bool success = false;
    int exitCode = 0;
    // Deliberately NOT the existing async path's own "relaunch
    // GreatTamanaEditor.exe..." wording (that advice is wrong for a
    // hot-reload caller) - each CALLER writes its own final summary log
    // line using this struct's exitCode/success; this field is reserved
    // for a future caller that wants a single ready-made human string
    // without composing one itself (unused by PHASE4's own orchestrator,
    // which composes its own line - see PHASE4's own Step 5).
    std::string finalSummaryLine;
};

// Builds <projectName>_Game (then _Editor, if that target exists),
// IDENTICAL sequencing/target-missing heuristic as the existing async
// path (RunBuildThreadBody) - BLOCKS THE CALLING THREAD for the whole
// duration. Does NOT touch the per-project in-flight guard itself - see
// TryRunProjectAssemblyBuildSynchronously() below for the one sanctioned
// way a NEW caller uses this together with that guard; RunBuildThreadBody
// (the EXISTING async path) continues to guard it exactly as it already
// does today, unchanged. `onIdleTick`, if provided, is invoked from
// RunOneBuildTarget()'s own poll loop on a fixed ~50ms cadence whenever no
// build output is currently available - see that function's own updated
// doc comment (PHASE2) for why. Pass an empty std::function (the default)
// from any call site that is NOT running on the main/window-owning thread
// (a background thread has no window to pump messages for - doing so
// would be a pointless no-op, never do it).
BuildOutcome RunProjectAssemblyBuildAndWait(const std::string& projectName, const std::string& buildDirectory,
    const std::function<void()>& onIdleTick = {});

// The synchronous counterpart of TriggerProjectAssemblyCompile() - shares
// the EXACT SAME per-project in-flight guard (g_inFlightProjects) as that
// existing async path: a hot-reload request for a project whose own async
// "Compile" build (the button OR /compile_only) is ALREADY running is
// rejected here (returns false, outOutcome left untouched), exactly
// mirroring TriggerProjectAssemblyCompile()'s own existing rejection of a
// second overlapping async request - and vice versa (a fresh async
// request is rejected while THIS function's own synchronous build is
// still running), since both now go through the SAME g_inFlightProjects
// set. Returns true once the build genuinely ran to completion (regardless
// of whether it succeeded - check outOutcome.success for that).
bool TryRunProjectAssemblyBuildSynchronously(const std::string& projectName, const std::string& buildDirectory,
    BuildOutcome& outOutcome, const std::function<void()>& onIdleTick = {});
```

### 3.2 — Poll-based read loop in `RunOneBuildTarget()`

Add a `const std::function<void()>& onIdleTick` parameter. Replace the
existing blocking loop (lines ~193-225) with:

```cpp
std::string lineBuffer;
char readBuffer[4096];
for (;;) {
    DWORD bytesAvailable = 0;
    if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
        break; // Pipe closed (child exited) or a genuine error - both end this loop, matching ReadFile()'s own former "false -> stop" contract.
    }
    if (bytesAvailable == 0) {
        if (onIdleTick) {
            onIdleTick();
        }
        Sleep(50); // Fixed poll cadence - see this phase's own PHASE0 Correction (c).
        continue;
    }
    DWORD bytesRead = 0;
    if (!ReadFile(readPipe, readBuffer, sizeof(readBuffer), &bytesRead, nullptr) || bytesRead == 0) {
        break;
    }
    lineBuffer.append(readBuffer, bytesRead);
    std::size_t newlinePos = 0;
    while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {
        // ... UNCHANGED per-line classification body (line/lower/error/
        // warning/target-missing/info GTE_LOG_* dispatch) ...
    }
}
if (!lineBuffer.empty()) {
    GTE_LOG_INFO("ProjectAssemblyBuild", lineBuffer);
}
```

Everything after this loop (`CloseHandle(readPipe)`,
`WaitForSingleObject()`, exit-code retrieval) is UNCHANGED.

**Verify concretely during implementation**: `PeekNamedPipe()`'s
`lpTotalBytesAvail` output parameter reports 0 correctly while the child is
simply silent (not yet exited) versus the pipe being genuinely broken —
confirm this by testing against a real, slow/no-op build target (e.g. a
project with no source changes, "ninja: no work to do." — a fast case) AND
a deliberately slowed one (see PHASE4's own Definition of Done, which needs
this exact scenario anyway) before considering this loop correct.

### 3.3 — Extract `RunProjectAssemblyBuildAndWait()`

Move `RunBuildThreadBody`'s own Game-then-Editor sequencing body (current
lines 255-272, everything between the "Starting build..." log line and the
"Build finished..." log line) into the new, non-static (declared in the
header) function:

```cpp
BuildOutcome RunProjectAssemblyBuildAndWait(const std::string& projectName, const std::string& buildDirectory,
    const std::function<void()>& onIdleTick)
{
    GTE_LOG_INFO("ProjectAssemblyBuild",
        "Starting build for Project Assembly '" + projectName + "' (build directory: " + buildDirectory + ")...");

    bool gameTargetMissing = false;
    const int gameExitCode = RunOneBuildTarget(buildDirectory, projectName + "_Game", gameTargetMissing, onIdleTick);

    int editorExitCode = 0;
    if (gameExitCode == 0) {
        bool editorTargetMissing = false;
        editorExitCode = RunOneBuildTarget(buildDirectory, projectName + "_Editor", editorTargetMissing, onIdleTick);
        if (editorExitCode != 0 && editorTargetMissing) {
            GTE_LOG_INFO("ProjectAssemblyBuild",
                "Project '" + projectName + "' has no '_Editor' target (no Editor/ sources) - this is normal, not a build failure.");
            editorExitCode = 0;
        }
    } else {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "'" + projectName + "_Game' target build failed (exit code " + std::to_string(gameExitCode) + ") - skipping the '_Editor' target attempt.");
    }

    BuildOutcome outcome;
    outcome.exitCode = (gameExitCode != 0) ? gameExitCode : editorExitCode;
    outcome.success = (outcome.exitCode == 0);
    return outcome;
}
```

`RunBuildThreadBody` becomes a thin wrapper, preserving its EXACT existing
log line (do not change this user-visible text — it is still correct for
the async "Compile" button/`compile_only` route, which never reloads
anything):

```cpp
void RunBuildThreadBody(std::string projectName, std::string buildDirectory, std::shared_ptr<std::atomic<bool>> completionFlag)
{
    const BuildOutcome outcome = RunProjectAssemblyBuildAndWait(projectName, buildDirectory); // no onIdleTick - background thread owns no window.
    GTE_LOG_INFO("ProjectAssemblyBuild",
        "Build finished with exit code " + std::to_string(outcome.exitCode) +
        " - relaunch GreatTamanaEditor.exe to use the result (Project Assemblies are not hot-reloaded, see PHASE0_MASTER_STRATEGY.md, LDD4).");
    ClearInFlight(projectName);
    completionFlag->store(true, std::memory_order_release);
}
```

(Note: this log line's own "not hot-reloaded" claim is now only accurate
for the async "Compile" button/`compile_only` path, never for a real
`/project_assembly/hot_reload` cycle — this is CORRECT and intentional:
this exact log line is never reached by the new synchronous path at all,
which composes its own, different final line in PHASE4.)

### 3.4 — `TryRunProjectAssemblyBuildSynchronously()`

```cpp
bool TryRunProjectAssemblyBuildSynchronously(const std::string& projectName, const std::string& buildDirectory,
    BuildOutcome& outOutcome, const std::function<void()>& onIdleTick)
{
    if (!TryMarkInFlight(projectName)) {
        GTE_LOG_WARNING("ProjectAssemblyBuild",
            "Synchronous build for Project Assembly '" + projectName + "' rejected - a build for this project is already in progress.");
        return false;
    }
    outOutcome = RunProjectAssemblyBuildAndWait(projectName, buildDirectory, onIdleTick);
    ClearInFlight(projectName);
    return true;
}
```

### 3.5 — Tier-1 / concurrency test

Add a test proving the shared in-flight guard now also rejects a
synchronous call while ANOTHER synchronous (or async) call for the SAME
project name is in flight. A practical way to do this without a real
multi-minute build: point both calls at a project name/build directory
combination that legitimately fails fast (e.g. a non-existent build
directory — `RunOneBuildTarget`'s `CreateProcessW()` will fail immediately,
`RunProjectAssemblyBuildAndWait()` still returns a well-formed
`BuildOutcome{success=false}` without hanging), and dispatch two calls
concurrently from two `std::thread`s for the exact same fabricated project
name, asserting that AT LEAST one of the two calls to
`TryRunProjectAssemblyBuildSynchronously`/`TryMarkInFlight` (indirectly)
observes `false` OR that the two builds never overlap (exact assertion
shape left to the implementer, since this is inherently a timing-sensitive
test — prefer a deterministic version if one is achievable, e.g. by having
the first thread hold a manually-inserted small `Sleep()` proxy instead of
a real build call, if the existing test file's own conventions make that
  easier). Add this test to the EXISTING, already-registered
  `tests/Core/Plugins/ProjectAssemblyBuildRunnerBackupRestoreTests.cpp` file
  (it already includes `Core/Plugins/ProjectAssemblyBuildRunner.h` and is
  already in `tests/CMakeLists.txt`) rather than a new file, so this phase
  needs zero `CMakeLists.txt` change.

## Definition of Done — this phase only

- [ ] `BuildOutcome` exists in the header; `RunProjectAssemblyBuildAndWait()`
      and `TryRunProjectAssemblyBuildSynchronously()` exist, are declared in
      the header, defined in the `.cpp`.
- [ ] `RunOneBuildTarget()`'s read loop is poll-based
      (`PeekNamedPipe()`-driven); `RunBuildThreadBody()` behaves
      IDENTICALLY to before (same final log line text, same exit-code
      semantics) — confirmed via a quick live `POST
      /project_assembly/debug/compile_only?name=ProjectAssemblyProbe` smoke
      check against `GET /get_logs?category=ProjectAssemblyBuild` showing
      the SAME output shape as `editor-core-separation-12`'s own
      documented check #7.
- [ ] The new concurrency test (3.5) passes.
- [ ] Incremental build (`gte_core` + `GreatTamanaEngineTests` targets only)
      succeeds, zero new warnings.
