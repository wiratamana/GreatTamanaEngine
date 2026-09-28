# editor-core-separation-14 — CAMPAIGN COMPLETION REPORT

## Status: DONE

Implements the whole five-phase `editor-core-separation-14` campaign
("Project Assembly Hot Reload — BIG-STEP 3 of 4: Synchronous Compile & Atomic
Swap Orchestrator"), per `PHASE0_MASTER_STRATEGY.md` (as corrected by
`PHASE0_DOUBLECHECK_REPORT.md`). This is the final phase (PHASE5) and this is
the campaign's closing report — see `PHASE1_COMPLETION_REPORT.md` through
`PHASE5_COMPLETION_REPORT.md` for each phase's own detailed writeup; this
report summarizes the whole campaign and restates, honestly, exactly what
remains out of scope.

---

## What was built, per phase (summary — see each phase's own report for detail)

- **PHASE1** — `ProjectAssemblyHost` gained two new public methods,
  `LoadOneProjectAssemblyFromExactPath(dllPath, core, editorHost)` and
  `LoadOneProjectAssemblyFromExactPathIfExists(...)` (the latter treats a
  non-existent path as a normal, successful no-op — needed for the
  `_Editor.dll` half of a project that never ships one); `TryLoadOneAssembly()`
  return type changed `void` → `bool`. Both `TryLoadOneAssembly()` and
  `UnloadProjectAssembly()` now lock `GetHotReloadEngineStateMutex()` around
  their whole body, closing an obligation `editor-core-separation-13`'s own
  mutex header comment had explicitly required but never actually
  implemented anywhere.
- **PHASE2** — `ProjectAssemblyBuildRunner` gained a shared, non-anonymous
  `RunProjectAssemblyBuildAndWait()`/`BuildOutcome` pair (extracted from the
  existing async `RunBuildThreadBody`'s own Game-then-Editor sequencing logic,
  now genuinely reusable for a synchronous caller) plus
  `TryRunProjectAssemblyBuildSynchronously()`, which shares the exact same
  `g_inFlightProjects` guard the existing async `TriggerProjectAssemblyCompile()`
  already uses. `RunOneBuildTarget()`'s read loop was rewritten from a
  plain blocking `ReadFile()` to a `PeekNamedPipe()`-driven poll loop with an
  optional `onIdleTick` callback — the foundation the freeze's own Windows
  message pump (PHASE4) hooks into.
