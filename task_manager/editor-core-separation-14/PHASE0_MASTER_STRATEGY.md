# editor-core-separation-14 — PHASE0 MASTER STRATEGY
## Project Assembly Hot Reload — BIG-STEP 3 of 4: Synchronous Compile & Atomic Swap Orchestrator

Parent external plan (read this whole 5-file series once, in order, before
touching any code):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-2\HOTRELOAD_BIGSTEP_00_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
through `..._03_SYNCHRONOUS_COMPILE_AND_ATOMIC_SWAP_ORCHESTRATOR_2026-09-28.txt`.

Prior campaigns this one builds directly on top of (both DONE, both verified
against the real repo before this file was written):
- `task_manager/editor-core-separation-12/` — BIG-STEP 1 (Live Debug + Compile/Reload Trigger Surface).
- `task_manager/editor-core-separation-13/` — BIG-STEP 2 (Teardown Safety & Registration Ledger).

This campaign (`editor-core-separation-14`) implements ONLY BIG-STEP 3.
BIG-STEP 4 (state snapshot/restore) is explicitly future work — this
campaign only leaves two clearly-named hook points for it.

---

## STEP 1 — THE GOAL (where are we going?)

At the end of this campaign, an external caller (a human clicking a future
"Compile & Reload" button, or an AI/script issuing
`POST /project_assembly/hot_reload?name=<X>`) can:

