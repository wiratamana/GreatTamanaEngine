# PHASE4 — The Real Orchestrator, Message Pump, Status Wiring, and Route Response — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file implemented:
`PHASE4_ORCHESTRATOR_AND_ROUTE_WIRING.md`.

## What was actually done

Re-read `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE0_DOUBLECHECK_REPORT.md`, and `PHASE1_COMPLETION_REPORT.md`/
`PHASE2_COMPLETION_REPORT.md`/`PHASE3_COMPLETION_REPORT.md` first, per the
task's own instructions — none of the three reported any deviation that
changes an exact function name/signature this phase needed (PHASE1's
`LoadOneProjectAssemblyFromExactPath()`/`...IfExists()`/mutex closure,
PHASE2's `TryRunProjectAssemblyBuildSynchronously()`/`BuildOutcome`, and
PHASE3's `ProjectAssemblyHotReload.h`'s permanent 6-parameter signature,
`TriggerHotReload()`'s real body, and the drain point in `EditorHost.cpp`
were all confirmed present and byte-for-byte matching what this phase's own
file assumes, by directly reading every one of those files before editing
anything).

Re-verified every file/line/function-name citation the phase file makes
against the real, current source tree before touching anything —
`ProjectAssemblyHotReload.h/.cpp` (PHASE3's temporary body, confirmed exactly
as that phase's completion report describes), `Core.h` line 351
(`GetProjectAssemblyHost()`), `ProjectAssemblyHost.h` (the two PHASE1
public loaders), `ProjectAssemblyBuildRunner.h` (`BuildOutcome`,
`TryRunProjectAssemblyBuildSynchronously`, `BackupProjectAssemblyBinaries`/
`RestoreProjectAssemblyBinariesFromBackup`), `HotReloadEngineStateMutex.h`
line 40 (plain, non-recursive `std::mutex`), `EditorHotReloadDebugCapability.h/.cpp`
(`TriggerHotReload()`'s real PHASE3 body, unchanged this phase),
`NetworkServer.cpp` (the exact `POST /project_assembly/hot_reload` handler,
found at lines 1332-1367, matching the phase file's own 1345-1367 citation
closely enough — small drift from PHASE1-3's own edits, as expected), and
`tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp` (the
obsolete `HotReloadValidNameReturns501NotImplemented` test and the existing
`FakeSceneSnapshotStandIn` precedent it names).

### 1. `src/Core/Plugins/ProjectAssemblyHotReload.h`

- Added `HotReloadStateSnapshot` (empty struct), `CaptureProjectAssemblyHotReloadState(Core&)`
  (HOOK POINT A), and `RestoreProjectAssemblyHotReloadState(Core&, const HotReloadStateSnapshot&)`
  (HOOK POINT B) declarations, exactly per Section 3.1.
- `PerformProjectAssemblyHotReload()`'s own signature is byte-for-byte
  unchanged from PHASE3 (still the permanent 6-parameter shape) — only its
  doc comment was updated to describe the real body instead of PHASE3's
  temporary one, and an explicit reminder was added: "do not
  #include ../../Editor/ProjectRootPath.h here" (per the task's own
  instruction, restated at the exact point it matters).

### 2. `src/Core/Plugins/ProjectAssemblyHotReload.cpp`

- Both HOOK POINT A/B bodies implemented exactly per Section 3.1 — logged
  no-op stubs, `GTE_LOG_INFO`, `"ProjectAssemblyHotReload"` category.
- `PumpWindowsMessagesDuringHotReloadFreeze()` implemented exactly per
  Section 3.2 (`PeekMessage(..., nullptr, 0, 0, PM_REMOVE)` /
  `TranslateMessage` / `DispatchMessage`, `hWnd = nullptr` for multi-viewport
  reasons, every message including `WM_CLOSE` dispatched normally, doc
  comment copied verbatim).
- `PerformProjectAssemblyHotReload()`'s real body implemented exactly per
  Section 3.3's code block: `Set("CapturingState")` → `CaptureProjectAssemblyHotReloadState()`
  → build-directory-empty guard → `Set("BackingUpBinaries")` → `BackupProjectAssemblyBinaries()`
  → `Set("Unloading")` → `core.GetProjectAssemblyHost().UnloadProjectAssembly(projectName, core, renderer)`
  (never wrapped in an outer lock on `GetHotReloadEngineStateMutex()`, per
  PHASE1's own note) → `Set("Compiling")` →
  `TryRunProjectAssemblyBuildSynchronously(..., &PumpWindowsMessagesDuringHotReloadFreeze)`
  → success path (`Set("ReloadingNewCode")` → `LoadOneProjectAssemblyFromExactPath()`
  + `...IfExists()`, both threaded with the REAL `editorHost` pointer, never
  `nullptr`) → failure path (`Set("RollingBack")` →
  `RestoreProjectAssemblyBinariesFromBackup()` → reload-from-backup, same
  two calls, same real `editorHost`) → `Set("RestoringState")` →
  `RestoreProjectAssemblyHotReloadState()` → `Finish("Success"/"RolledBack"/"CriticalFailure", ...)`.
  Every `CriticalFailure` early-return matches the phase file's own exact
  wording (`"could not resolve CMake build directory..."`,
  `"backup failed before any teardown..."`,
  `"restoring the backup binaries failed..."`, `"rollback itself failed..."`).
- Includes exactly as specified — `"../Core.h"`, `"../Logging.h"`,
  `"ProjectAssemblyBuildRunner.h"`, `"ProjectAssemblyHotReloadDebugStatus.h"`,
  `"../../Renderer/Renderer.h"`, `<windows.h>` — **deliberately no
  `"../../Editor/ProjectRootPath.h"` include and no call to
  `gte::ExecutableDirectory()` anywhere in this file**, confirmed by
  `search_in_dir` after writing it.

### 3. `src/Network/NetworkServer.cpp`

Replaced the obsolete `501`/unreachable-`500` comment block and handler body
(lines 1332-1367 as re-confirmed against the live file, not blindly trusted
from the phase file's own 1345-1367 citation) with the real body from
Section 3.5: `TriggerHotReload()` → `false` → `503` (`"hot reload rejected -
a build for this project may already be in progress, or this request timed
out waiting for the main thread"`); `true` → `res.set_content(BuildHotReloadStatusResponseJson(hotReloadDebugCapability->GetHotReloadStatus()), ...)`
— reusing the pre-existing, unchanged `BuildHotReloadStatusResponseJson()`
builder, zero new JSON-shape code, zero `IHotReloadDebugCapability` signature
change (confirmed by reading `NetworkRoutes.h`/`.cpp` first).

### 4. `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`

Implemented Section 3.6 exactly: `HotReloadValidNameReturns501NotImplemented`
replaced by:

- `HotReloadValidNameReturns503WhenBridgeIsNotWired` — reuses the existing
  `ProjectAssemblyHotReloadEndpointsEndToEndTest` fixture (whose `m_capability`
  never calls `SetHotReloadCommandBridge()`), asserting `503` with a
  non-empty `"error"` field.
- `FakeHotReloadStandIn` (new class) + `ProjectAssemblyHotReloadTriggerEndToEndTest`
  (new fixture) + `HotReloadValidNameReturns200WithRealStatusBodyWhenBridgeIsServiced`
  — mirrors `FakeSceneSnapshotStandIn`'s own precedent exactly: a background
  thread looping `TryPeekPendingProjectName()`/`FulfillPending()`, never
  calling the real orchestrator, proving the capability↔bridge↔route wiring
  end-to-end with a real `200` and a well-formed `GetHotReloadStatus()` JSON
  body shape.

Added `#include "Application/ProjectAssemblyHotReloadCommandBridge.h"` to
this file's own include block (the `edit_line` auto-dedup safety net caught
and removed one accidental leftover duplicate `#include` line from my own
first attempt at this edit — verified by a full re-read afterward; not a
tool malfunction, an authoring mistake in my own `contents` payload). No new
`tests/CMakeLists.txt` entry needed (same, already-registered file).

## Build / test verification (incremental only, per this phase's scope)

- `cmake --build build --target gte_core` — succeeded (2 files recompiled:
  `ProjectAssemblyHotReload.cpp`, `NetworkServer.cpp`; relink of
  `libgte_core.a`), zero new warnings.
- `cmake --build build --target gte_editor` — succeeded (no source changes
  in this library this phase; verified it still links).
- `cmake --build build --target GreatTamanaEditor` — succeeded, relinked.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (1 file
  recompiled: `ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`, relinked),
  zero new warnings.
- `ctest -C Debug --output-on-failure -R ProjectAssemblyHotReload` — **21/21
  tests passed** (the 4 existing `ProjectAssemblyHotReloadEndpointsEndToEndTest`
  OBSERVE-route tests, the new `HotReloadValidNameReturns503WhenBridgeIsNotWired`,
  the new `HotReloadValidNameReturns200WithRealStatusBodyWhenBridgeIsServiced`,
  the 2 scene-snapshot tests, the no-capability test, the 5
  `ProjectAssemblyHotReloadCommandBridgeTest`s from PHASE3, and the 3
  `ProjectAssemblyHotReloadDebugStatusTest`s from BIG-STEP 1).

No full build and no full regression `ctest` run were performed, per this
phase's own explicit scope (PHASE5's job).

## Live verification against a real running `GreatTamanaEditor.exe`

Launched `build\GreatTamanaEditor.exe` via `run_app_background`, confirmed
running via `GET /get_swapchain` (Probe Panel already docked/active,
`GET /list_tabs`/`GET /render_graph` confirmed the baseline `"Probe Panel"`
and `"ProjectAssemblyProbe.FillTexture"` pass, `GET /get_texture?texture_name=ProjectAssemblyProbe.Output`
confirmed the baseline solid-orange fill). Debugging used exclusively the
engine's own internal logging via `GET /get_logs` (categories
`ProjectAssemblyHotReload`/`ProjectAssemblyBuild`) — no printf/console
logging anywhere, per the task's own instruction.

1. **SUCCESS path** — changed `Assets/ProbeCompute.comp`'s fill color from
   orange `(1.0, 0.35, 0.05)` to blue `(0.1, 0.4, 1.0)`, then
   `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` → `200`,
   `"last_outcome":"Success"`. Confirmed: `GET /list_tabs` still shows
   `"Probe Panel"`; `GET /get_texture?texture_name=ProjectAssemblyProbe.Output`
   now renders solid blue; `GET /get_logs?category=ProjectAssemblyHotReload`
   shows every phase's log line in order, all under the **same frame number**
   (proving the whole cycle ran inside one synchronous, non-yielding main-loop
   iteration — LDD-HR4): `"...freezing engine."` → `"CaptureProjectAssemblyHotReloadState:
   no-op stub..."` → `"Build finished with exit code 0..."` →
   `"RestoreProjectAssemblyHotReloadState: no-op stub..."` →
   `"...complete (new code) - engine unfrozen."`.
2. **FAILURE/ROLLBACK path** — introduced a deliberate compile error into
   `Assets/HelloGame.cpp` (`THIS_IS_A_DELIBERATE_COMPILE_ERROR_FOR_PHASE4_LIVE_TESTING garbage garbage;`
   inside `RegisterProbeGame()`). Confirmed the error was real via
   `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
   (`{"started":true}`) followed by `GET /get_logs?category=ProjectAssemblyBuild`
   showing a genuine `g++` compile error (`'THIS_IS_A_DELIBERATE_COMPILE_ERROR_FOR_PHASE4_LIVE_TESTING'
   was not declared in this scope`). Then `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe`
   → `200`, `"last_outcome":"RolledBack"`. Confirmed: `GET /list_tabs` still
   shows `"Probe Panel"`; the texture is still the last-known-good blue
   (from step 1, not orange); `GET /get_swapchain` confirms the process is
   alive and rendering normally — no crash. Reverted the compile error
   afterward.
3. **RESTORE-FAILURE → `CriticalFailure` path** — made
   `project_assemblies/ProjectAssemblyProbe_Game.dll` read-only
   (`attrib +R`) BEFORE triggering a hot reload with the still-broken
   source, so `RestoreProjectAssemblyBinariesFromBackup()`'s own
   `copy_file(..., overwrite_existing)` genuinely fails
   (`ERROR_ACCESS_DENIED`) instead of the earlier, cleaner rollback. Result:
   `200`, `"last_outcome":"CriticalFailure"`,
   `"last_error_message":"restoring the backup binaries failed - project is
   now unloaded, binaries on disk are of unknown provenance, manual
   intervention required"` — the EXACT string the phase file's own Section
   3.3 code specifies. Confirmed: `GET /list_tabs` no longer shows
   `"Probe Panel"` (project genuinely unloaded, exactly as documented);
   `GET /get_swapchain` confirms the process is still alive, still rendering
   everything else normally — no crash, matching the phase file's own
   "process never crashes" requirement even in this worst-case branch.
   **Recovery / honest methodology note**: `attrib +R` on the live `.dll`
   caused Windows' own `CopyFileW` (which `std::filesystem::copy_file`
   wraps) to propagate the read-only attribute onto the FRESH backup copy
   the very next `BackupProjectAssemblyBinaries()` call made from it — this
   caused TWO follow-up `CriticalFailure`s (`"backup failed before any
   teardown"`, a DIFFERENT branch than the one being tested) purely because
   my own manual `attrib +R` on the source file leaked into its own backup
   copy, not any product defect. Cleared with `attrib -R` on both the live
   `.dll` and the `.hotreload_backup\*.dll.bak` files, then reverted the
   compile error and re-triggered — a fresh cycle recreated the backup and
   returned to `"Success"`, fully recovering the project (`Probe Panel`
   back in `GET /list_tabs`).
