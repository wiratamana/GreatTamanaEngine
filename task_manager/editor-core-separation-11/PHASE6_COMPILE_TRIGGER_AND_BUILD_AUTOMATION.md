# PHASE6 of 8 — THE "COMPILE" TRIGGER (BUILD AUTOMATION)

Read `PHASE0_MASTER_STRATEGY.md` first.
**Depends on:** PHASE3 (needs a real project to compile), PHASE5 (the
result of a fresh compile is only ever picked up on the NEXT launch — this
phase's own user-facing messaging must say so, LDD4).
**Blocks:** nothing downstream.

End state: a callable C++ function,
`TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory)`,
that runs `cmake --build <build-dir> --target <Name>_Game --target
<Name>_Editor` as a genuine background child process, streams its
stdout/stderr into the engine's real logging system as it runs (not
buffered until the end), and never blocks the main render/UI thread. WHERE
this gets triggered from in the Editor UI is explicitly left to whoever
wires it in later — this phase only builds the mechanism.

## `JobSystem::Schedule()` is the WRONG mechanism here — use
`RegisterBackgroundThread()` instead

`src/Jobs/JobSystem.h`'s `Schedule()` runs a job on one of a FIXED-SIZE pool
of worker threads, shared by every other engine subsystem (GPU-driven
culling, animation jobs, ...). A `cmake --build` invocation can legitimately
take anywhere from seconds to several minutes. Scheduling it via
`Schedule()` would tie up one entire worker thread for that whole duration —
a real, meaningful engine-wide performance regression while any build is in
progress, not a cosmetic concern.

`JobSystem` already has the CORRECT, existing mechanism for exactly this
shape of work: `RegisterBackgroundThread(std::thread thread,
std::shared_ptr<std::atomic<bool>> completionFlag)` (confirmed, current
`JobSystem.h` line 164, `.cpp` line 56) — a genuinely separate `std::thread`,
not part of the fixed worker pool, with its own lifecycle tied into
`JobSystem`'s shutdown sequence. A real, working precedent for this exact
pattern already exists: `JobContinuation.cpp` line 168. Mirror that
precedent, do not invent a third pattern.

## STEP 1 — new file: `src/Core/Plugins/ProjectAssemblyBuildRunner.h`/`.cpp`

```cpp
// src/Core/Plugins/ProjectAssemblyBuildRunner.h
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE6.
// Runs `cmake --build` as a real child process on a dedicated background
// thread (JobSystem::RegisterBackgroundThread() - NEVER JobSystem::
// Schedule(), which would tie up a fixed worker-pool thread for the
// build's entire, potentially multi-minute duration - see this phase's own
// PHASE6_COMPILE_TRIGGER_AND_BUILD_AUTOMATION.md for the full reasoning),
// streaming its stdout/stderr into GTE_LOG_INFO/GTE_LOG_WARNING as it runs,
// never a raw printf/console window.
#pragma once

#include <string>

namespace gte {

// Kicks off an ASYNCHRONOUS build of `<projectName>_Game` and (if it
// exists) `<projectName>_Editor` against `buildDirectory`. Returns
// immediately - the actual build happens on a background thread. Safe to
// call again while a previous build for a DIFFERENT project is still
// running; calling it again for the SAME project while its own previous
// build is still in flight is caller error (see this file's own .cpp for
// the simple, single-flag-per-project guard that turns this into a
// harmless, logged no-op rather than two overlapping child processes
// racing each other's output).
void TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory);

} // namespace gte
```

