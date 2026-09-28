# PHASE3 — Hot Reload Command Bridge, Main-Loop Integration Point, and the Temporary Orchestrator Stub — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file implemented:
`PHASE3_HOT_RELOAD_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md`.

## What was actually done

Re-verified every file/line/function-name citation the phase file makes
against the real, current source tree before touching anything
(`EngineCommandBridge.h/.cpp`, `EditorHost.h` lines 154-170 area,
`EditorHost.cpp`'s `SetProjectAssemblyHost()` call site and its
`TryPeekPendingCommandRequest()` drain-point block, `NetworkServer.cpp`'s
`POST /project_assembly/hot_reload` handler, `EditorHotReloadDebugCapability.h/.cpp`,
`ProjectAssemblyHotReloadDebugStatus.h`, `ProjectAssemblyBuildRunner.h`'s
`ResolveProjectAssemblyOutputDirectory()`/`ResolveCMakeBuildDirectory()`) —
every citation was confirmed accurate (line numbers had drifted by a small,
expected amount from PHASE1/PHASE2's own edits, but every function name/
signature/relative ordering the phase file describes was correct). Also
re-read `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE0_DOUBLECHECK_REPORT.md`, `PHASE1_COMPLETION_REPORT.md`, and
`PHASE2_COMPLETION_REPORT.md` first, per the task's own instructions —
neither PHASE1 nor PHASE2 reported any gap relevant to this phase's own
scope.

### 1. `src/Application/ProjectAssemblyHotReloadCommandBridge.h` (new)

Created exactly as the phase file's own Section 3.1 code block specifies —
`SubmitAndWait()`/`TryPeekPendingProjectName()`/`FulfillPending()`, a
10-minute default timeout, and the full "read this carefully" doc comment
explaining the ONE deliberate divergence from `EngineCommandBridge`
(`m_requested` is cleared in exactly one place, `FulfillPending()`, never by
`SubmitAndWait()` itself on either the success or the timeout path).

### 2. `src/Application/ProjectAssemblyHotReloadCommandBridge.cpp` (new)

Implements the class exactly per the phase file's own Section 3.1 prose:
`SubmitAndWait()` returns `{alreadyPending=true}` immediately if already
requested; otherwise sets state and waits on the condition variable. On a
successful wakeup, returns cleanly WITHOUT touching `m_requested` a second
time (already cleared by `FulfillPending()` under the same lock before
`notify_one()`). On timeout, returns `{timedOut=true}` and deliberately does
NOT clear `m_requested` — the request stays observable via
`TryPeekPendingProjectName()` until the main thread's drain point eventually
calls `FulfillPending()`, which is the ONLY place `m_requested` is ever
cleared.

Both new files were added to the root `CMakeLists.txt`'s hand-maintained
`gte_core` source list, immediately after
`src/Application/RenderGraphControlCommandBridge.h/.cpp`'s own existing
entries (their nearest sibling in that same list).

### 3. `src/Editor/EditorHost.h`/`EditorHost.cpp` wiring

- `EditorHost.h`: added `#include "../Application/ProjectAssemblyHotReloadCommandBridge.h"`
  next to the other bridge includes, and a new member,
  `ProjectAssemblyHotReloadCommandBridge m_hotReloadCommandBridge;`,
  declared immediately after `m_renderGraphControlCommandBridge` — in the
  same "declared BEFORE `m_networkServer`" group as every other bridge.
- `EditorHost.cpp`: added `#include "../Core/Plugins/ProjectAssemblyHotReload.h"`
  near the other Project-Assembly-Hot-Reload includes (`ProjectAssemblyBuildRunner.h`
  was already included there, from PHASE1/PHASE2's own work, and
  `ProjectRootPath.h` — needed for `gte::ExecutableDirectory()` — was
  already included too). Added
  `s_editorHotReloadDebugCapability.SetHotReloadCommandBridge(m_hotReloadCommandBridge);`
  immediately after the existing `SetProjectAssemblyHost(...)` call in the
  constructor body. Added the new drain point immediately after the
  existing `m_commandBridge.TryPeekPendingCommandRequest()` block (right
  before `const Uint64 nowTicksNs = ...`), exactly matching the phase
  file's own Section 3.5 code block — peeks the pending project name,
  resolves `outputDirectory`/`buildDirectory` via
  `ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory())`/
  `ResolveCMakeBuildDirectory(gte::ExecutableDirectory())` on the main
  thread (gte_editor-tier), calls `PerformProjectAssemblyHotReload(...)`,
  then `FulfillPending()`.

**One authoring mistake caught and corrected during this phase**: the first
`edit_line` call that inserted the new drain point (targeting a blank line
right after the existing `EngineCommandBridge` block) accidentally swallowed
the closing `}` of that PRE-EXISTING `if (... m_commandBridge.TryPeekPendingCommandRequest())`
block instead of leaving it alone (a line-offset miscount on my part, not a
tool malfunction). Caught immediately by re-reading the file in full right
afterward, before running any build — fixed with one follow-up `edit_line`
call restoring the missing `}`. Verified again by a fresh full read of the
surrounding ~50 lines before compiling. No lasting effect.