4. **In-flight guard, direction A (hot-reload's own build in flight →
   concurrent `compile_only` rejected) + slow-build/message-pump check
   (single-window)** — added a TEMPORARY `add_custom_command`/
   `add_custom_target`/`add_dependencies` block to
   `Projects/ProjectAssemblyProbe/Libraries/CMakeLists.txt` (a real, literal
   `${CMAKE_COMMAND} -E sleep 20` wired as a build dependency of
   `ProjectAssemblyProbe_Game`, for testing purposes only). Triggered
   `POST /project_assembly/hot_reload` via a background `curl` process, then
   drove a small driver batch script (`curl`-based, since this environment's
   own tool-call dispatch is sequential across separate messages, not truly
   concurrent — confirmed by a direct timing experiment first) that polled
   `GET /project_assembly/hot_reload/status` every 2 seconds. Result: the
   `phase` field read `"Compiling"` continuously for **~24-26 real seconds**
   (12 consecutive polls) before flipping to `"Idle"` with
   `"last_outcome":"Success"` — direct proof the main thread was genuinely,
   synchronously frozen doing a real multi-second build (LDD-HR4) while the
   network thread kept answering unrelated concurrent requests the whole
   time, and the cycle completed cleanly afterward with the process never
   needing a force-kill. Mid-poll (poll 3), the same driver issued
   `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe` and
   got `{"reason":"a build for this project is already in progress","started":false}`
   — confirming the in-flight guard rejects a concurrent async trigger while
   a hot-reload's own synchronous build is running.