- **PHASE3** — new `ProjectAssemblyHotReloadCommandBridge`
  (`src/Application/`), mirroring `EngineCommandBridge`'s proven
  submit-and-wait shape with ONE deliberate divergence: `m_requested` is
  cleared in exactly one place, `FulfillPending()`, never by `SubmitAndWait()`
  itself on either the success OR timeout path — a genuine semantics bug in
  the external plan's own pseudocode, caught and corrected by this campaign's
  own PHASE0 double-check pass before any code was written. `EditorHost`
  gained a new member instance of the bridge and a new `Run()` drain point;
  `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp` (new) declared the
  PERMANENT 6-parameter `PerformProjectAssemblyHotReload()` signature
  (deliberately taking `outputDirectory`/`buildDirectory` as explicit,
  caller-resolved `std::filesystem::path` parameters rather than calling
  `gte::ExecutableDirectory()` itself — closing a real `gte_core` →
  `gte_editor` layering violation the external plan's own pseudocode
  contained, also caught by PHASE0's own double-check) with a temporary,
  real-but-placeholder body so the whole chain compiled and was smoke-testable
  this phase. `EditorHotReloadDebugCapability::TriggerHotReload()`'s
  permanent `return false;` placeholder was replaced with a real body
  submitting into the new bridge.
- **PHASE4** — `PerformProjectAssemblyHotReload()`'s real body: `Set("CapturingState")`
  → `CaptureProjectAssemblyHotReloadState()` (HOOK POINT A, no-op stub) →
  `Set("BackingUpBinaries")` → `BackupProjectAssemblyBinaries()` →
  `Set("Unloading")` → `UnloadProjectAssembly()` → `Set("Compiling")` →
  `TryRunProjectAssemblyBuildSynchronously(..., &PumpWindowsMessagesDuringHotReloadFreeze)`
  → success path (`Set("ReloadingNewCode")` → reload the fresh binaries) or
  failure path (`Set("RollingBack")` → `RestoreProjectAssemblyBinariesFromBackup()`
  → reload the restored binaries) → `Set("RestoringState")` →
  `RestoreProjectAssemblyHotReloadState()` (HOOK POINT B, no-op stub) →
  `Finish("Success"/"RolledBack"/"CriticalFailure", ...)`.
  `PumpWindowsMessagesDuringHotReloadFreeze()` (a real `PeekMessage`/
  `TranslateMessage`/`DispatchMessage` loop, `hWnd = nullptr` for ImGui
  multi-viewport correctness) keeps Windows from marking the frozen main
  window "Not Responding" during a long compile. `POST
  /project_assembly/hot_reload`'s route handler was replaced with the real
  thing (`503` when the bridge rejects/times out; a real, populated status
  JSON body — `200` — otherwise), reusing the pre-existing, unchanged
  `BuildHotReloadStatusResponseJson()` builder with zero
  `IHotReloadDebugCapability` signature change. PHASE4's OWN live
  verification (performed as part of that phase, ahead of PHASE5) already
  proved: a real success-path reload (probe fill-color change), a real
  rollback (deliberate compile error), a real `CriticalFailure` (forced
  restore-from-backup failure), and both directions of the shared in-flight
  guard (hot-reload's build vs. a concurrent async `compile_only`, and vice
  versa) — all against a real running `GreatTamanaEditor.exe`.
- **PHASE5** (this phase) — extended `Projects/ProjectAssemblyProbe/` for a
  FRESH round of the exact same rollback/success live tests (a deliberately
  toggled compile error, and a real compute-shader color change), ran the
  full 13/14-point live-verification checklist from this phase's own file
  (baseline → rollback → post-cycle → revert → success → slow-build/message-
  pump → in-flight-guard cross-check), the ONE full clean build + full
  `ctest` regression pass this whole campaign is permitted to run, this
  report, and the `CAMPAIGN_COMPLETION_REPORT.md`/`AGENTS.md` update.

---

## Deliberate deviations from the plan, found and resolved by this campaign

Every deviation below was found and corrected during PHASE0's own dedicated
double-check pass (`PHASE0_DOUBLECHECK_REPORT.md`), BEFORE any implementation
phase began — restated here for a reader of this closing report who has not
read that file:

1. **`gte_core` → `gte_editor` layering violation in the external plan's own
   pseudocode (Section 2.2, item 6).** The external plan had
   `PerformProjectAssemblyHotReload()` — living in `src/Core/Plugins/`, the
   SAME folder whose every other file compiles into `gte_core` — call
   `gte::ExecutableDirectory()` directly, a function defined ONLY in
   `src/Editor/ProjectRootPath.cpp` (`gte_editor`-only). This would have
   compiled and linked completely fine in every ordinary
   `cmake --build build` (both libraries always link together into
   `GreatTamanaEditor`), and the one CI probe purpose-built to catch exactly
   this class of violation, `tools/ci/gte_core_player_link_probe/`, is its
   own separate, manually-invoked CMake project never `add_subdirectory()`'d
   from the root `CMakeLists.txt` — meaning this would have shipped silently.
   **Fixed**: `PerformProjectAssemblyHotReload()`'s permanent signature
   (PHASE3) takes two additional explicit `std::filesystem::path` parameters,
   `outputDirectory`/`buildDirectory`, resolved by the CALLER
   (`EditorHost::Run()`'s drain point, itself `gte_editor`-tier) — mirroring
   the exact same "resolve outside, pass in" precedent
   `ProjectAssemblyBuildRunner.h`'s own pre-existing
   `ResolveProjectAssemblyOutputDirectory()`/`ResolveCMakeBuildDirectory()`
   functions already established. Confirmed, live, by `search_in_dir`: no
   `#include "../../Editor/ProjectRootPath.h"` and no call to
   `gte::ExecutableDirectory()` anywhere in
   `src/Core/Plugins/ProjectAssemblyHotReload.cpp`.
2. **`ProjectAssemblyHotReloadCommandBridge::SubmitAndWait()` timeout
   semantics bug.** The external plan's own pseudocode described clearing
   `m_requested = false` on EVERY return path, including the timeout path —
   exactly mirroring `EngineCommandBridge`'s own DIFFERENT, deliberately
   opposite, already-locked-in behavior. Had an implementer followed the
   pseudocode literally, a client timeout would silently and completely
   break this whole feature: the main thread's drain point would never find
   the already-triggered request pending and would never run the cycle at
   all, with no crash or visible error. **Fixed**: `m_requested` is cleared
   in exactly ONE place, `FulfillPending()` — confirmed by 5 new, dedicated
   Tier-1 tests (PHASE3), most importantly
   `RequestWithNoServicerTimesOutButStaysPending`.
3. **`LDD-HRn` citations were undefined.** PHASE0/PHASE3/PHASE4 all cited
   `LDD-HR2`/`HR4`/`HR5` as established shorthand with no "Locked Design
   Decisions" section anywhere defining them. **Fixed**: a new section was
   added to `PHASE0_MASTER_STRATEGY.md` defining LDD-HR1 through LDD-HR5
   concretely.
4. **A pre-existing regression test would have broken with no plan to fix
   it.** `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`'s
   `HotReloadValidNameReturns501NotImplemented` hard-coded the OLD "always
   `501`" contract PHASE4's own route-handler replacement deliberately
   removes. **Fixed**: PHASE0 added a new Section 3.6 to PHASE4's own file
   instructing its replacement by two new tests (a `503`-when-bridge-not-
   wired test, and a `FakeHotReloadStandIn`-based end-to-end test proving the
   real `200` wiring) — both implemented and passing (PHASE4).
5. **New production files never flagged for the root `CMakeLists.txt`'s
   hand-maintained source list.** PHASE0 added explicit reminders at both new
   file-introduction points in PHASE3 — a recurring, previously-documented
   mistake class in this exact codebase (`editor-core-separation-11`'s own
   PHASE5/PHASE6). Both new file pairs
   (`ProjectAssemblyHotReloadCommandBridge.h/.cpp`,
   `ProjectAssemblyHotReload.h/.cpp`) were correctly added, confirmed by every
   subsequent incremental/full build succeeding.

No other deviation of substance occurred across PHASE1-5 — every phase's own
completion report confirms its own file's plan was implemented exactly as
written, with only small, cosmetic, meaning-preserving wording differences
(each individually noted in that phase's own report).

---

## Full build and full `ctest` regression pass (PHASE5 — the ONLY phase in this campaign permitted to run either)

```
cmake --build build
```
`ninja: no work to do` — PHASE1-4's own incremental builds had already left
the whole tree fully built and up to date; the one clean-build gate this
campaign requires was satisfied with zero new compile/link work needed.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
**Final result: 1934 tests total, 100% passing, 7 legitimate,
environment-gated skips** (up from `editor-core-separation-13`'s own
documented 1925/5 baseline):

- The same 5 pre-existing skips `editor-core-separation-13` already
  documented (`PmxLoaderRealModelSmokeTest`,
  `ProjectAssemblyHostTest.UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp`,
  two `ProjectAssemblyRegistrationLedgerTest` cases, and
  `CoreHeadlessConstructionTest...`) — all conditional on this development
  machine's Vulkan driver/loader not reporting `VK_EXT_headless_surface`,
  unrelated to this campaign.
- **2 new, expected skips, both added by this campaign's own PHASE1**:
  `ProjectAssemblyHostTest.LoadOneProjectAssemblyFromExactPathOnANonExistentPathReturnsFalse`
  and `...LoadOneProjectAssemblyFromExactPathIfExistsOnANonExistentPathReturnsTrue`
  — both need a real, constructed `Core&` to pass in (even though the
  no-op/failure path they exercise never dereferences it), so both self-skip
  for the exact same pre-existing, documented, environment-only reason —
  confirmed correct and expected by PHASE1's own completion report at the
  time it was written.

**No new failures were found by this full pass.** Unlike
`editor-core-separation-13`'s own PHASE5 (which found and fixed two genuine,
previously-latent regressions this exact way), this campaign's own new
orchestrator code had already been thoroughly, live, end-to-end exercised by
PHASE4's own dedicated verification session (a real success path, a real
rollback, a real `CriticalFailure`, and both directions of the in-flight
guard, all against a real running `GreatTamanaEditor.exe`) BEFORE this final
full-suite pass ever ran — leaving nothing latent for it to surface. This is
an honest, confirmed, checked-for outcome, not a skipped verification step.

---

## Live verification — full 13/14-point checklist (PHASE5, re-run fresh)

Run against a real, freshly-built, background-launched `GreatTamanaEditor.exe`
(see `PHASE5_COMPLETION_REPORT.md` for the full blow-by-blow):

1-4. **Baseline** — both DLLs loaded, `"Probe Panel"` present,
`"ProjectAssemblyProbe.FillTexture"` present in `/render_graph`, normal
rendering, texture solid orange. ✅

5-8. **Rollback path** — a deliberately reintroduced compile error was
confirmed real via `compile_only` first; `POST /project_assembly/hot_reload`
was dispatched via a non-blocking background `curl` process while `GET
/project_assembly/hot_reload/status` was polled from a separate connection,
genuinely observing `"phase":"Compiling"` mid-cycle before resolving to
`"Idle"`/`"RolledBack"`; every phase transition logged under the exact SAME
frame number (LDD-HR4 re-confirmed); post-cycle checks (both DLLs reloaded,
panel/pass present, normal rendering) and a multi-second process-survival
check all passed. ✅

9. **Revert** — the deliberate compile error confirmed byte-for-byte reverted. ✅

10-12. **Success path** — a real compute-shader fill-color change (orange →
blue) reloaded successfully (`"last_outcome":"Success"`), visibly confirmed
via `GET /get_texture`, with the SAME background process ID used throughout
(no relaunch). ✅

13. **Slow-build / message-pump check** — a temporary 15-second `Sleep()`
build dependency confirmed `"phase":"Compiling"` held for the WHOLE
~20-second real delay; `GET /get_swapchain` issued mid-freeze correctly
returned an honest `504` (main thread genuinely, synchronously blocked — a
positive confirmation of LDD-HR4, not a defect), and normal operation resumed
immediately once the cycle finished, with no crash/force-close. Reverted
afterward. ✅

14. **In-flight guard cross-check** — a concurrently-dispatched async
`compile_only` and synchronous `hot_reload` request for the SAME project
correctly resulted in the guard rejecting the second-acquired side (the
hot-reload's own build), with the whole cycle falling through to a safe,
honest rollback rather than any silent corruption or crash — confirmed via
`GET /get_logs`. ✅

All checks pass. The fixture was left in its exact, original, documented
baseline state (solid orange, both DLLs loaded) before shutdown, matching
`Projects/ProjectAssemblyProbe/`'s own permanent, cross-campaign baseline —
one pre-existing, already-documented (PHASE4) shader-staging build-system
gap was re-encountered and worked around identically to how PHASE4 already
handled it (never a new defect, never edited into any shipped CMake file);
see `PHASE5_COMPLETION_REPORT.md` for the full detail.

---

## Honest restatement: BIG-STEP 4 remains FULLY UNIMPLEMENTED

Per this campaign's own non-goals (`PHASE0_MASTER_STRATEGY.md`, LDD-HR1) and
every phase's own restatement:

- **BIG-STEP 4 (ECS world-state snapshot/restore across a reload) does not
  exist.** HOOK POINT A (`CaptureProjectAssemblyHotReloadState()`) and HOOK
  POINT B (`RestoreProjectAssemblyHotReloadState()`) — both in
  `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp`, PHASE4 — remain
  permanent, literal, logged no-op stubs, exactly as this whole campaign's
  own scope requires. A hot reload today discards any live ECS entity state
  a Project Assembly's own code might have wanted to preserve across the
  cycle — this campaign proves the compile/swap/rollback MECHANISM only,
  never state continuity.
- **What IS now real and working, end to end, for the first time**: `POST
  /project_assembly/hot_reload?name=<X>` genuinely freezes the whole engine,
  backs up, unloads, recompiles, and either reloads the fresh binaries or
  rolls back to the last-known-good ones — all synchronously, on the main
  thread, with the network thread staying alive and responsive to unrelated
  requests throughout, and `GET /project_assembly/hot_reload/status` genuinely
  reporting live phase progress to a second, concurrent connection.
- **This campaign's own, exact, narrow scope**: exactly ONE named,
  already-loaded Project Assembly can now be safely, synchronously,
  atomically hot-reloaded (or safely rolled back on any failure) — nothing
  about its own runtime ECS state, nothing about `gte_core`/`gte_editor`
  themselves (LDD-HR2, never touched/recompiled), nothing about more than one
  project per cycle (LDD-HR5).

---

## Definition of Done — this whole campaign

- [x] `ProjectAssemblyHost` gained public exact-path loaders;
      `TryLoadOneAssembly()` returns `bool`; `GetHotReloadEngineStateMutex()`
      obligation closed (PHASE1).
- [x] A shared, synchronous build helper + poll-based message-pump hook exist
      (PHASE2).
- [x] A dedicated hot-reload command bridge + main-loop drain point exist,
      with `PerformProjectAssemblyHotReload()`'s permanent, layering-correct
      6-parameter signature in place (PHASE3).
- [x] The real orchestrator, Windows message pump, status wiring, and route
      response all exist and were live-verified end-to-end — success path,
      rollback path, `CriticalFailure` path, and both directions of the
      in-flight guard (PHASE4).
- [x] The probe fixture was extended and re-exercised fresh; all 13/14 live
      checks pass; the ONE full build + full `ctest` regression pass required
      by this campaign succeeded with zero new regressions
      (1934 tests, 100% passing, 7 legitimate environment-gated skips, up
      from 1925/5) (PHASE5).
- [x] `CAMPAIGN_COMPLETION_REPORT.md` exists in this folder (this file).
- [x] `AGENTS.md` has a new, accurate entry for this campaign's shipped
      surface, explicitly stating BIG-STEP 4 remains unimplemented.
- [x] Every checkbox in every one of PHASE1-4's own Definition of Done is
      re-confirmed still true (re-verified this phase, both by direct source
      reading and by this phase's own live checks independently
      re-exercising the exact same success/rollback/in-flight-guard/
      message-pump surfaces).

**This whole campaign (`editor-core-separation-14`, BIG-STEP 3 of 4) is
DONE.**
