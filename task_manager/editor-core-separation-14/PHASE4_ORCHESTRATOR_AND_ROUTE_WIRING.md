# PHASE4 — The Real Orchestrator, Message Pump, Status Wiring, and Route Response

Parent: `PHASE0_MASTER_STRATEGY.md`.
Depends on: PHASE1 (`LoadOneProjectAssemblyFromExactPath[IfExists]`,
`GetHotReloadEngineStateMutex()` closure), PHASE2
(`TryRunProjectAssemblyBuildSynchronously`/`BuildOutcome`), PHASE3 (the
bridge, the drain point, `ProjectAssemblyHotReload.h`'s signature) — **all
three must already be done and compiling**, not merely started.
Blocks: PHASE5 (live verification needs the real orchestrator to exist).

---

## STEP 1 — The Goal

`PerformProjectAssemblyHotReload()`'s TEMPORARY PHASE3 body is replaced
with the full, real, ordered sequence: freeze (already true by construction
— it runs synchronously on the main thread) -> capture state (HOOK POINT
A, a no-op stub this campaign) -> back up binaries -> unload (GPU-safe,
ledger-safe) -> compile synchronously (with a Windows message pump keeping
the OS happy) -> on success, load the fresh binaries; on any failure,
restore the backup and reload the OLD binaries -> restore state (HOOK
POINT B, a no-op stub) -> done. `ProjectAssemblyHotReloadDebugStatus`
reports every phase transition. `TriggerHotReload()`'s permanent placeholder
is replaced with the real bridge-submission body (already written in
PHASE3 — unchanged here). The `POST /project_assembly/hot_reload` route
handler is updated to build a REAL, informative response from the ALREADY-
EXISTING, UNCHANGED `GetHotReloadStatus()` method once `TriggerHotReload()`
returns — no interface signature changes anywhere.

## STEP 2 — The Situation

Confirmed from PHASE1/PHASE2/PHASE3 (all already done by this point):
- `core.GetProjectAssemblyHost().UnloadProjectAssembly(projectName, core, renderer)`
  — locks `GetHotReloadEngineStateMutex()` internally (PHASE1). **This
  orchestrator must NEVER wrap this call in its own outer lock on the same
  mutex** (non-recursive `std::mutex` — see `HotReloadEngineStateMutex.h`
  line 40 — double-locking deadlocks immediately).
- `core.GetProjectAssemblyHost().LoadOneProjectAssemblyFromExactPath(path, core, editorHost)`
  / `...IfExists(...)` — same internal locking (PHASE1).
- `BackupProjectAssemblyBinaries(projectName, outputDirectory)` /
  `RestoreProjectAssemblyBinariesFromBackup(projectName, outputDirectory)`
  (`ProjectAssemblyBuildRunner.h`) — pure file I/O, no engine-state mutex
  involvement at all (confirmed, `ProjectAssemblyBuildRunner.cpp`).
- `TryRunProjectAssemblyBuildSynchronously(projectName, buildDirectory, outOutcome, onIdleTick)`
  (PHASE2) — shares the existing in-flight guard; returns `false` only if
  rejected (already building elsewhere), never for a build that ran and
  failed.
- `ProjectAssemblyHotReloadDebugStatus::Instance().Set(phase, projectName)`
  / `.Finish(outcome, errorMessage)` — real, already-proven-in-isolation
  (BIG-STEP 1 PHASE1's own Tier-1 tests).
- `outputDirectory`/`buildDirectory` are NOW TWO EXPLICIT PARAMETERS of
  `PerformProjectAssemblyHotReload()` itself (PHASE3's own revised
  signature) — resolved by the CALLER, `EditorHost::Run()`'s drain point
  (`ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory())`/
  `ResolveCMakeBuildDirectory(gte::ExecutableDirectory())`, the same two
  resolvers `EditorHost.cpp`'s own constructor / `EditorHotReloadDebugCapability::
  TriggerCompileOnly()` already call). **This orchestrator itself must NEVER
  call `gte::ExecutableDirectory()`** — that function is gte_editor-tier
  (`src/Editor/ProjectRootPath.cpp`), while this file
  (`src/Core/Plugins/ProjectAssemblyHotReload.cpp`) is gte_core-tier (same
  CMake source list as every other file in `src/Core/Plugins/`) — calling it
  directly here would be a real `gte_core` -> `gte_editor` layering
  violation, confirmed by this campaign's own PHASE0 review (Section 2.2,
  item 6) to compile fine but silently break the one CI probe designed to
  catch exactly this,`tools/ci/gte_core_player_link_probe/` (a separate,
  manually-invoked project, never part of an ordinary `cmake --build build`).
  Just use the two parameters as handed in — do not re-resolve anything.

## STEP 3 — The Plan

### 3.1 — HOOK POINT A/B stub types, in `ProjectAssemblyHotReload.h`

```cpp
// editor-core-separation-14 campaign, PHASE4. A deliberately EMPTY
// placeholder value type - a future BIG-STEP 4 campaign
// (HOTRELOAD_BIGSTEP_04_STATE_SNAPSHOT_RESTORE_AND_VERIFICATION_PLAN_2026-09-28.txt)
// gives this real fields (reusing gte::SceneDocument - see that file's own
// design). Kept as a named type (not "just skip these two calls entirely")
// so this orchestrator's own call sites/control flow never need to change
// shape when BIG-STEP 4 lands - only these two functions' BODIES change.
struct HotReloadStateSnapshot {};

// HOOK POINT A - called BEFORE anything is torn down. No-op stub this
// campaign (returns a default-constructed, empty snapshot).
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core);

// HOOK POINT B - called AFTER the (new-or-rolled-back) code's own
// GTE_RegisterProject has already run, so every render-pass/panel/
// component-type registration the now-running code needs already exists.
// No-op stub this campaign.
void RestoreProjectAssemblyHotReloadState(Core& core, const HotReloadStateSnapshot& snapshot);
```

`.cpp` stub bodies (log once, at INFO level, so a live test can confirm
these were reached without needing to guess):

```cpp
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& /*core*/)
{
    GTE_LOG_INFO("ProjectAssemblyHotReload", "CaptureProjectAssemblyHotReloadState: no-op stub (BIG-STEP 4 not yet implemented).");
    return HotReloadStateSnapshot{};
}

void RestoreProjectAssemblyHotReloadState(Core& /*core*/, const HotReloadStateSnapshot& /*snapshot*/)
{
    GTE_LOG_INFO("ProjectAssemblyHotReload", "RestoreProjectAssemblyHotReloadState: no-op stub (BIG-STEP 4 not yet implemented).");
}
```

### 3.2 — The Windows message pump, as an `onIdleTick` lambda

Defined LOCALLY inside `PerformProjectAssemblyHotReload()`'s own `.cpp`
(needs `<windows.h>`), passed to `TryRunProjectAssemblyBuildSynchronously()`
as its `onIdleTick` argument. Deliberately simpler than the external plan's
own "check a global flag" sketch: this lambda ONLY EVER RUNS while
literally inside this one, synchronous, main-thread call — there is no
ambiguity about "is a hot reload in progress" to check, so no separate flag
is needed at all:

```cpp
namespace {
void PumpWindowsMessagesDuringHotReloadFreeze()
{
    // editor-core-separation-14 campaign, PHASE4. Keeps Windows from
    // marking the main window "Not Responding" during a long, deliberately
    // -blocking hot-reload compile (LDD-HR4) - PURELY cosmetic OS
    // bookkeeping, never touches game/render state, never calls
    // SDL_PollEvent() (which would let a queued input event leak into a
    // "frame" that, per LDD-HR4, must not run any game/render logic this
    // iteration - this function is called from INSIDE
    // TryRunProjectAssemblyBuildSynchronously()'s own poll loop, NOT from
    // EditorHost::Run()'s own per-frame SDL_PollEvent() block).
    //
    // hWnd = nullptr is DELIBERATE, not "forgot to filter": ImGui
    // multi-viewport is enabled in this codebase
    // (ImGuiConfigFlags_ViewportsEnable, ImGuiEditorLayer.cpp) - any panel
    // dragged outside the main OS window becomes its own real, extra
    // SDL3-created top-level window (src/Window/Window.h's own header
    // comment), created/destroyed on THIS SAME (main) thread by
    // imgui_impl_sdl3.cpp's platform callbacks. A NULL hWnd makes
    // PeekMessage() service EVERY window this thread owns, not only the
    // main one - restricting this to just the main window's own HWND would
    // starve any currently-torn-off panel window of its own message pump,
    // so IT would be the one Windows marks "Not Responding" instead.
    //
    // EVERY message, including WM_CLOSE, is dispatched normally below -
    // deliberately NOT swallowed. This is safe: SDL3's own Win32 window
    // procedure handles WM_CLOSE by pushing an SDL_EVENT_WINDOW_CLOSE_REQUESTED
    // event into SDL's OWN internal event queue and returning - it never
    // calls DestroyWindow()/PostQuitMessage() itself, and nothing reads
    // that queue until EditorHost::Run()'s own per-frame SDL_PollEvent()
    // call, which cannot run until this whole synchronous freeze returns.
    // "nothing is acted on until the cycle finishes" therefore already
    // holds for every message type, with zero special-casing needed -
    // dropping WM_CLOSE via a `continue` would be a real bug, not a safe
    // simplification: PeekMessage(..., PM_REMOVE) permanently removes the
    // message from the queue, so skipping DispatchMessage() for it means
    // SDL's own window procedure never runs for that click at all and the
    // close request is silently LOST forever (the user has to click the
    // close button again after the freeze ends), not merely deferred as
    // intended.
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
} // namespace
```

**Verify concretely during implementation** (this is a real, open question
this strategy flags rather than assumes): confirm SDL3's own window
procedure does not itself re-enter engine/render code unsafely when
`DispatchMessage()` runs a message OTHER than WM_CLOSE (e.g. `WM_PAINT`,
`WM_SIZE`) for ANY of this thread's windows (main OR an ImGui viewport
platform window) mid-freeze — test this live via PHASE5's own
"deliberately slowed build" Definition-of-Done check, watching for any
crash or visible corruption, on a run that has at least one panel torn out
into its own platform window at the time the freeze starts (the
multi-window case is the one this file's own review flagged as previously
untested). If a specific message type proves unsafe, add back a narrow,
documented swallow for exactly that one message value (never WM_CLOSE -
see above for why that one specifically must always be dispatched)
directly in this function's own comment.

### 3.3 — The real `PerformProjectAssemblyHotReload()` body

Replaces PHASE3's temporary body entirely, in
`src/Core/Plugins/ProjectAssemblyHotReload.cpp`:

```cpp
#include "ProjectAssemblyHotReload.h"

#include "../Core.h"
#include "../EditorCapabilities.h" // ProjectAssemblyHost via Core::GetProjectAssemblyHost().
#include "../Logging.h"
#include "ProjectAssemblyBuildRunner.h"
#include "ProjectAssemblyHotReloadDebugStatus.h"
#include "../../Renderer/Renderer.h"
// Deliberately NO "../../Editor/ProjectRootPath.h" include here, and NO call
// to gte::ExecutableDirectory() anywhere in this file - this is gte_core-tier
// code (src/Core/Plugins/, same CMake source list as ProjectAssemblyHost.cpp/
// ProjectAssemblyBuildRunner.cpp) and gte::ExecutableDirectory() is
// gte_editor-tier - see this function's own new outputDirectory/buildDirectory
// PARAMETERS below (resolved by the caller, EditorHost::Run(), PHASE3) and
// PHASE0_MASTER_STRATEGY.md Section 2.2 item 6 for the full reasoning.

#include <windows.h>

namespace gte {

namespace {
void PumpWindowsMessagesDuringHotReloadFreeze() { /* ... 3.2 above ... */ }
} // namespace

void PerformProjectAssemblyHotReload(const std::string& projectName, Core& core, Renderer& renderer,
    EditorHost* editorHost, const std::filesystem::path& outputDirectory, const std::filesystem::path& buildDirectory)
{
    GTE_LOG_INFO("ProjectAssemblyHotReload", "Hot reload requested for '" + projectName + "' - freezing engine.");
    ProjectAssemblyHotReloadDebugStatus::Instance().Set("CapturingState", projectName);

    const HotReloadStateSnapshot snapshot = CaptureProjectAssemblyHotReloadState(core);

    if (buildDirectory.empty()) {
        GTE_LOG_ERROR("ProjectAssemblyHotReload", "Aborting - could not resolve the CMake build directory for '" + projectName + "'.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "could not resolve CMake build directory - nothing changed");
        return;
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("BackingUpBinaries", projectName);
    if (!BackupProjectAssemblyBinaries(projectName, outputDirectory)) {
        GTE_LOG_ERROR("ProjectAssemblyHotReload", "Aborting - could not back up current binaries for '" + projectName + "'.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "backup failed before any teardown - nothing changed");
        return;
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("Unloading", projectName);
    // Locks GetHotReloadEngineStateMutex() INTERNALLY (PHASE1) - do not
    // wrap this call in an outer lock on the same mutex (non-recursive,
    // would deadlock).
    core.GetProjectAssemblyHost().UnloadProjectAssembly(projectName, core, renderer);

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("Compiling", projectName);
    BuildOutcome outcome;
    const bool buildRan = TryRunProjectAssemblyBuildSynchronously(
        projectName, buildDirectory.string(), outcome, &PumpWindowsMessagesDuringHotReloadFreeze);
    if (!buildRan) {
        // Rejected by the shared in-flight guard (an async "Compile"
        // build for this SAME project is already running) - the project
        // is ALREADY unloaded at this point (see above), so this is not a
        // harmless no-op like a rejected async trigger would be; treat it
        // as a compile failure and fall through to the rollback path
        // below, exactly like any other failed-to-produce-a-good-binary
        // case. KNOWN, ACCEPTED NARROW RISK: if that OTHER, still-running
        // async build's own child `cmake --build` process still holds an
        // open write handle on this SAME project's output .dll at this
        // exact moment, the rollback below's file copy can race it (most
        // likely outcome: the copy fails with a sharing violation, which
        // RestoreProjectAssemblyBinariesFromBackup()'s own return value
        // check below turns into an honest CriticalFailure rather than a
        // silently-wrong result) - not fully closable from this phase
        // alone without PHASE2 exposing a plain, pre-flight "is this
        // project already in flight" query, which does not exist today.
        GTE_LOG_ERROR("ProjectAssemblyHotReload",
            "Synchronous build for '" + projectName + "' was rejected (already building elsewhere) - rolling back.");
        outcome.success = false;
    } else {
        GTE_LOG_INFO("ProjectAssemblyHotReload",
            "Build finished with exit code " + std::to_string(outcome.exitCode) +
            " - hot reload will now attempt to " + (outcome.success ? "load the result." : "roll back to the last known-good binaries."));
    }

    bool reloadedSuccessfully = false;
    if (outcome.success) {
        ProjectAssemblyHotReloadDebugStatus::Instance().Set("ReloadingNewCode", projectName);
        ProjectAssemblyHost& host = core.GetProjectAssemblyHost();
        reloadedSuccessfully =
            host.LoadOneProjectAssemblyFromExactPath(outputDirectory / (projectName + "_Game.dll"), core, editorHost)
            && host.LoadOneProjectAssemblyFromExactPathIfExists(outputDirectory / (projectName + "_Editor.dll"), core, editorHost);
    }

    if (!reloadedSuccessfully) {
        GTE_LOG_WARNING("ProjectAssemblyHotReload",
            "Compile/reload failed for '" + projectName + "' - rolling back to the last known-good binaries.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Set("RollingBack", projectName);
        if (!RestoreProjectAssemblyBinariesFromBackup(projectName, outputDirectory)) {
            // The backup copy-back itself failed (missing/unreadable backup,
            // or a sharing violation against a straggling build process -
            // see the in-flight-rejection comment above) - whatever is
            // currently sitting at outputDirectory is now of UNKNOWN
            // provenance (could be the failed compile's own bad output,
            // could be a partially-overwritten file). Loading it anyway
            // and reporting "RolledBack" would be actively dishonest (it
            // may well be running the BROKEN new code while claiming to
            // have rolled back) - abort here instead, matching the
            // existing "rollback itself failed" CriticalFailure shape
            // immediately below.
            GTE_LOG_ERROR("ProjectAssemblyHotReload",
                "CRITICAL: restoring the backup binaries failed for '" + projectName + "' - this project is now UNLOADED, and the binaries on disk are of unknown provenance. Manual intervention required.");
            ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure",
                "restoring the backup binaries failed - project is now unloaded, binaries on disk are of unknown provenance, manual intervention required");
            return;
        }
        ProjectAssemblyHost& host = core.GetProjectAssemblyHost();
        const bool rolledBack =
            host.LoadOneProjectAssemblyFromExactPath(outputDirectory / (projectName + "_Game.dll"), core, editorHost)
            && host.LoadOneProjectAssemblyFromExactPathIfExists(outputDirectory / (projectName + "_Editor.dll"), core, editorHost);
        if (!rolledBack) {
            GTE_LOG_ERROR("ProjectAssemblyHotReload",
                "CRITICAL: rollback itself failed for '" + projectName + "' - this project is now UNLOADED. Manual intervention required.");
            ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure",
                "rollback itself failed - project is now unloaded, manual intervention required");
            return;
        }
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("RestoringState", projectName);
    RestoreProjectAssemblyHotReloadState(core, snapshot);

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "Hot reload cycle for '" + projectName + "' complete (" + (reloadedSuccessfully ? "new code" : "rolled back") + ") - engine unfrozen.");
    ProjectAssemblyHotReloadDebugStatus::Instance().Finish(reloadedSuccessfully ? "Success" : "RolledBack", "");
}

} // namespace gte
```

**Two points worth stating explicitly, since both are easy to get wrong by
analogy with other, superficially-similar code elsewhere in this codebase**:

1. **`editorHost` is the REAL, live `EditorHost*` parameter, threaded through
   into BOTH `LoadOneProjectAssemblyFromExactPath[IfExists]` call sites
   (success path AND rollback path) above - never `nullptr`.** Re-loading
   `_Editor.dll` calls `GTE_RegisterProject(Core&, EditorHost&)` again,
   which (per `ProjectAssemblyHost.cpp`'s own confirmed body) re-registers
   this project's editor panel via `EditorPanelRegistry`; passing `nullptr`
   here would make `TryLoadOneAssembly` skip the `_Editor.dll` entirely
   (logged warning, not a crash - but a silently incomplete reload: the
   project's own render pass/component types would come back, but not its
   Editor panel). This is exactly why `PerformProjectAssemblyHotReload()`'s
   own parameter list already carries `EditorHost* editorHost` (PHASE3) -
   there is no other, indirect way to reach a live `EditorHost&` from this
   free function.
2. `ProjectAssemblyHost::Instance()` does not exist - every call above goes
   through `core.GetProjectAssemblyHost()` (PHASE0's own Correction,
   restated here at the exact point it matters).

### 3.4 — `TriggerHotReload()` — already real since PHASE3, unchanged here

No further edit needed — PHASE3 already gave it its permanent, real body
(submits into the bridge, waits, returns `true`/`false`). Restated here
only so this phase's own Definition of Done can re-confirm it.

### 3.5 — `POST /project_assembly/hot_reload` route handler — real response

`NetworkServer.cpp`, replace the handler body (lines 1345-1367) with:

```cpp
server.Post("/project_assembly/hot_reload",
    [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
    const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
    if (!parsed.valid) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
        return;
    }
    if (hotReloadDebugCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
        return;
    }
    // editor-core-separation-14 campaign (BIG-STEP 3), PHASE4 - real body.
    // TriggerHotReload() now BLOCKS until the whole cycle finishes (see
    // ProjectAssemblyHotReloadCommandBridge::SubmitAndWait()'s long
    // default timeout) - `started` here means "the cycle ran to
    // completion" (regardless of Success/RolledBack/CriticalFailure - see
    // GetHotReloadStatus() below for that), NOT "the cycle succeeded".
    // false means REJECTED (already in flight, or the caller gave up
    // waiting - see SubmitAndWait()'s own doc comment for the latter).
    const bool started = hotReloadDebugCapability->TriggerHotReload(parsed.projectName);
    if (!started) {
        res.status = 503;
        res.set_content(
            BuildGenericErrorResponseJson("hot reload rejected - a build for this project may already be in progress, or this request timed out waiting for the main thread"),
            "application/json");
        return;
    }
    // Reuses the EXISTING BuildHotReloadStatusResponseJson() builder
    // (already used by GET /project_assembly/hot_reload/status since
    // BIG-STEP 1) - zero new JSON-shape code needed. The IHotReloadDebugCapability
    // interface itself needed ZERO signature changes for this whole
    // campaign, exactly as BIG-STEP 1/2 promised downstream campaigns.
    res.set_content(BuildHotReloadStatusResponseJson(hotReloadDebugCapability->GetHotReloadStatus()), "application/json");
});
```

Delete the now-obsolete `501`/unreachable-`500` comment block that used to
sit above this handler (it describes the OLD placeholder behavior).

### 3.6 — Update the now-OBSOLETE pre-existing route test

`tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp` (BIG-STEP
1) has one test that HARD-CODES the OLD, permanent-until-now `501` contract
this exact phase deliberately replaces:
`ProjectAssemblyHotReloadEndpointsEndToEndTest.HotReloadValidNameReturns501NotImplemented`
(asserts `res->status == 501`). After 3.5 above, this route never returns
`501` again for a valid request — that test WILL fail the moment this
phase's route-handler change lands, and it is NOT a real regression, just a
stale assumption that must be fixed in THIS SAME change (`AGENTS.md`'s
Testability rule: a behavior change to already-tested code must come with a
matching test update in the same change, never discovered later during
PHASE5's full `ctest` pass). Replace it with:

- `HotReloadValidNameReturns503WhenBridgeIsNotWired` — same request
  (`POST /project_assembly/hot_reload?name=SomeProject`), but now asserting
  `res->status == 503` with an `"error"` field mentioning rejection/timeout
  — this test's own `m_capability` fixture (`SetUp()`) never calls
  `SetHotReloadCommandBridge()` (there is no `EditorHost` in this
  network-only fixture), so `TriggerHotReload()` hits its own
  `m_hotReloadCommandBridge == nullptr` guard (PHASE3, 3.3) and returns
  `false` immediately — proving the route's OWN new `503` branch (3.5),
  never the deleted `501` one.
- `HotReloadValidNameReturns200WithRealStatusBodyWhenBridgeIsServiced` — a
  NEW test, mirroring this SAME file's own existing `FakeSceneSnapshotStandIn`
  precedent (search this file for that class) exactly: a small
  `FakeHotReloadStandIn` background thread that loops calling
  `ProjectAssemblyHotReloadCommandBridge::TryPeekPendingProjectName()`/
  `FulfillPending()` (nothing else — it deliberately never calls the real
  `PerformProjectAssemblyHotReload()`, exactly like `FakeSceneSnapshotStandIn`
  never calls the real `BuildSceneSnapshotJson()`), wired into a REAL
  `ProjectAssemblyHotReloadCommandBridge` + a REAL `EditorHotReloadDebugCapability`
  whose `SetHotReloadCommandBridge()` is called in this test's own `SetUp()`
  (unlike the OTHER test class in this file, which deliberately never calls
  it) — proves the capability<->bridge<->route wiring succeeds end-to-end
  with a `200` response and a well-formed status JSON body, without needing
  a real `EditorHost`/compile/reload at all (that end-to-end proof is
  PHASE5's own live verification job, not this Tier-1-adjacent test's).

Add this new test's own fixture class to `tests/CMakeLists.txt` only if it
lands in a NEW file — if it is added to the SAME, already-registered
`ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp` file (recommended,
since it needs the exact same includes/precedent already in that file),
no `CMakeLists.txt` change is needed at all.

## STEP 4 — Definition of Done — this phase only

- [ ] `PerformProjectAssemblyHotReload()` executes the full real sequence
      above, in the exact stated order, with HOOK POINT A/B as logged
      no-op stubs.
- [ ] `editorHost` (the REAL pointer, not `nullptr`) is threaded correctly
      into every `LoadOneProjectAssemblyFromExactPath[IfExists]` call site
      (success path AND rollback path) — confirmed by a live test that a
      project WITH an Editor panel (`ProjectAssemblyProbe`) shows its panel
      again in `GET /list_tabs` after a successful reload.
- [ ] `ProjectAssemblyHotReloadDebugStatus::Set()`/`Finish()` are wired at
      every phase transition; `GET /project_assembly/hot_reload/status`,
      polled from a second connection while a cycle runs on a first, shows
      `phase` genuinely advancing through every named value in order.
- [ ] `POST /project_assembly/hot_reload` returns a real, populated JSON
      body (not `501`) reflecting the ACTUAL final outcome.
- [ ] The now-obsolete `HotReloadValidNameReturns501NotImplemented` test
      (3.6) is replaced by the two new tests described there, and the
      updated `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`
      passes (an incremental/filtered `ctest` run this phase — the full
      suite runs in PHASE5).
- [ ] A live test with a DELIBERATELY-introduced compile error in
      `ProjectAssemblyProbe`'s own source (confirm the error is real via
      `POST /project_assembly/debug/compile_only` first) proves the
      FAILURE/rollback path: `lastOutcome == "RolledBack"`, the OLD
      render pass/panel are confirmed still present
      (`GET /render_graph`, `GET /list_tabs`) immediately after, process
      never crashes.
- [ ] A live test with a REAL, meaningful source change (e.g. the probe's
      compute-shader fill color) proves the SUCCESS path:
      `lastOutcome == "Success"`, the NEW behavior is visible
      (`GET /render_graph` / a visibly different `GET /get_swapchain`),
      no relaunch of `GreatTamanaEditor.exe` at any point.
- [ ] The main window is confirmed NOT killed/force-closed by Windows
      during a compile long enough to matter (a deliberately-slowed build,
      e.g. a temporary `Sleep()` inserted into `ProjectAssemblyProbe`'s own
      build step for testing purposes only, removed before committing) —
      cross-checked against a continuously-polled `GET .../status` to
      confirm the phase is genuinely still `"Compiling"`, not silently
      hung. Repeat this SAME deliberately-slowed-build check with at least
      one Editor panel torn out into its own ImGui platform window (drag any
      docked panel outside the main OS window first, per
      `ImGuiConfigFlags_ViewportsEnable` - see the message-pump's own doc
      comment, Section 3.2) at the moment the freeze starts — confirm no
      crash/visible corruption on either window, and that the torn-off
      window is ALSO not marked "Not Responding" during the freeze.
- [ ] A live test where `RestoreProjectAssemblyBinariesFromBackup()` is made
      to fail (e.g. delete/rename the `.hotreload_backup/<name>_Game.dll.bak`
      file out from under it just before triggering a hot reload with a
      deliberately-broken source change) confirms `lastOutcome ==
      "CriticalFailure"` with `lastErrorMessage` mentioning the restore
      failure specifically — never a false `"RolledBack"`/`"Success"`.
- [ ] The shared in-flight guard is confirmed to also reject a hot-reload
      request for a project whose OWN prior async build (button OR
      `compile_only`) is still running, and vice versa.
- [ ] Incremental build succeeds (`gte_core`, `gte_editor`,
      `GreatTamanaEditor`, `GreatTamanaEngineTests`), zero new warnings.
      Full build/full `ctest` are PHASE5's job, not this phase's.