5. **In-flight guard, direction B ("vice versa" — async build in flight →
   concurrent hot-reload rejected)** — with a second, similar temporary
   Sleep()-padded custom command installed, triggered
   `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
   (`{"started":true}`) and immediately, from a second connection,
   `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe`. Result:
   `200`, `"last_outcome":"RolledBack"`; `GET /get_logs?category=ProjectAssemblyBuild`
   confirms the exact expected message,
   `"Synchronous build for Project Assembly 'ProjectAssemblyProbe' rejected
   - a build for this project is already in progress."`, and
   `GET /get_logs?category=ProjectAssemblyHotReload` confirms
   `"Synchronous build for 'ProjectAssemblyProbe' was rejected (already
   building elsewhere) - rolling back."` — this is exactly the
   "accepted narrow risk" scenario the phase file's own Section 3.3 code
   comment documents (the project was already unloaded by the time the
   guard rejected the build, so it fell through to a rollback, which
   succeeded cleanly here since the async build was still asleep and had not
   yet touched the `.dll` file). The still-running async build finished
   afterward with exit code 0 with no interference (confirmed via
   `GET /get_logs`), and the project stayed loaded and functional throughout
   (`GET /list_tabs` still showed `"Probe Panel"`).
6. **Temporary test-only edits reverted, confirmed** — both rounds of the
   `add_custom_command` Sleep() hook were removed from
   `Projects/ProjectAssemblyProbe/Libraries/CMakeLists.txt` (back to its
   original single line, `gte_add_project(ProjectAssemblyProbe)`), the
   deliberate compile error was removed from `Assets/HelloGame.cpp` (back to
   its original body), and — since the color change was itself only a
   *test-input* for the success-path check, not a permanent product change —
   `Assets/ProbeCompute.comp`'s fill color was reverted back to the original
   solid orange `(1.0, 0.35, 0.05, 1.0)`. A final `POST /project_assembly/hot_reload`
   cycle confirmed `"Success"`, `GET /get_texture` shows solid orange again,
   `GET /list_tabs` shows `"Probe Panel"`, `GET /render_graph` shows
   `"ProjectAssemblyProbe.FillTexture"` again, and `GET /get_swapchain`
   confirms the Editor is fully healthy. Stray build artifacts from the two
   temporary custom-command hooks were deleted. `GreatTamanaEditor.exe` was
   then stopped via `stop_app_background`.

