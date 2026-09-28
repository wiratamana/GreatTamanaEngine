# editor-core-separation-13 — PHASE0: MASTER STRATEGY
## Project Assembly Hot Reload — BIG-STEP 2 of 4: Teardown Safety & Registration Ledger

Parent plan (external, read-only, DO NOT EDIT):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-2\HOTRELOAD_BIGSTEP_00_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
(read this first, every time — it defines LDD-HR1..5 and Hazards 1-4, and this
whole campaign assumes them as already-established ground truth)

Immediate spec for THIS campaign (external, read-only, DO NOT EDIT):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-2\HOTRELOAD_BIGSTEP_02_TEARDOWN_SAFETY_AND_REGISTRATION_LEDGER_2026-09-28.txt`

Prior, COMPLETED campaign this one builds directly on top of (BIG-STEP 1):
`task_manager/editor-core-separation-12/` — read `CAMPAIGN_COMPLETION_REPORT.md`
in full before starting PHASE1. It shipped `IHotReloadDebugCapability`, seven
live HTTP routes, `ProjectAssemblyHotReloadDebugStatus`, and
`HotReloadEngineStateMutex` — all real, all live, all reusable as-is. **BIG-STEP
2/3/4 were confirmed FULLY UNIMPLEMENTED at that campaign's close.** This
campaign implements BIG-STEP 2 only.

---

## Task Status

- PHASE1_COMPONENT_TYPE_REGISTRY_UNREGISTER_HAZARD1.md
- PHASE2_EDITOR_PANEL_REGISTRY_UNREGISTER_HAZARD2.md
- PHASE3_REGISTRATION_LEDGER_CLASS_AND_ENTRY_POINT_WIRING.md
- PHASE4_PROJECT_ASSEMBLY_HOST_UNLOAD_GPU_SAFETY_AND_BINARY_BACKUP.md
- PHASE5_PROBE_FIXTURE_EXTENSION_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md

Each phase file is self-contained (Goal / Situation / Plan) but assumes every
earlier phase in this list is DONE, its own `PHASEn_COMPLETION_REPORT.md`
written, and its own compile check green, before starting. Do not reorder —
PHASE3 literally cannot compile without PHASE1/2's new methods existing, and
PHASE4 literally cannot compile without PHASE3's ledger class existing.

---

## Step 1: The Goal

Give this engine a `ProjectAssemblyHost::UnloadProjectAssembly()` that a
future BIG-STEP 3 orchestrator can call, at any time, for one named,
currently-loaded Project Assembly, and have it come back having left **zero
dangling pointers anywhere in the engine** — no stale `ComponentTypeDescriptor`,
no stale `IEditorPanelModule_v1*`, no stale `RenderPassProvider`, no GPU
resource freed while a frame was still possibly in flight — and having taken a
private backup of the still-good `.dll` bytes first, so a future rollback is
physically possible. This is narrower than "hot reload works" (that is
BIG-STEP 3/4). This campaign's own success bar is exactly: **you can safely
make a currently-running Project Assembly disappear from this engine, cleanly,
and the process keeps running normally afterward, provably, for at least a
few seconds of continued operation** — verified live via BIG-STEP 1's own
seven HTTP routes, not by code review alone.

## Step 2: The Situation

Confirmed, directly, by reading the real, current source tree (not the
external plan's own paraphrase of it) before writing this file:

1. **`ComponentTypeRegistry::RegisterDescriptor()`**
   (`src/ECS/Reflection/ComponentTypeRegistry.cpp` line 31-39) `assert()`s on
   any duplicate `typeName` and has **no unregister method at all**. Confirmed
   `Projects/ProjectAssemblyProbe/` registers **zero** custom component types
   today (`search_in_dir` for `RegisterComponentType<` inside
   `Projects/ProjectAssemblyProbe/` returned nothing) — PHASE5 of this
   campaign must add one, specifically to exercise this hazard for real.

2. **`EditorPanelRegistry::RegisterPluginPanel()`**
   (`src/Core/EditorPanelRegistry.cpp` line 17-45) pushes into **two**
   vectors, not one: `m_pluginPanels` (the one holding the dangerous raw
   `IEditorPanelModule_v1*`) **and** `m_allNames` (a plain `std::string` list
   also used by `IsKnownName()`'s own **collision guard** — the same method,
   lines 34-41, that REFUSES a second registration under an already-known
   name). **This is a real, newly-found landmine the external plan's own
   BIG-STEP 2 file explicitly left as an "open question, deferred"**
   (`HOTRELOAD_BIGSTEP_02...txt`, Step 2, "Deliberately does NOT touch
   m_allNames... a real, separate design question... deferred to BIG-STEP
   3/4"). This campaign resolves it NOW, in PHASE2, not later — see PHASE2's
   own Step 2 for the full, concrete reasoning for why leaving `m_allNames`
   stale would silently break every future reload of any project with an
   Editor panel (which includes the campaign's own `ProjectAssemblyProbe`
   fixture, permanently, from BIG-STEP 3 onward). Deferring this is not an
   option — it would ship a feature that passes this campaign's own tests and
   then structurally can never re-register the exact same panel name a second
   time, ever, which is precisely what every reload after the first does.

3. **`Core::RegisterProjectRenderPassProvider()`** (`src/Core/Core.cpp` line
   337-340) is a one-line forward into
   `m_offscreenRenderPipeline.Register(...)`. `rg::RenderPipeline::Unregister()`
   (`src/Renderer/RenderGraph/RenderPipeline.h` line 500-515) **already
   exists**, already does a correct linear-scan-and-erase by `debugName`, and
   is explicitly commented "not load-bearing, nothing calls this yet" — this
   campaign is its first real caller.

4. **`Renderer::WaitForGpuIdle()`** (`src/Renderer/Renderer.h` line 345,
   body `src/Renderer/Renderer.cpp` line 218) already exists, already does a
   full blocking `vkDeviceWaitIdle()`, and its own doc comment (lines 337-344)
   currently names exactly ONE sanctioned caller family ("the rare, explicit,
   human/LLM-triggered `GET /get_texture` request path") — this campaign adds
   a second sanctioned caller and MUST update that comment (PHASE4).

5. **`ProjectAssemblyHost`** (`src/Core/Plugins/ProjectAssemblyHost.h/.cpp`)
   has `LoadProjectAssemblies()`/`TryLoadOneAssembly()` (startup-only scan),
   a private `m_loadedAssemblies` vector of `{moduleHandle, dllFileName}`, and
   a destructor that deliberately does nothing (`~ProjectAssemblyHost()`,
   `.cpp` line 15-25, "never `FreeLibrary()`'d before process exit"). **No
   unload method, no per-project accessor, exists today.**

6. **`ProjectAssemblyBuildRunner`** (`src/Core/Plugins/
   ProjectAssemblyBuildRunner.h/.cpp`) already has a working, already-fixed
   (argv-quoting bug, confirmed by `editor-core-separation-11` PHASE6),
   already-guarded (`TryMarkInFlight`/`ClearInFlight`) `TriggerProjectAssemblyCompile()`.
   It has **no backup/restore-binaries function** — PHASE4 adds one, in this
   same file family, since it is purely about files on disk, not live engine
   state.

7. **BIG-STEP 1 already shipped the exact placeholder shape this campaign
   fills in for real**: `EditorHotReloadDebugCapability::GetLedgerEntry()`
   and `::GetLoadedAssemblyFileNames()`
   (`src/Editor/EditorHotReloadDebugCapability.cpp`, current lines 21-41)
   are, TODAY, hard-coded to always return empty, with an explicit code
   comment naming "a future BIG-STEP 2 campaign" as the one that replaces
   their bodies. **This campaign is that campaign** — for THIS specific pair
   of methods, PHASE3/PHASE4 replace ONLY their bodies, never their
   signatures, and never touch `src/Core/EditorCapabilities.h`'s own
   `IHotReloadDebugCapability` interface (that abstract interface is frozen
   for this whole campaign). This does NOT forbid PHASE4 from otherwise
   extending `EditorHotReloadDebugCapability.h/.cpp` itself for an unrelated,
   necessary reason — PHASE4 legitimately adds a new private
   `ProjectAssemblyHost*` member plus a `SetProjectAssemblyHost()` setter to
   that same concrete class (never to the interface), because a live
   `ProjectAssemblyHost&` cannot reach it any other way (see PHASE4's own
   Step 2 for the full, confirmed reasoning on why a constructor parameter is
   impossible here). `HotReloadEngineStateMutex` (`src/Core/
   Plugins/HotReloadEngineStateMutex.h`) already exists and is already locked
   by both of those placeholder bodies — this campaign's own new mutating
   code (ledger writes, `UnloadProjectAssembly()`) **must** take the exact
   same mutex, per that header's own explicit, load-bearing comment (lines
   29-34).

## Step 3: The Plan (five phases, in this fixed order)

```
PHASE1 — ComponentTypeRegistry::UnregisterDescriptor() (Hazard 1 fix)
    |  (no dependency on anything else in this campaign)
    v