### 4. `src/Core/Plugins/ProjectAssemblyHotReload.h`/`.cpp` (new)

Created exactly per the phase file's own Section 3.4 code blocks — the
PERMANENT 6-parameter signature
(`projectName, Core& core, Renderer& renderer, EditorHost* editorHost,
const std::filesystem::path& outputDirectory, const std::filesystem::path& buildDirectory`)
with the TEMPORARY body: logs a `GTE_LOG_WARNING`, then
`ProjectAssemblyHotReloadDebugStatus::Instance().Set("CapturingState", projectName)`
followed by `.Finish("CriticalFailure", "PHASE3 temporary stub - PHASE4 not yet implemented")`.
Both files added to the root `CMakeLists.txt`'s hand-maintained `gte_core`
source list, immediately after `src/Core/Plugins/ProjectAssemblyHost.h/.cpp`'s
own existing entries (their nearest sibling, same folder).

### 5. `src/Editor/EditorHotReloadDebugCapability.h`/`.cpp`

- `.h`: added a forward declaration,
  `class ProjectAssemblyHotReloadCommandBridge;`, alongside the existing
  `class ProjectAssemblyHost;` one; added the new public setter
  `void SetHotReloadCommandBridge(ProjectAssemblyHotReloadCommandBridge& bridge) noexcept;`
  right after `SetProjectAssemblyHost()`; added the new private member
  `ProjectAssemblyHotReloadCommandBridge* m_hotReloadCommandBridge = nullptr;`.
- `.cpp`: added `#include "../Application/ProjectAssemblyHotReloadCommandBridge.h"`.
  Replaced `TriggerHotReload()`'s old permanent `return false;` placeholder
  body with the real one from the phase file's own Section 3.3: a defensive
  null check on the bridge pointer, `SubmitAndWait(projectName)`, then
  `alreadyPending -> false`, `timedOut -> false` (with the phase file's own
  documented reasoning that the cycle keeps running on the main thread
  regardless), otherwise `true`. Added `SetHotReloadCommandBridge()`'s body
  (a one-line pointer assignment), mirroring `SetProjectAssemblyHost()`'s
  own exact shape.

**One authoring mistake caught and corrected during this phase**: my first
`edit_line` call inserting the new include accidentally targeted the wrong
existing line (meant to insert a comment+include block AFTER the existing
`ProjectAssemblyHotReloadDebugStatus.h` include, but instead replaced that
very include line with the new block, and then a second stray duplicate
`ProjectAssemblyRegistrationLedger.h` include line was left over). Caught
immediately by re-reading the file in full right after the edit (before
touching anything else) — fixed with one follow-up `edit_line` call that
replaced the whole broken 6-line include block with the correct 7-line
version (restoring the accidentally-removed `ProjectAssemblyHotReloadDebugStatus.h`
include, keeping `ProjectAssemblyRegistrationLedger.h` exactly once, and
keeping the new `ProjectAssemblyHotReloadCommandBridge.h` include). Verified
again by a fresh full read before proceeding. No lasting effect — mentioned
here for the record, not because any tool is considered malfunctioning
(both mistakes were in the `contents`/`line_index` I supplied, not a tool
bug); `edit_line`'s own auto-dedup safety net did catch and flag the second,
unrelated duplicate in the EditorHotReloadDebugCapability.h private-member
edit (see below), which is exactly the kind of authoring-mistake detection
it exists for.

### 6. `tests/Application/ProjectAssemblyHotReloadCommandBridgeTests.cpp` (new)

All 5 tests the phase file's own Section 3.7 lists, mirroring
`EngineCommandBridgeTests.cpp`'s own proven shape:

- `RequestWithNoServicerTimesOutButStaysPending` — the most important one:
  proves a timed-out `SubmitAndWait()` leaves the request observable via
  `TryPeekPendingProjectName()` afterward.
- `FulfillPendingAfterATimeoutClearsTheSlotForAFreshRequest` — proves a late
  `FulfillPending()` call clears the slot and a fresh request afterward is
  serviced normally.
- `FulfilledBeforeTimeoutReturnsCleanlyWithNeitherFlagSet` — proves the
  condition-variable wakeup path (not the timeout) resolves a genuinely
  fulfilled request quickly.
- `SecondConcurrentRequestReturnsAlreadyPendingImmediately` — proves a
  second concurrent `SubmitAndWait()` call returns immediately with
  `alreadyPending == true`, never actually waiting.
- `FulfillPendingWithNothingPendingIsASafeNoOp` — proves calling
  `FulfillPending()` on a freshly-constructed bridge never crashes/asserts.

Added to `tests/CMakeLists.txt`'s own hand-maintained source list,
immediately after `Application/RenderGraphControlCommandBridgeTests.cpp`'s
own existing entry.

## Build / test verification (incremental only, per this phase's scope)

- `cmake --build build --target gte_core` — succeeded (2 new files
  compiled: `ProjectAssemblyHotReloadCommandBridge.cpp`,
  `ProjectAssemblyHotReload.cpp`; relink of `libgte_core.a`), zero new
  warnings.