**Not performed — honestly disclosed gap, not silently skipped**: the
phase file's own Definition of Done asks for the slow-build/message-pump
check to be repeated with at least one Editor panel torn out into its own
ImGui platform window (dragged outside the main OS window). No tool
available in this session can simulate a mouse drag/drop to tear a docked
ImGui panel into its own platform window — this multi-window variant of the
check was **not performed**. The single-window variant (item 4 above) was
performed thoroughly and passed. A future verification pass with either a
human operator or a dedicated UI-automation tool should still perform this
specific sub-check before treating `PumpWindowsMessagesDuringHotReloadFreeze()`'s
multi-viewport behavior as fully proven live (its own code comment already
flags this as an open question to verify).

## Confirmed deviations from the phase file

None in substance. `PerformProjectAssemblyHotReload()`'s real body,
`PumpWindowsMessagesDuringHotReloadFreeze()`, the HOOK POINT A/B stubs, the
route handler, and the two new tests were all implemented exactly as
Sections 3.1-3.6 specify, byte-for-byte in logic (only cosmetic comment
wording differs in a few places, meaning-preserving only).

## New gaps found (not part of this phase's own scope to fix)

**Pre-existing shader-staging nuance, unrelated to the Hot Reload
orchestrator itself** (found live during the success-path re-verification
step, item 6 above): a Project Assembly's own `.comp`/`.vert`/`.frag` shader
source file is compiled to `<buildDir>/shaders/<Name>.spv` by
`gte_add_shader()`, then **staged** (copied) to
`project_assemblies/shaders/<Name>.spv` (the path the running `.dll`
actually loads from at runtime) by a `POST_BUILD` step attached to the
owning `_Game`/`_Editor` CMake target. If ONLY the shader source changes
(no C++ source in that same target changes), `cmake --build`'s incremental
logic recompiles the `.spv` at its `<buildDir>/shaders/` location but does
NOT re-run the owning target's own `POST_BUILD` staging step, since the
target's own link inputs (object files) are unchanged and ninja therefore
never considers the target itself in need of rebuilding — leaving the
STALE, previously-staged `.spv` in `project_assemblies/shaders/`, silently
out of sync with the freshly-recompiled one sitting one level up. This
was discovered by hand (comparing file timestamps) after a shader-only hot
reload didn't visibly change the probe's fill color the first time it was
attempted for real; worked around for THIS task's own live testing by
manually copying the fresh `.spv` into place, never by editing any shipped
CMake/build-system file. This is a `cmake/GteProject.cmake`
(`editor-core-separation-11` campaign, PHASE4/PHASE8) build-system-level
gap, has nothing to do with `ProjectAssemblyHotReload.cpp`'s own orchestrator
logic (LDD-HR2 explicitly keeps this whole campaign's scope to the
orchestrator, never the Project Assembly build system's own CMake
plumbing), and is left unfixed here, honestly flagged for whoever
encounters it next.