PHASE2 — EditorPanelRegistry::UnregisterPluginPanel() (Hazard 2 fix,
         INCLUDING the m_allNames collision-guard fix Step 2 above found)
    |  (no dependency on PHASE1; both could run in parallel in principle,
    |   but this campaign runs them serially for a cleaner review trail)
    v
PHASE3 — NEW class ProjectAssemblyRegistrationLedger; wire it into the THREE
         existing registration entry points (Core::RegisterProjectRenderPassProvider,
         EditorPanelRegistry::RegisterPluginPanel, ComponentTypeRegistry::
         RegisterDescriptor); wire ProjectAssemblyHost::TryLoadOneAssembly()
         to bracket every GTE_RegisterProject call in BeginRecordingFor()/
         EndRecording(); replace EditorHotReloadDebugCapability::GetLedgerEntry()'s
         placeholder body with a real PeekEntry() call.
    |  (HARD compile dependency: needs PHASE1's UnregisterDescriptor() and
    |   PHASE2's UnregisterPluginPanel() to exist as the two methods
    |   UnregisterEverythingFor() calls)
    v
PHASE4 — ProjectAssemblyHost::UnloadProjectAssembly() (Hazard 4's
         WaitForGpuIdle() sequencing + PHASE3's ledger teardown +
         FreeLibrary(), in that exact order) + GetLoadedAssemblyFileNames();
         ProjectAssemblyBuildRunner's BackupProjectAssemblyBinaries()/
         RestoreProjectAssemblyBinariesFromBackup(); replace
         EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()'s
         placeholder body with a real call.
    |  (HARD compile dependency: needs PHASE3's ProjectAssemblyRegistrationLedger
    |   class to exist)
    v