**Concrete child-process/streaming mechanics (Windows-specific — this repo
is Windows-only, confirmed by root `CMakeLists.txt`'s own `if(NOT WIN32)
message(FATAL_ERROR ...)` gate):** use `CreateProcessW()` with
`STARTUPINFOW::hStdOutput`/`hStdError` redirected to an anonymous pipe
(`CreatePipe()`), read that pipe on the SAME background thread in a loop
(`ReadFile()` blocking is fine here — this thread's whole job is exactly
this blocking loop). Forward each completed line to `GTE_LOG_INFO("ProjectAssemblyBuild",
line)` (or `GTE_LOG_WARNING`/`GTE_LOG_ERROR` if the line matches a simple
`ninja`/`cmake`/`glslc`/`g++` error-marker heuristic — keep this heuristic
simple and clearly documented as best-effort, not guaranteed-correct). On
the child process exiting, log one final, clearly-marked summary line:

```
"Build finished with exit code N — relaunch GreatTamanaEditor.exe to use the
result (Project Assemblies are not hot-reloaded, see PHASE0_MASTER_STRATEGY.md,
LDD4)."
```

This exact reminder MUST be in the final log line, every time, so a user is
never confused about why nothing changed on screen after a successful
build.

## STEP 2 — the exact command line

```
cmake --build <buildDirectory> --target <projectName>_Game --target <projectName>_Editor
```

**What is `<buildDirectory>` at runtime?** It cannot be `CMAKE_BINARY_DIR` (a
configure-time-only CMake variable, not available to running C++ code) — it
must be derived at RUNTIME from `gte::ExecutableDirectory()`. Confirmed,
current repository fact: `GreatTamanaEditor.exe` has no
`RUNTIME_OUTPUT_DIRECTORY` override (a plain `add_executable()` call), so the
CMake BUILD directory itself is `gte::ExecutableDirectory()`'s own parent
chain up to wherever `CMakeCache.txt` lives (confirmed today:
`build/GreatTamanaEditor.exe` sits directly inside `build/`, which directly
contains `CMakeCache.txt` — i.e. one level up in the common case, but do NOT
hardcode a single `".."` — walk upward looking for a real `CMakeCache.txt`
file, and fail loudly (a clear `GTE_LOG_ERROR`, no crash) if none is found
within a small, bounded number of parent levels (e.g. 5). This correctly
handles both a Ninja single-config tree (confirmed active today,
`CMAKE_GENERATOR:INTERNAL=Ninja`) and a Visual-Studio-style multi-config
tree's differing folder depths without hardcoding either shape.

`--target <Name>_Editor` will FAIL (target does not exist) if a given
project has no `Editor/` sub-folder sources at all (PHASE3's
`gte_add_project()` only creates `${NAME}_Editor` conditionally). Handle
this: attempt `cmake --build <dir> --target <Name>_Game`, THEN, only if that
succeeds, separately attempt `--target <Name>_Editor`, logging (not failing
the whole operation) if the second target genuinely does not exist — do not
combine them into one command line that fails outright the moment a project
has no Editor code at all, a completely normal, valid case.

## STEP 3 — clean shutdown while a build is in flight

If the user closes `GreatTamanaEditor.exe` while a background build is still
running, `JobSystem`'s own destructor calls `JoinAllBackgroundThreads()`
(confirmed, current `JobSystem.h`'s own doc comment on `IsShuttingDown()`) —
this will BLOCK process exit until the child `cmake --build` process's own
pipe-reading loop actually returns. Two acceptable choices, pick ONE
explicitly and document which:

  (a) Let it block — the user simply waits for the in-flight build to
      finish before the process actually exits (simplest, safest, matches
      this whole system's own explicit "manual compile, no hot reload,
      single-developer" framing — no silent data loss, no half-finished
      `.dll`). **Recommended default.**
  (b) On `IsShuttingDown()` becoming true (poll it, mirroring
      `JobContinuation.cpp`'s own established polling pattern exactly),
      forcibly terminate the child process (`TerminateProcess()`) so the
      background thread can return immediately.

State explicitly, in your own completion notes, which one was actually
implemented — this is a real, user-visible behavior choice, not a cosmetic
detail.

## STEP 4 — where this gets called from (out of scope for THIS phase's own
Definition of Done)

`TriggerProjectAssemblyCompile()`'s signature takes a plain
`projectName`/`buildDirectory` pair specifically so it can be called from
ANY future UI surface with zero coupling to where that UI eventually lives
— do not couple this function to any specific ImGui panel class. To prove
it end-to-end for this phase's own testing, a temporary, throwaway call site
is acceptable (e.g. a debug keybinding, removed before considering this
phase done) — the permanent UI placement decision itself is explicitly
deferred (Non-Goals, PHASE0).

## Definition of Done

- [ ] `ProjectAssemblyBuildRunner.h`/`.cpp` exist, compile, and use
      `JobSystem::RegisterBackgroundThread()` — NOT `Schedule()` — with a
      clear code comment explaining why.
- [ ] Calling `TriggerProjectAssemblyCompile("ProjectAssemblyProbe", <resolved
      build dir>)` while `GreatTamanaEditor.exe` is running (via a temporary
      test call site) produces real, live, line-by-line `cmake --build`
      output visible via repeated `GET /get_logs` polling, WHILE the main
      window stays fully responsive (confirm via `GET /get_swapchain`
      returning fresh frames during the build, not a frozen one).
- [ ] The final log line, on both success and failure, explicitly reminds
      the user that a relaunch is required to see any effect.
- [ ] A project with no `Editor/` sources builds its `_Game` target
      successfully without the whole operation being reported as failed
      just because `_Editor` does not exist.
- [ ] Completion notes state explicitly which shutdown-behavior choice
      (Step 3, (a) or (b)) was implemented.

## What this phase does NOT do

- Does NOT add a file-watcher, auto-rebuild-on-save, or any hot-reload
  behavior (LDD4).
- Does NOT decide the permanent UI location of the "Compile" trigger.
- Does NOT retry a failed build automatically, and does NOT attempt to
  parse `cmake --build`'s output for anything beyond the simple
  success/failure exit code and the best-effort log-level heuristic.
