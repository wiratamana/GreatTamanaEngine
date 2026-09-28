# editor-core-separation-12 — PHASE0 MASTER STRATEGY

## Scope note (read this first)

This whole campaign (`editor-core-separation-12`) implements **ONLY BIG-STEP 1
of 4** from the external master plan:

- Master plan: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-2\HOTRELOAD_BIGSTEP_00_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
- This campaign implements: `HOTRELOAD_BIGSTEP_01_LIVE_DEBUG_AND_COMPILE_RELOAD_TRIGGERS_2026-09-28.txt`
- BIG-STEP 2 (teardown safety/registration ledger), BIG-STEP 3 (synchronous
  compile/atomic swap orchestrator), and BIG-STEP 4 (state snapshot/restore)
  are **NOT built by this campaign** — they are future campaigns. Every
  implementer of every phase below must resist the temptation to "helpfully"
  start building the real unload/reload cycle now. That is out of scope,
  on purpose, per the master plan's own dependency graph (BIG-STEP 1 has no
  hard compile-time dependency on BIG-STEP 2/3/4, and is deliberately built
  first, standing alone).

Every phase file below assumes BIG-STEP 0 and BIG-STEP 1's own source text
(both files above) as already-read ground truth. Re-read
`HOTRELOAD_BIGSTEP_01_...txt` before starting PHASE1 — it is the primary
spec; this document and its four children are the concrete, file-and-line
grounded implementation plan for it, including three corrections this
strategy pass found and locked in (Section 3 below).

---

## Step 1: The Goal (Where are we going?)

Today, `GreatTamanaEditor.exe` has **zero HTTP visibility** into its own
Project Assembly system: no route exposes which `.dll`s are loaded, which
ECS component types are registered, what a live entity's data looks like, or
any way to trigger a Project Assembly compile remotely. The existing
`TriggerProjectAssemblyCompile()` (`src/Core/Plugins/
ProjectAssemblyBuildRunner.h/.cpp`) has no caller anywhere outside a
not-yet-built ImGui button.

By the end of this campaign, an AI (or human) debugging this system live,
via `gte_send_request`, can:

1. `GET /project_assembly/hot_reload/status` — see which phase of a future
   hot-reload cycle is running (today: always reports `"Idle"`, since no
   cycle exists yet — this route's whole point is to already exist and be
   stable before BIG-STEP 3 ever calls into it).
2. `GET /project_assembly/debug/ledger?name=<X>` — see a project's
   registered render-pass/panel/component-type names (today: always
   empty — the real ledger is BIG-STEP 2's job; this route's contract is
   locked in now, its body fills in later).