## Temporary edits made under `Projects/` (never committed — `.gitignore`d),
## confirmed fully reverted

- `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` — briefly had one
  deliberate, invalid line inserted at the top of `RegisterProbeGame()`
  (Section: FAILURE/ROLLBACK path test) — reverted; final content confirmed
  byte-for-byte identical to its pre-existing form via a full file re-read.
- `Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp` — its one
  `imageStore(...)` color literal was changed from orange to blue (success-
  path test), then changed back to the original orange — confirmed via a
  full file re-read.
- `Projects/ProjectAssemblyProbe/Libraries/CMakeLists.txt` — twice had a
  temporary `add_custom_command`/`add_custom_target`/`add_dependencies`
  block appended (Sleep()-padded build, for the slow-build/message-pump and
  in-flight-guard checks) — both times fully reverted back to its original
  single line, `gte_add_project(ProjectAssemblyProbe)`, confirmed via a full
  file re-read.
- `build/project_assemblies/ProjectAssemblyProbe_Game.dll` — briefly made
  read-only (`attrib +R`) to force a restore-copy failure — cleared
  afterward (`attrib -R`).
- `build/project_assemblies/.hotreload_backup/ProjectAssemblyProbe_Game.dll.bak`/
  `ProjectAssemblyProbe_Editor.dll.bak` — inherited the read-only attribute
  from the above during one backup cycle (an unintended side effect of my
  own test methodology, documented above) — cleared (`attrib -R`) and
  confirmed subsequent backups succeed normally again.