- `cmake --build build --target gte_editor` — succeeded (2 files
  recompiled: `EditorHotReloadDebugCapability.cpp`, `EditorHost.cpp`;
  relink of `libgte_editor.a`), zero new warnings.
- `cmake --build build --target GreatTamanaEditor` — succeeded, relinked
  `GreatTamanaEditor.exe`.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (new
  test file compiled, `GreatTamanaEngineTests.exe` relinked), zero new
  warnings.
- `ctest -C Debug --output-on-failure -R ProjectAssemblyHotReloadCommandBridge`
  — 5/5 tests passed:
  - `RequestWithNoServicerTimesOutButStaysPending` — **Passed** (0.19s)
  - `FulfillPendingAfterATimeoutClearsTheSlotForAFreshRequest` — **Passed** (0.12s)
  - `FulfilledBeforeTimeoutReturnsCleanlyWithNeitherFlagSet` — **Passed** (0.06s)
  - `SecondConcurrentRequestReturnsAlreadyPendingImmediately` — **Passed** (1.58s)
  - `FulfillPendingWithNothingPendingIsASafeNoOp` — **Passed** (0.06s)
- **Live smoke test** (this phase's own Section 3.6, required): background-
  launched `GreatTamanaEditor.exe`, confirmed it rendered via
  `GET /get_swapchain` (real screenshot, Editor UI visible), then:
  - `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` returned
    exactly the predicted **HTTP 500**, body
    `{"error":"unexpected: TriggerHotReload() reported success in a build with no real orchestrator","success":false}`
    — confirming `TriggerHotReload()` now genuinely calls into the bridge,
    the drain point ran the temporary orchestrator body, and
    `FulfillPending()` was called, all before the (still-unmodified-this-
    phase) route handler fell through its own pre-existing `else` branch.
  - `GET /project_assembly/hot_reload/status` immediately after returned
    `{"cycle_id":1,"last_error_message":"PHASE3 temporary stub - PHASE4 not yet implemented","last_outcome":"CriticalFailure","phase":"Idle","phase_elapsed_ms":0,"project_name":"ProjectAssemblyProbe"}`
    — `lastOutcome == "CriticalFailure"` with `lastErrorMessage` containing
    `"PHASE3 temporary stub"`, exactly as predicted.
  - `GET /get_logs?category=ProjectAssemblyHotReload` confirmed the real
    `GTE_LOG_WARNING` line:
    `"PerformProjectAssemblyHotReload('ProjectAssemblyProbe') called - PHASE4 has not replaced this temporary body yet; no real reload occurred."`
  - Stopped the background process afterward (`stop_app_background`).

No full build and no full regression `ctest` run were performed, per this
phase's own explicit scope (PHASE5's job).

## Confirmed deviations from the phase file

None in substance. Two authoring mistakes made and self-corrected during
implementation are documented above (both caught by re-reading the file
before compiling, not by any tool malfunction) — neither left any trace in
the final code, confirmed by the full file re-reads shown above and by the
zero-warning incremental builds.

## New gaps found

None. This phase's own scope (bridge, `EditorHost` wiring, temporary
orchestrator stub, tests) was implemented exactly as PHASE0/
PHASE0_DOUBLECHECK_REPORT.md described, with no new architecture/layering/
deadlock hazard discovered.

## Definition of Done — verified

- [x] `ProjectAssemblyHotReloadCommandBridge` exists, compiles, mirrors
      `EngineCommandBridge`'s proven shape; all 5 new Tier-1 tests pass, in
      particular confirming a timed-out `SubmitAndWait()` leaves the
      request observable via `TryPeekPendingProjectName()` afterward.
- [x] `EditorHost` owns it as a new member, in the correct declared-before-
      `m_networkServer` position; `EditorHotReloadDebugCapability` reaches
      it via the new `SetHotReloadCommandBridge()` setter, wired once from
      `EditorHost`'s constructor body.
- [x] `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp` exist with the
      PERMANENT signature and the TEMPORARY body above.
- [x] `EditorHost::Run()`'s new drain point exists, placed exactly where
      specified.
- [x] The live smoke test (3.6) passes: `POST /project_assembly/hot_reload`
      no longer answers a bare `501`; `GET .../status` shows the temporary
      `"CriticalFailure"`/"PHASE3 temporary stub" outcome, proving the
      whole chain is wired.
- [x] Incremental build succeeds (`gte_core`, `gte_editor`,
      `GreatTamanaEditor`, `GreatTamanaEngineTests`), zero new warnings.

## Note for PHASE4 (restated, not re-derived)

- `PerformProjectAssemblyHotReload()`'s signature is now permanent and
  compiling — PHASE4 replaces ONLY its body.
- `TriggerHotReload()`'s body is now real (submits into the bridge) — PHASE4
  does not need to touch it again; PHASE4's own job is the route handler's
  `else`/success-path branches in `NetworkServer.cpp` (still answering `501`/
  the now-reachable `500` respectively) and the real orchestrator logic
  itself.
- `m_hotReloadCommandBridge`/`SetHotReloadCommandBridge()` are both fully
  wired — no further plumbing needed for PHASE4 to have a live bridge to
  read from.