1. Trigger, for exactly ONE named, already-loaded Project Assembly, a full,
   synchronous cycle: **freeze the whole engine -> back up the current
   `_Game.dll`/`_Editor.dll` -> unload them cleanly (zero dangling
   pointers) -> recompile -> on success, load the freshly-built pair; on
   any failure, restore the backup and reload the OLD, still-good pair ->
   unfreeze** — with the whole engine main loop genuinely blocked for the
   entire duration (a real, literal `while` loop freeze, not merely "the UI
   looks busy").
2. Poll `GET /project_assembly/hot_reload/status` from a SECOND connection
   while the first, triggering request is still blocked, and see `phase`
   genuinely advance through every real phase name in order, ending at
   `lastOutcome` = `"Success"`, `"RolledBack"`, or (only in the
   already-documented, low-probability "failure of failure" case)
   `"CriticalFailure"`.
3. Get this behavior with **zero change to `GreatTamanaEditor.exe`'s own
   compiled-in `gte_core`/`gte_editor`** — only the targeted project's own
   two `.dll`s are ever touched (LDD-HR2).

State capture/restore across the reload (BIG-STEP 4) is explicitly OUT of
this campaign's scope — see "Non-Goals" below. This campaign proves the
mechanism with two named, literal no-op stub hook points a future campaign
fills in.

---

## STEP 2 — THE SITUATION (where are we now?)

Everything below was confirmed by directly reading the real, current
source tree during this strategy's own preparation (never guessed, never
taken only from the external plan's own prose) — every phase file below
cites its own anchors again at the point it needs them, since a fast-moving
codebase drifts.

### 2.1 — What already exists and works (confirmed, reused as-is)

- `gte::ProjectAssemblyHost` (`src/Core/Plugins/ProjectAssemblyHost.h/.cpp`)
  — **owned as a plain member of `Core`** (`Core::m_projectAssemblyHost`,
  `Core.h` line 624), reached via `Core::GetProjectAssemblyHost() noexcept`
  (`Core.h` line 351). It already has `UnloadProjectAssembly(projectName,
  core, renderer)` (does `renderer.WaitForGpuIdle()` ->
  `ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor()`
  -> `FreeLibrary()` Editor-then-Game, in that exact order — confirmed,
  `ProjectAssemblyHost.cpp` lines 167-206) and
  `GetLoadedAssemblyFileNames()`. Its private `TryLoadOneAssembly(dllPath,
  core, editorHost)` (lines 90-161) already does everything a fresh load
  needs (suffix-based `_Game`/`_Editor` dispatch, `GTE_RegisterProject`
  resolution, `ProjectAssemblyRegistrationLedger` bracketing) — it is
  simply `private` and returns `void` today.
- `gte::ProjectAssemblyRegistrationLedger` (`src/Core/Plugins/
  ProjectAssemblyRegistrationLedger.h/.cpp`) — `BeginRecordingFor`/
  `EndRecording`/`RecordX()`/`UnregisterEverythingFor()`/`PeekEntry()`, all
  real, all working (confirmed live in `editor-core-separation-13`'s own
  13-check isolation test).
- `gte::ComponentTypeRegistry::UnregisterDescriptor()` and
  `gte::EditorPanelRegistry::UnregisterPluginPanel()` — both real, both
  wired into the ledger's own `UnregisterEverythingFor()`.
- `bool TriggerProjectAssemblyCompile(projectName, buildDirectory)`
  (`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`) — the existing,
  ASYNCHRONOUS compile path: spawns a background thread
  (`RunBuildThreadBody`), builds `<name>_Game` then (if that succeeds)
  `<name>_Editor` (a missing `_Editor` target is treated as success, not
  failure), streams output into `GTE_LOG_*` via `RunOneBuildTarget()`'s own
  blocking `ReadFile()` loop, guarded by a file-local
  `g_inFlightMutex`/`g_inFlightProjects`/`TryMarkInFlight()`/
  `ClearInFlight()` (anonymous namespace, `ProjectAssemblyBuildRunner.cpp`
  lines 30-47).
- `BackupProjectAssemblyBinaries(projectName, outputDirectory)` /
  `RestoreProjectAssemblyBinariesFromBackup(projectName, outputDirectory)`
  (same file) — real, file-copy-based, already tested in isolation.
- `ProjectAssemblyHotReloadDebugStatus` (Meyers singleton,
  `Set()`/`Finish()`/`GetSnapshot()`) and `IHotReloadDebugCapability`
  (`Core/EditorCapabilities.h` line 173) with its real, gte_editor-tier
  implementation `EditorHotReloadDebugCapability` (`src/Editor/
  EditorHotReloadDebugCapability.h/.cpp`) — `GetHotReloadStatus()`,
  `GetLedgerEntry()`, `GetLoadedAssemblyFileNames()`,
  `GetRegisteredComponentTypeNames()`, `BuildSceneSnapshotJson()`,
  `TriggerCompileOnly()` are all REAL today. `TriggerHotReload()`'s body is
  the ONE remaining permanent-until-now placeholder: `return false;`
  (`EditorHotReloadDebugCapability.cpp` lines 104-112).
- `POST /project_assembly/hot_reload?name=<X>` (`NetworkServer.cpp` lines
  1345-1367) — a STABLE, AGREED route contract, currently always answering
  `501` because `TriggerHotReload()` always returns `false`. This
  campaign's whole job on the "trigger" side is making this real, WITHOUT
  changing its method/path/query-param contract.
- `Renderer::WaitForGpuIdle()` (`src/Renderer/Renderer.h` line 354) — a
  full, blocking `vkDeviceWaitIdle()`, already a sanctioned
  `UnloadProjectAssembly()` caller.
- `EditorHost::Run()`'s main loop (`src/Editor/EditorHost.cpp`) — a single
  `while (running)` loop on one thread; the existing
  `m_commandBridge.TryPeekPendingCommandRequest()` drain point (lines
  438-443) is the established, proven pattern for "network thread wants
  something done on the main thread, synchronously, once per frame" this
  campaign's own new drain point mirrors exactly.
- `gte::GetHotReloadEngineStateMutex()` (`src/Core/Plugins/
  HotReloadEngineStateMutex.h`) — already locked by every
  `EditorHotReloadDebugCapability` OBSERVE method around its own read. Its
  own header comment states, IN WRITING: *"A FUTURE BIG-STEP 2/3 CAMPAIGN
  MUST lock this same mutex around every ...
  ProjectAssemblyHost load/unload call it makes during a reload cycle."*

### 2.2 — What is missing (this campaign's real scope)

1. `ProjectAssemblyHost` has no PUBLIC way to load one exact `.dll` path on
   demand (only the private, startup-only `TryLoadOneAssembly`, and only a
   directory-scanning public entry point). PHASE1.
2. Nothing shares `RunOneBuildTarget`'s process-spawn/pipe-read machinery
   with a genuinely SYNCHRONOUS (blocking-the-caller) call shape; its own
   read loop has no periodic tick anywhere a Windows message pump could
   hook into. PHASE2.
3. No cross-thread request slot exists for "a hot-reload was requested for
   project X, run it on the main thread and block the HTTP caller until
   it's done" — `EngineCommandBridge` exists but is documented as
   deliberately the wrong shape for a multi-minute, blocking operation.
   PHASE3.
4. No orchestrator function ties any of the above together, no main-loop
   drain point exists yet, and `TriggerHotReload()`/the `POST
   /project_assembly/hot_reload` handler are still permanent placeholders.
   PHASE4.
5. **Genuine, previously-undocumented gap this strategy's own review
   found** (not in the external plan): `ProjectAssemblyHost::
   UnloadProjectAssembly()` and `TryLoadOneAssembly()` do **not** lock
   `GetHotReloadEngineStateMutex()` today (confirmed by direct reading of
   `ProjectAssemblyHost.cpp` — zero mutex usage anywhere in that file)
   despite that mutex's own header comment explicitly requiring it. This
   campaign inherits and must close this obligation (PHASE1), or a
   network-thread `GET /project_assembly/debug/ledger`/
   `loaded_assemblies`/`component_types` call can race a live unload/reload
    with no lock protecting either side.
6. **Second genuine, previously-undocumented gap this strategy's own review
   found** (also not in the external plan): the external plan's own
   pseudocode resolves `gte::ExecutableDirectory()` (defined only in
   `src/Editor/ProjectRootPath.cpp`, compiled ONLY into `gte_editor`)
   directly inside `PerformProjectAssemblyHotReload()`'s own body — but that
   function's home, `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp`, sits
   in the exact same folder as every OTHER `gte_core`-tier file this
   campaign touches (`ProjectAssemblyHost.h/.cpp`,
   `ProjectAssemblyBuildRunner.h/.cpp`, ...) and would therefore be added to
   `gte_core`'s own CMake source list, never `gte_editor`'s — a direct
   `gte_core` -> `gte_editor` dependency, forbidden by `AGENTS.md`'s own
   Clean Architecture rule and exactly the layering hazard
   `ProjectAssemblyBuildRunner.h`'s OWN existing
   `ResolveProjectAssemblyOutputDirectory()`/`ResolveCMakeBuildDirectory()`
   functions were deliberately designed to avoid (both take their starting
   directory as an explicit, caller-resolved parameter for exactly this
   reason — confirmed, that file's own header comments). PHASE3/PHASE4 fix
   this by having `EditorHost::Run()`'s own drain point (gte_editor-tier,
   already calls `gte::ExecutableDirectory()` for its own
   `LoadProjectAssemblies()` call) resolve both directories and pass them
   into `PerformProjectAssemblyHotReload()` as two new, explicit
   `std::filesystem::path` parameters — the orchestrator itself never calls
   `gte::ExecutableDirectory()`. Left uncaught, this would have compiled
   fine in every ordinary `cmake --build build` (both libraries always link
   together into `GreatTamanaEditor`) and shipped completely undetected,
   since the one CI probe built specifically to catch this exact class of
   violation, `tools/ci/gte_core_player_link_probe/`, is its own separate,
   manually-invoked CMake project — never `add_subdirectory()`'d from the
   root `CMakeLists.txt` — so it would never have run as part of this
   campaign's own PHASE1-5 build/test steps at all.

### 2.3 — Two corrections to the external plan's own pseudocode (BIG-STEP 3
file, Section "STEP 4"), found by this strategy's own direct source review