- Stray build artifacts from the two temporary CMake custom-command hooks
  (`hotreload_phase4_slow_build_stamp.txt`/`...stamp2.txt`) were deleted; a
  temporary driver batch script and its JSON output files
  (`phase4_live_test_inflight.bat`, `phase4_hotreload_result*.json`) created
  directly under `build/` for the concurrency tests were deleted.

Final state, confirmed live before shutting the Editor down: `Probe Panel`
present in `GET /list_tabs`, `"ProjectAssemblyProbe.FillTexture"` present in
`GET /render_graph`, `GET /get_texture?texture_name=ProjectAssemblyProbe.Output`
renders solid orange (the project's own original, pre-existing baseline
appearance), `GET /project_assembly/hot_reload/status` reports
`"last_outcome":"Success"`.

## Definition of Done — verified

- [x] `PerformProjectAssemblyHotReload()` executes the full real sequence,
      in the exact stated order, with HOOK POINT A/B as logged no-op stubs.
- [x] The REAL `editorHost` pointer is threaded into every
      `LoadOneProjectAssemblyFromExactPath[IfExists]` call site (success AND
      rollback paths) — confirmed live: `"Probe Panel"` reappears in
      `GET /list_tabs` after both a successful reload and a rollback.
- [x] `ProjectAssemblyHotReloadDebugStatus::Set()`/`Finish()` wired at every
      phase transition; `GET /project_assembly/hot_reload/status`, polled
      from a second connection while a cycle runs, showed `phase` genuinely
      persisting through `"Compiling"` for ~24-26 real seconds before
      resolving.
- [x] `POST /project_assembly/hot_reload` returns a real, populated JSON
      body (never `501`) reflecting the actual final outcome — confirmed for
      `Success`/`RolledBack`/`CriticalFailure`/`503` (bridge-not-wired) all
      four.
- [x] The obsolete `HotReloadValidNameReturns501NotImplemented` test replaced
      by the two new tests (3.6); the updated
      `ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp` passes (21/21,
      filtered `ctest -R ProjectAssemblyHotReload`).
- [x] Live failure/rollback test with a deliberately-introduced compile
      error — `lastOutcome == "RolledBack"`, old pass/panel confirmed still
      present, process never crashed. Reverted afterward.
- [x] Live success test with a real, meaningful source change (the probe's
      fill color) — `lastOutcome == "Success"`, new behavior visibly
      confirmed, no relaunch of `GreatTamanaEditor.exe` at any point.
- [x] Main window confirmed NOT killed/frozen during a deliberately-slowed
      build, cross-checked against continuously-polled status — **single-
      window variant only**; the multi-window (torn-out panel) variant could
      NOT be performed (no drag/drop simulation tool available) — honestly
      disclosed above, not silently skipped.
- [x] A live test where `RestoreProjectAssemblyBinariesFromBackup()` was
      made to fail confirms `lastOutcome == "CriticalFailure"` with
      `lastErrorMessage` mentioning the restore failure specifically.
- [x] The shared in-flight guard confirmed to reject a hot-reload request
      for a project whose own prior async build is still running, and vice
      versa (compile_only rejected while a hot-reload's own build runs).
- [x] Incremental build succeeds (`gte_core`, `gte_editor`,
      `GreatTamanaEditor`, `GreatTamanaEngineTests`), zero new warnings.