3. `GET /project_assembly/debug/loaded_assemblies` — see which
   `_Game.dll`/`_Editor.dll` files are currently resident (today: always
   empty — `ProjectAssemblyHost` needs a new accessor, BIG-STEP 2's job).
4. `GET /project_assembly/debug/component_types` — see every
   `ComponentTypeRegistry`-registered type name, **live and real, today**
   (this one needs nothing from BIG-STEP 2 at all).
5. `GET /project_assembly/debug/scene_snapshot` — see the live ECS world as
   JSON, **live and real, today** (same generic `SceneDocument` machinery
   `File > Save Scene`/`POST /save_scene` already use).
6. `POST /project_assembly/debug/compile_only?name=<X>` — trigger a real,
   already-existing, already-working `cmake --build` for one Project
   Assembly, watch it stream into `GET /get_logs`, **live and real, today**.
7. `POST /project_assembly/hot_reload?name=<X>` — a stable, permanent route
   contract that answers `501 Not Implemented` today, and will be filled in
   by a future BIG-STEP 3 campaign without ever changing its own shape.

Everything is purely additive. Nothing about any existing route, panel, or
behavior changes.

---

## Step 2: The Situation (Where are we now?)

Confirmed directly against the real, current source tree (every citation
below re-verified by this strategy pass, not copied blind from the master
plan):

- **No existing capability interface fits.** `src/Core/EditorCapabilities.h`
  has exactly two Bucket-B capability interfaces today
  (`ISceneIOCapability`, `ILogQueryCapability`), neither of which answers
  "what is the Project Assembly Hot Reload system's live state". A new
  `IHotReloadDebugCapability` is needed, following that file's own, already
  twice-proven pattern exactly (nullable pointer, `nullptr` → safe 503,
  never a crash).
- **The real implementation lives in `gte_editor`, mirrored on
  `EditorLogQueryCapability`.** `src/Editor/EditorLogQueryCapability.h/.cpp`
  is a ~30-line pure-delegation class, wired into `NetworkServer`'s
  constructor via a namespace-scope `static` instance declared in
  `EditorHost.cpp` (that file, lines ~91-170, confirmed). The new
  `EditorHotReloadDebugCapability` follows this exact shape.
  `EditorSceneIOCapability.h/.cpp` is the OTHER precedent, used instead
  whenever the real body needs a live `Game&`/`Renderer&` per-call (not a
  singleton instance) — this campaign needs BOTH shapes for different
  methods (see PHASE1/PHASE2).
- **`NetworkServer`'s constructor takes 7 defaulted, non-owning pointers
  today** (`src/Network/NetworkServer.h`, confirmed), each one documented as
  "appended AFTER the previous one" specifically so
  `tests/Network/NetworkServerTests.cpp`'s several no-argument
  `NetworkServer server;` constructions never need to change. The 8th
  parameter, `IHotReloadDebugCapability* hotReloadDebugCapability = nullptr`,
  is appended at the end, following this exact, repeated convention.
- **`ComponentTypeRegistry::Instance().AllSortedByTypeName()` is real,
  public, and already returns `std::vector<ComponentTypeDescriptor>` with a
  `.typeName` field** (`src/ECS/Reflection/ComponentTypeDescriptor.h` line
  68, confirmed) — the component-types OBSERVE route can be genuinely real
  today, zero placeholder.
- **The generic scene-serialization system is real and already used for
  exactly this shape of read** (`src/Scene/SceneBuilder.h`'s
  `BuildSceneDocumentFromRegistry(Registry&, const AssetDatabase&)`,
  `src/Scene/SceneJsonFormat.h`'s `SerializeSceneDocument()`,
  `src/Editor/SceneIO.cpp`'s `SaveScene()` body as the exact worked
  precedent, confirmed lines 25-48) — the scene-snapshot OBSERVE route can
  be genuinely real today too, **but not the way BIG-STEP 1's own text
  describes it** — see Section 3, Correction 1, immediately below.
- **`TriggerProjectAssemblyCompile()` has zero existing callers anywhere in
  this codebase** (confirmed via `search_in_dir` across `src/`) — its
  signature is free to change with zero risk of breaking anything.
- **`src/Core/Plugins/ProjectAssemblyHost.h`'s `m_loadedAssemblies` and
  a real registration ledger are BIG-STEP 2 deliverables, not built by this
  campaign** — the loaded-assemblies and ledger OBSERVE routes are correctly
  placeholder-only for this campaign (empty results), per BIG-STEP 1's own
  Section 7.
- **`main CMakeLists.txt`'s source lists are hand-maintained, not
  globbed** (confirmed, line 348's own comment, and every new file listed
  individually — e.g. `EditorSceneIOCapability.cpp` at line 1017,
  `EditorLogQueryCapability.cpp` at line 1022, both inside `gte_editor`'s
  block; `ProjectAssemblyBuildRunner.cpp` at line 351, inside `gte_core`'s
  block). Every new file this campaign adds must be inserted into
  `CMakeLists.txt` by hand, in the correct target's block.

---

## Step 3: The Plan (How do we get there?)

### 3.1 — Three corrections this strategy pass locked in (read before PHASE1)

BIG-STEP 1's own text is the primary spec and is correct almost everywhere,
but this strategy pass found one genuine, concrete correctness gap and two
useful simplifications. All three are locked in for every phase below —
do not "restore" the literal BIG-STEP 1 wording over these.

**Correction 1 — `GetSceneSnapshotJson()` must NOT be a lock-free, direct,
network-thread read.** BIG-STEP 1's own Section 1 justifies bypassing the
per-frame `EngineCommandBridge` for all five OBSERVE routes by analogy with
`ILogQueryCapability` (a purpose-built, thread-safe store). But the live ECS
`Registry` is **not** a purpose-built thread-safe store — it is mutated
every single frame, unconditionally, by the main thread
(`Game::Update()`/`RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem`
— see `AGENTS.md`, "Entity-Component-System (ECS)": "only
`RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem` are allowed to
depend on both the ECS world and `Renderer`", with no thread-safety
mechanism of any kind). A network-thread route reading `Registry` directly,
at any arbitrary moment, races with that continuous per-frame mutation —
this is real undefined behavior, not a theoretical nitpick, and is exactly
why `BuildSceneDocumentFromRegistry()` today is only ever called from
`SaveScene()`/`LoadScene()`, which this engine already routes through
`EngineCommandBridge`'s `SubmitAndWait()` (main-thread-only execution,
confirmed `src/Network/NetworkServer.cpp` lines 1145-1164 for
`/save_scene`/`/load_scene`). **Fix: `GET
/project_assembly/debug/scene_snapshot` reuses that SAME, already-proven
bridge** — a new `EngineCommandKind::GetSceneSnapshot` — rather than being a
capability method the network thread calls directly. This is the ONE
OBSERVE route, of the five, that will correctly **block/hang if called
during an actual future frozen hot-reload cycle** — which is fine and
correct: nothing in BIG-STEP 1's own worked example (Section 8) ever
actually needs a snapshot mid-freeze, only immediately before triggering a
cycle and immediately after it completes, both times with the main loop
running normally. See PHASE2 for the exact mechanism.