- **`ProjectAssemblyHost::Instance()` does not exist.** `ProjectAssemblyHost`
  is a plain, non-singleton member of `Core` (`Core::m_projectAssemblyHost`).
  Every phase file below uses the real accessor,
  `core.GetProjectAssemblyHost()`, everywhere the external doc wrote
  `ProjectAssemblyHost::Instance()`.
- **The message-pump sketch cannot simply wrap the EXISTING blocking
  `ReadFile()` call** — that call has no timeout and can block for an
  arbitrarily long stretch between build-output lines, defeating a "pump
  every ~100ms" intent. PHASE2 replaces it with a `PeekNamedPipe()`-driven
  poll loop so an injected idle-tick callback runs on a genuinely fixed
  cadence, independent of how chatty the child build process is.

---

## STEP 3 — THE PLAN (how do we get there?)

Five phases, in this exact dependency order (never reorder — each phase's
own file states its own "Depends on" line again):

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_PROJECT_ASSEMBLY_HOST_EXACT_PATH_LOADERS_AND_MUTEX_CLOSURE.md` | `ProjectAssemblyHost` gains public `LoadOneProjectAssemblyFromExactPath()`/`...IfExists()`; `TryLoadOneAssembly` returns `bool`; `GetHotReloadEngineStateMutex()` obligation closed. |
| 2 | `PHASE2_SHARED_SYNCHRONOUS_BUILD_HELPER_AND_MESSAGE_PUMP.md` | Extracts a shared `RunProjectAssemblyBuildAndWait()`/`BuildOutcome`; adds `TryRunProjectAssemblyBuildSynchronously()`; read loop becomes poll-based with an optional idle-tick callback. |
| 3 | `PHASE3_HOT_RELOAD_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md` | New `ProjectAssemblyHotReloadCommandBridge`; new `EditorHost` member + `Run()` drain point; new `ProjectAssemblyHotReload.h/.cpp` with a REAL-BUT-TEMPORARY `PerformProjectAssemblyHotReload()` body so the whole chain compiles and is smoke-testable this phase. |
| 4 | `PHASE4_ORCHESTRATOR_AND_ROUTE_WIRING.md` | Replaces PHASE3's temporary body with the full, ordered pause->backup->unload->compile->reload-or-rollback->unpause sequence; wires `HotReloadEngineStateMutex`; wires `ProjectAssemblyHotReloadDebugStatus` transitions; wires the two BIG-STEP-4 hook-point stubs; replaces `TriggerHotReload()`'s permanent placeholder and the `POST /project_assembly/hot_reload` route handler with the real thing. |
| 5 | `PHASE5_PROBE_FIXTURE_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` | Extends `Projects/ProjectAssemblyProbe/` for a real rollback test; full live verification (rollback path + success path); full build + full `ctest`; completion report; `AGENTS.md` update. |

Each phase file below is self-contained: Goal / Situation / Plan (concrete
code, exact file paths, exact function signatures) / Definition of Done.
Every phase performs REAL code edits — no phase is "read-only research" or
"we will not apply any code fix yet."

### Non-Goals (restated, so no phase "helpfully" expands scope)

- Does NOT implement BIG-STEP 4 (ECS state snapshot/restore) — HOOK POINT
  A/B (PHASE4) stay literal no-op stubs, clearly named for a future
  campaign to fill in.
- Does NOT reload `gte_core`/`gte_editor` themselves (LDD-HR2).
- Does NOT support more than one targeted project per cycle, and does not
  touch any OTHER simultaneously-loaded Project Assembly (LDD-HR5).
- Does NOT decide the permanent ImGui "Compile & Reload" button's
  placement — only the underlying HTTP-reachable mechanism.
- Does NOT change `IHotReloadDebugCapability`'s own method signatures —
  every phase reuses the EXISTING `bool TriggerHotReload(name)` /
  `Status GetHotReloadStatus()` shape (see PHASE4 for exactly how a rich,
  final-outcome HTTP response is built from these two unchanged methods) (LDD-HR3).

### Locked Design Decisions (cited as "LDD-HRn" throughout every phase file)

These are the concrete, permanent decisions every phase file's own "LDD-HRn"
citations refer back to — collected here in ONE place so a citation is never
left dangling with no definition to find (numbered to match the external
master plan's own five decisions; there is no LDD-HR1/HR3 gap in the source
material, they are simply cited less often below):

- **LDD-HR1** — State capture/restore (BIG-STEP 4) is explicitly OUT of this
  campaign's scope. HOOK POINT A (`CaptureProjectAssemblyHotReloadState()`)
  and HOOK POINT B (`RestoreProjectAssemblyHotReloadState()`), both PHASE4,
  are permanent, literal, logged no-op stubs for this whole campaign's
  lifetime — never partially implemented "just this one field" as a shortcut.
- **LDD-HR2** — `gte_core`/`gte_editor` themselves are NEVER reloaded or
  recompiled by this feature — only the two `.dll`s of the ONE targeted
  Project Assembly (`<Name>_Game.dll`/`<Name>_Editor.dll`) are ever unloaded,
  recompiled, or reloaded.
- **LDD-HR3** — `IHotReloadDebugCapability`'s own method signatures never
  change across this whole campaign — every phase reuses the EXISTING `bool
  TriggerHotReload(name)` / `Status GetHotReloadStatus()` shape verbatim.
- **LDD-HR4** — The whole engine main loop is genuinely, synchronously
  frozen for the entire duration of one hot-reload cycle — a real, literal
  blocking call on the main thread (`EditorHost::Run()`'s own drain point
  calling `PerformProjectAssemblyHotReload()` synchronously), never merely
  "the UI looks busy while something happens in the background."
- **LDD-HR5** — Exactly ONE named Project Assembly is targeted per
  hot-reload cycle; no other simultaneously-loaded Project Assembly is ever
  touched, unloaded, or reloaded as a side effect.

### Reporting convention

After each phase, write `PHASEn_COMPLETION_REPORT.md` in this same folder:
what was actually built, any confirmed deviation from that phase's own file
(and why), any NEW gap found the way Section 2.2 item 5 above was found.
After PHASE5, write one `CAMPAIGN_COMPLETION_REPORT.md` summarizing all five,
mirroring `editor-core-separation-13/CAMPAIGN_COMPLETION_REPORT.md`'s own
shape exactly.