PHASE5 — Extend Projects/ProjectAssemblyProbe/ with one throwaway custom ECS
         component (exercises Hazard 1 for real); full live isolation test of
         the WHOLE unload sequence via BIG-STEP 1's own HTTP routes (register,
         confirm live, unload, confirm empty/gone, confirm process survives);
         full incremental build + full ctest regression pass (the ONLY phase
         in this campaign allowed to run either); CAMPAIGN_COMPLETION_REPORT.md;
         AGENTS.md update.
```

### Cross-cutting rules for every phase below (do not repeat per-file, but every
phase MUST follow these)

- **No full build/ctest before PHASE5.** Each of PHASE1-4 ends with a
  **targeted/incremental compile check only** (build just the affected
  target(s) — `gte_core`/`GreatTamanaEngineTests`, whichever is smallest and
  sufficient to prove the new code compiles and links). Full build + full
  `ctest -C Debug --output-on-failure` happens exactly once, in PHASE5, per
  this whole session's own standing workflow rule.
- **Never use `printf`/`std::cout`/`OutputDebugString` anywhere.** Every new
  log line uses `GTE_LOG_INFO`/`GTE_LOG_WARNING`/`GTE_LOG_ERROR` (see
  `src/Core/Logging.h`), exactly like every existing call site in the files
  this campaign touches already does. Retrieve logs live via
  `gte_send_request` against `GET /get_logs` (optionally
  `?category=ProjectAssemblyBuild` or `?category=ProjectAssembly`) — never by
  reading a console window.
- **Verify every new piece of engine-internal state live, via
  `gte_send_request`, using BIG-STEP 1's own seven routes, before trusting a
  code read alone.** Each phase file below names the exact route(s) and
  exact expected JSON shape for its own new code. `run_app_background` to
  launch `GreatTamanaEditor.exe` non-blocking, `gte_send_request` to poll it,
  `stop_app_background` to clean up afterward — never leave a background
  instance running between phases.
- **Every new/modified method gets a doc comment citing this campaign**
  (`editor-core-separation-13`) and the specific Hazard/Step it fixes,
  mirroring this whole repo's own extremely consistent citation discipline
  (see every file read while writing this strategy).
- **New test files go where the existing sibling test lives, and MUST be
  manually added to `tests/CMakeLists.txt`'s own explicit file list** — this
  repo's test build does **not** glob test sources (confirmed: `tests/
  CMakeLists.txt` line 2147 lists `Core/Plugins/
  ProjectAssemblyHotReloadDebugStatusTests.cpp` explicitly). Forgetting this
  step means a new test file silently never runs, and `ctest`'s PHASE5 count
  simply won't include it. Never assume auto-discovery.
- **If a genuine design ambiguity comes up that this file (or the phase's own
  file) does not already resolve, use `ask_questions` before guessing** —
  every phase below already resolves every ambiguity this investigation
  found during its own research; a NEW one appearing during implementation
  (e.g. an API shape that changed since this was written) is exactly the
  case `ask_questions` exists for.
- **Every phase file's own "Definition of Done" is the actual bar — not
  "code compiles".** A phase is not done until its own live
  `gte_send_request` checks pass against a real running engine.

### Non-goals for this whole campaign (restated from the external plan, do not
expand scope)

- Does NOT implement the reload half, the compile half, or the freeze/UI
  half of hot reload — BIG-STEP 3's job.
- Does NOT implement ECS world-state snapshot/restore — BIG-STEP 4's job.
  PHASE5's own isolation test deliberately uses a component with no
  meaningful runtime VALUE to preserve — it proves the
  register/unregister MECHANISM, not state survival.
- Does NOT modify `ProjectAssemblyHost::LoadProjectAssemblies()`'s own
  startup SCAN behavior (still scans `<exe dir>/project_assemblies/` once, at
  startup, unconditionally) — this campaign only ADDS
  `UnloadProjectAssembly()`/`GetLoadedAssemblyFileNames()` alongside it, and
  brackets the EXISTING `TryLoadOneAssembly()` call with ledger
  recording (additive, not a behavior change to what gets loaded or when).
- Does NOT touch `plugins/gte_plugin_abi/`, `PluginHost`, or any pre-existing
  route's behavior.
- Does NOT decide any ImGui button placement for a future "Compile & Reload"
  action.

### Reading order for whoever (or whichever delegated session) implements this

This file, then PHASE1 through PHASE5 in order, each phase's own
`PHASEn_COMPLETION_REPORT.md` (once it exists) read before starting the next
phase — mirrors `editor-core-separation-12`'s own proven structure exactly.