**Correction 2 — a new, lightweight, shared mutex for the OTHER three
"maybe mutated later" OBSERVE routes.** `GetLedgerEntry()`,
`GetLoadedAssemblyFileNames()`, and `GetRegisteredComponentTypeNames()` read
`ComponentTypeRegistry`/the future ledger/the future assembly-list — none of
these are mutated per-frame (only at startup today, and only during a
future, main-thread-only, frozen reload cycle once BIG-STEP 2/3 exist) — so,
unlike the ECS `Registry`, a plain, cheap `std::mutex` genuinely is enough
to make them race-free, INCLUDING mid-freeze, exactly as BIG-STEP 1 intends.
PHASE1 adds one new, tiny, shared utility for this
(`HotReloadEngineStateMutex`) that a future BIG-STEP 2/3 mutator is REQUIRED
to also lock — documented loudly in that file so the requirement is never
lost.

**Correction 3 — `TriggerProjectAssemblyCompile()`'s return type changes
from `void` to `bool`.** Needed so `TriggerCompileOnly()` can honestly
report "started" vs. "rejected, already building" per BIG-STEP 1 Section 2
(f)'s own contract. Zero risk: confirmed zero existing callers anywhere.

### 3.2 — Child phase breakdown and dependency order

```
PHASE0 (this file)
   |
   v
PHASE1 — Capability Interface, Push-Status Singleton, Shared Mutex
   (Core/EditorCapabilities.h append; new
   Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h/.cpp;
   new Core/Plugins/HotReloadEngineStateMutex.h/.cpp;
   new Game/EngineCommandResults.h GetSceneSnapshotOutcome;
   new EngineCommandKind::GetSceneSnapshot + payload structs;
   CMakeLists.txt: 4 new files added by hand, in this SAME phase;
   new tests/Core/Plugins/ProjectAssemblyHotReloadDebugStatusTests.cpp)
   |
   v
PHASE2 — Real Capability Implementation + Engine Command Wiring
   (new Editor/EditorHotReloadDebugCapability.h/.cpp;
   ProjectAssemblyBuildRunner.h/.cpp signature change;
   EngineCommandDispatch.h/.cpp new parameter + switch case;
   CMakeLists.txt: 2 new files added by hand, in this SAME phase)
   |
   v
PHASE3 — HTTP Routes + NetworkServer/EditorHost Wiring
   (NetworkRoutes.h/.cpp: 7 new routes' parse/build pure functions;
   NetworkServer.h/.cpp: 8th constructor parameter + RegisterRoutes();
   EditorHost.h/.cpp: new capability instance + wiring + ExecuteEngineCommand
   call-site update; new tests/Network/
   ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp — CMakeLists.txt itself
   is NOT touched by this phase, only tests/CMakeLists.txt, since every
   production file this campaign creates was already registered by
   whichever phase introduced it)
   |
   v
PHASE4 — Live Verification & Definition of Done
   (incremental build, run GreatTamanaEditor.exe in the background,
   gte_send_request against all 7 routes, confirm real vs. placeholder data,
   confirm POST /project_assembly/debug/compile_only really compiles
   Projects/ProjectAssemblyProbe, confirm zero regression via a full
   ctest pass — the ONE phase in this campaign allowed to do that, per
   this campaign's own workflow rules)
```

Each phase must be delegated as its own implementation task, in this exact
order — PHASE2 needs PHASE1's new types to exist; PHASE3 needs PHASE2's
capability class and engine-command plumbing to exist; PHASE4 needs
everything built first.

### 3.3 — Files touched, by phase (quick reference)

| Phase | New files | Modified files |
|---|---|---|
| PHASE1 | `Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h/.cpp`, `Core/Plugins/HotReloadEngineStateMutex.h/.cpp`, `tests/Core/Plugins/ProjectAssemblyHotReloadDebugStatusTests.cpp` | `Core/EditorCapabilities.h`, `Application/EngineCommandBridge.h`, `Game/EngineCommandResults.h`, `CMakeLists.txt`, `tests/CMakeLists.txt` |
| PHASE2 | `Editor/EditorHotReloadDebugCapability.h/.cpp` | `Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`, `Application/EngineCommandDispatch.h/.cpp`, `CMakeLists.txt` |
| PHASE3 | `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp` | `Network/NetworkRoutes.h/.cpp`, `Network/NetworkServer.h/.cpp`, `Editor/EditorHost.h/.cpp`, `tests/CMakeLists.txt` |
| PHASE4 | — (test/report only) | none (verification only) |

Every new `.h/.cpp` pair must be added to `CMakeLists.txt` in the correct
target block (`Core/Plugins/*` → the `gte_core` block near line 351;
`Editor/*` → the `gte_editor` block near line 1022) — PHASE1 AND PHASE2
each add their OWN new files there directly, in the SAME phase that
introduces them; PHASE3 never touches the root `CMakeLists.txt` at all,
only `tests/CMakeLists.txt` (registering its own new end-to-end test file).

### 3.4 — Non-goals (unchanged from BIG-STEP 1's own text)

- Does NOT implement the real `PerformProjectAssemblyHotReload()` body —
  `TriggerHotReload()`/`POST /project_assembly/hot_reload` stay a
  deliberate, permanent-for-this-campaign placeholder.
- Does NOT add any other mutating debug endpoint beyond `compile_only`/
  `hot_reload`.
- Does NOT touch `ProjectAssemblyHost`'s unload behavior, `EditorPanelRegistry`,
  or `ComponentTypeRegistry`'s `RegisterDescriptor()`/lack of an unregister
  method — those are BIG-STEP 2's job.
- Does NOT change anything about `plugins/gte_plugin_abi/`, `PluginHost`, or
  any pre-existing route's behavior.

### 3.5 — Definition of Done for this whole campaign

- [ ] `IHotReloadDebugCapability` exists, `EditorHotReloadDebugCapability`
      implements it, wired exactly like `EditorLogQueryCapability`.
- [ ] `ProjectAssemblyHotReloadDebugStatus` and `HotReloadEngineStateMutex`
      both exist and compile standalone.
- [ ] All 7 routes exist and are confirmed live via `gte_send_request`
      against a real running `GreatTamanaEditor.exe`.
- [ ] `GET /project_assembly/debug/component_types` and `GET
      /project_assembly/debug/scene_snapshot` return genuinely real, live
      data (not placeholders).
- [ ] `GET /project_assembly/debug/ledger` and `GET
      /project_assembly/debug/loaded_assemblies` return well-formed, empty
      placeholder data (not an error).
- [ ] `POST /project_assembly/debug/compile_only` triggers a real compile of
      `Projects/ProjectAssemblyProbe`, visible in `GET /get_logs`.
- [ ] `POST /project_assembly/hot_reload` answers `501` with the documented
      placeholder body.
- [ ] A full clean incremental build succeeds; a full `ctest` regression
      pass shows zero new failures versus the pre-campaign baseline,
      including the new `tests/Core/Plugins/
      ProjectAssemblyHotReloadDebugStatusTests.cpp` (PHASE1) and
      `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`
      (PHASE3) test files.
