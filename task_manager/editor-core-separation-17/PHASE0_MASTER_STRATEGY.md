# editor-core-separation-17 — MASTER STRATEGY
## On-Engine Project Workflow — BIG-STEP 3 of 5: "Open Project"

Parent series: `GreatTamanaEngin-Ideas/project-assembly-impl-3/PROJECTWORKFLOW_BIGSTEP_01..05_*.txt`.
This campaign implements **BIG-STEP 3 — "Open Project"**
(`PROJECTWORKFLOW_BIGSTEP_03_OPEN_PROJECT_2026-09-28.txt`). BIG-STEP 2
("Create New Project") is **already fully done** — `task_manager/editor-core-separation-16`,
all 5 phases, 1951/1951 tests passing. This campaign builds directly on top
of it, on the same branch (`feature/editor-core-separation`).

---
## Step 1: The Goal (Where are we going?)

Ship a second, fully-working on-engine project-workflow action: **"Open
Project"**. Concretely, when this campaign is done:

1. A user can click the Editor's **Project > Open Project...** menu item
   (currently a disabled placeholder, `DockLayout.cpp` line 187) and see a
   floating window listing every folder found directly under the resolved
   Project Assembly source root (`Projects/`), each one tagged with a
   colored validity badge (5 tiers — see Phase 1). Selecting a row and
   clicking "Open" marks it the new **active** project
   (`ActiveProjectAssemblyState`) and, if it is already compiled, genuinely
   loads its `.dll` into the running process (the SAME mechanism the
   Editor's own startup already uses for every other project).
2. The exact same action is reachable from a raw HTTP request:
   `POST /project_assembly/open_project?name=<X>`, plus a new, read-only
   `GET /project_assembly/list_projects` an external AI agent can poll to
   discover what is even available to Open, without needing eyes on the
   running Editor.
3. Both call paths are backed by **one, single, shared implementation** —
   never two independently-written/drifting code paths — per this whole
   5-file plan's own LDD-PW5.
4. Opening a project **never fails just because it hasn't been compiled
   yet** (LDD-PW4) — it always succeeds at "marking active", with an honest,
   tier-specific status message telling the user/agent what to do next.
5. Opening an **already-loaded** project never attempts a second,
   crash-prone load — it only updates which project is "active".

---
## Step 2: The Situation (Where are we now?)

### 2.1 — What already exists (confirmed by directly reading the real,
current repository right now, not assumed from the source `.txt` file
alone):

- **`ActiveProjectAssemblyState`** (`src/Editor/ActiveProjectAssemblyState.h/.cpp`)
  is **already fully built**, including `isCompiled`/`isLoaded`
  re-derivation inside `GetActive()` (real `std::filesystem::exists()` +
  `GetHotReloadEngineStateMutex()`-guarded scan of
  `ProjectAssemblyHost::GetLoadedAssemblyFileNames()`). **This is a genuine,
  positive correction to the source `.txt` file's own STEP 3**, which
  describes this as work this campaign still has to do — it does not. Do
  not re-implement it; only *read* it.
- **`IProjectLifecycleCapability`** (`src/Core/EditorCapabilities.h`, line
  263) currently has exactly one method, `CreateNewProjectAssembly()`. This
  campaign extends the SAME interface (never a second, competing
  interface), exactly as the source `.txt` file's own STEP 4 says.
- **`EditorProjectLifecycleCapability`** (`src/Editor/EditorProjectLifecycleCapability.h/.cpp`)
  is the real, currently-stateless implementation — it resolves
  `ResolveCMakeBuildDirectory()`/`ResolveProjectAssemblySourceRootDirectory()`
  fresh on every call and holds no members at all today. This campaign adds
  its first-ever member state (a bridge pointer + engine references — see
  Phase 3).
- **`NetworkServer`**'s constructor already carries a 9th parameter,
  `IProjectLifecycleCapability* projectLifecycleCapability` — **no new
  constructor parameter is needed for this campaign's new HTTP routes**,
  they reuse the exact same pointer.
- **`NewProjectWindow`** (`src/Editor/NewProjectWindow.h/.cpp`) is the exact,
  real, proven template `OpenProjectWindow` mirrors: non-dockable
  (`ImGuiWindowFlags_NoDocking`), driven by one `EditorContext` bool
  (`ctx.newProjectWindowOpen`), calling the capability directly from ImGui
  code, writing outcome into the shared
  `ctx.projectWorkflowStatusMessage`/`...StatusIsError`/`...StatusSetTime`
  fields (`EditorContext.h` lines 269-271 — already generically named for
  reuse by this exact campaign).
- **`AssetImportCommandBridge`** (`src/Application/AssetImportCommandBridge.h/.cpp`)
  is the exact, real, structurally-proven shape the source `.txt` file's own
  STEP 5 says to mirror for the new bridge — single global slot,
  mutex+condition_variable, `SubmitAndWait()` returning
  `std::optional<Result>` + `alreadyPending`/`timedOut` flags,
  `TryPeekPendingCommandRequest()`/`FulfillCommand()`.
- **`ProjectAssemblyHost::LoadOneProjectAssemblyFromExactPath()` /
  `...FromExactPathIfExists()`** (`src/Core/Plugins/ProjectAssemblyHost.h`,
  lines 117/123) already exist, already used by the hot-reload orchestrator
  (`ProjectAssemblyHotReload.cpp` lines 176-177/204-205) — this campaign
  reuses them completely unmodified. `ProjectAssemblyHost.cpp` **already
  locks `GetHotReloadEngineStateMutex()` internally** around its own
  mutation of `m_loadedAssemblies` (confirmed, lines 103/212) — a new call
  site does **not** need to take this lock itself.
- **`GetHotReloadEngineStateMutex()`** (`src/Core/Plugins/HotReloadEngineStateMutex.h`)
  is the one process-wide mutex already guarding this exact class of state.
- **`EditorHost::Run()`**'s per-frame loop already drains 4 bridges in
  sequence, early in the frame, BEFORE `m_editorLayer`'s own ImGui
  build/render happens later the same frame
  (`m_commandBridge`/`m_hotReloadCommandBridge`/
  `m_renderGraphControlCommandBridge`/`m_assetImportCommandBridge` —
  `EditorHost.cpp` lines ~491/506/701-723/727-749). This campaign adds a
  5th drain block, same section, same style.
- **`IEditorLayer::ImportExternalAssetIntoProject()`** (`EditorLayer.h` line
  606) is the existing, PROVEN precedent for "one real function, called
  DIRECTLY by ImGui code already on the main thread (`ProjectPanel`'s own
  drag-and-drop handler), and ALSO called from `EditorHost::Run()`'s bridge
  drain block (also on the main thread, just reached later, via a network
  request)" — **never through the bridge from the ImGui call site itself**.
  This is the exact pattern Phase 3 below copies to avoid a real deadlock
  (see 2.2).

### 2.2 — The one real bug this investigation found in the source `.txt`
file, and why it matters (read this before writing any Phase 3/4 code):

The source document's own STEP 5 says: *"BOTH callers — the ImGui 'Open'
button AND the HTTP route handler — submit through this SAME bridge."*
**Taken literally, this deadlocks the whole Editor the first time a user
clicks "Open" on a Tier-3 (compiled) project.** Why: `EditorHost::Run()` is
a single-threaded loop. Every bridge (`m_hotReloadCommandBridge`,
`m_assetImportCommandBridge`, etc.) is drained **once, early, each frame**
— then, **later in that SAME frame**, `m_editorLayer`'s own `Build()` call
renders ImGui, including `OpenProjectWindow`. If `OpenProjectWindow`'s
"Open" button handler calls a capability method that itself calls
`bridge.SubmitAndWait()` and **blocks the calling thread** waiting for a
drain point to service it — but the calling thread **is** the main thread,
and the only place that ever drains this bridge is a spot **earlier in this
exact same frame's own call stack**, which has already passed and can never
run again until this very call returns — the engine hangs forever. This is
categorically different from the HTTP-route caller, which really is a
second, separate OS thread (`NetworkServer`'s own background thread) and
can safely block waiting for the (different) main thread to make progress.

**The fix (this campaign's own necessary, disclosed refinement, mirroring
the ALREADY-PROVEN `ImportExternalAssetIntoProject()` precedent from 2.1
above)**: `IProjectLifecycleCapability` gains **two** methods for opening a
project, not one:

- `OpenProjectAssembly(name)` — safe to call from **any thread**. For a
  Tier-3 folder, submits into the new bridge and **blocks**, waiting for
  `EditorHost::Run()`'s own drain point to perform the real load and signal
  back. This is the one the **HTTP route** calls.
- `OpenProjectAssemblyOnMainThread(name)` — callable **only** from the
  engine's own main thread. For a Tier-3 folder, it calls the exact same
  underlying "perform the real load" logic **directly, inline, with no
  bridge/wait at all** (there is no thread to hop to — it is already there).
  This is the one **`OpenProjectWindow`** calls.

Both methods share one common, private helper for Tier 0/1/2/4 handling
(pure/cheap, thread-safe either way) and one common, private helper for the
actual Tier-3 `LoadOneProjectAssemblyFromExactPath[IfExists]()` pair — see
Phase 3 for the exact shape. This is flagged loudly, in this file, as a
**deliberate, necessary, disclosed correction** to the source `.txt` file —
not a silent reinterpretation.

### 2.3 — Non-goals (unchanged from BIG-STEP 1/3's own source files)

- Does NOT unload a previously-loaded, still-running project when a
  different one becomes "active" — only the hot-reload feature ever
  unloads anything.
- Does NOT auto-compile a Tier 1/2 selection.
- Does NOT provide a folder browser outside the resolved Project Assembly
  source root.
- Does NOT attempt to detect/repair a corrupted/partially-written project
  folder beyond the plain 5-tier classification.
- Does NOT implement BIG-STEP 4 ("Create Script Asset") or BIG-STEP 5
  ("Compile menu") — those remain separate, later campaigns.

---
## Step 3: The Plan (super-detailed strategy)

### 3.1 — Dependency graph (do not reorder)

```
PHASE0 (this file)
   |
   v
PHASE1 — Tier Classification Model
   (new, pure, gte_core-tier ClassifyProjectAssemblyFolder() +
   ProjectValidityTier enum, ProjectAssemblyBuildRunner.h/.cpp.
   Zero engine-state dependency; fully Tier-1-testable today.)
   |
   v
PHASE2 — ProjectLifecycleLoadCommandBridge
   (new, gte_application-tier cross-thread bridge, structurally identical
   to AssetImportCommandBridge. Zero dependency on Phase 1's enum - can, in
   principle, be built in parallel, but is sequenced after it here to keep
   each phase's own compile-check small and its own report readable.)
   |
   v
PHASE3 — EditorProjectLifecycleCapability extension + main-thread-safety fix
   (OpenProjectAssembly()/OpenProjectAssemblyOnMainThread()/
   ListProjectAssemblies(), new setters, EditorHost.cpp wiring + new Run()
   drain block. Depends on BOTH Phase 1 (tier enum) and Phase 2 (bridge).)
   |
   v
PHASE4 — HTTP routes + OpenProjectWindow UI + menu wiring
   (depends on Phase 3's two new capability methods existing.)
   |
   v
PHASE5 — Full live verification, regression, campaign closeout
   (depends on everything above.)
```

### 3.2 — One-paragraph summary of each child phase file

- **`PHASE1_TIER_CLASSIFICATION_MODEL.md`** — adds
  `enum class ProjectValidityTier` and
  `ClassifyProjectAssemblyFolder(candidateFolder, outputDirectory,
  loadedDllFileNames)` to `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`,
  implementing the 5-tier model (`NotAProject` / `NotBuildable` /
  `NotCompiled` / `Compiled` / `AlreadyLoaded`) from the source `.txt`
  file's own STEP 2, with a brand-new Tier-1 test file covering all 5
  tiers against synthetic temp-directory fixtures.
- **`PHASE2_PROJECT_LIFECYCLE_LOAD_COMMAND_BRIDGE.md`** — adds
  `src/Application/ProjectLifecycleLoadCommandBridge.h/.cpp`, a new,
  dedicated cross-thread bridge structurally identical to
  `AssetImportCommandBridge`, plus a Tier-1 test file mirroring
  `AssetImportCommandBridgeTests.cpp`'s own exact shape (submit/fulfill,
  already-pending rejection, timeout, late-fulfillment-after-timeout is
  inert).
- **`PHASE3_OPEN_PROJECT_ASSEMBLY_CAPABILITY_AND_MAIN_THREAD_SAFETY.md`** —
  the meaty phase: extends `IProjectLifecycleCapability`
  (`Core/EditorCapabilities.h`) with `OpenProjectOutcome`,
  `OpenProjectAssembly()`, `OpenProjectAssemblyOnMainThread()`, and
  `ListProjectAssemblies()`; extends `EditorProjectLifecycleCapability` with
  its first-ever member state (bridge pointer + `Core&`/`EditorHost*`
  references via new setters, mirroring `EditorHotReloadDebugCapability`'s
  own 3-setter precedent); wires `EditorHost.cpp`'s constructor + adds the
  new 5th bridge-drain block in `EditorHost::Run()`. This is where Section
  2.2's deadlock fix is actually implemented.
- **`PHASE4_HTTP_ROUTES_AND_OPEN_PROJECT_WINDOW_UI.md`** — adds
  `POST /project_assembly/open_project` and
  `GET /project_assembly/list_projects` to `NetworkRoutes`/`NetworkServer.cpp`
  (reusing the existing 9th constructor pointer, zero signature change);
  adds `src/Editor/OpenProjectWindow.h/.cpp` (mirrors `NewProjectWindow`
  exactly, a scrollable, tier-badged list instead of a text box); enables
  the previously-disabled "Open Project..." menu item in `DockLayout.cpp`.
- **`PHASE5_FULL_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md`** — a new
  end-to-end test file (`tests/Network/OpenProjectEndpointEndToEndTests.cpp`),
  a full clean build, a full `ctest` regression run, live HTTP-driven
  verification of every tier + the already-loaded no-double-load guarantee,
  and the `CAMPAIGN_COMPLETION_REPORT.md`.

### 3.3 — How progress is reported (mirrors `editor-core-separation-16`'s
own convention exactly)

After each phase, its own implementer writes a
`PHASEn_COMPLETION_REPORT.md` in this SAME folder: what was actually built,
any real, mechanically-confirmed deviation from that phase's own file (and
why), and any new gap found. `PHASE5`'s own implementer additionally writes
`CAMPAIGN_COMPLETION_REPORT.md`, mirroring
`editor-core-separation-16/CAMPAIGN_COMPLETION_REPORT.md`'s exact shape,
including a "new gaps found, for the NEXT campaign (BIG-STEP 4, Create
Script Asset) to know about" section.

### 3.4 — Build/test discipline for every phase

- Every phase EXCEPT Phase 5 does a **fast, incremental** compile check only
  (`cmake --build build --target GreatTamanaEditor` or a narrower unit-test
  target) — never a full clean rebuild, never a full `ctest` run.
- Only **Phase 5** runs the full clean build + full `ctest` regression
  suite.
- Every phase commits its own change (`git_add` + `git_commit`) before the
  next phase begins.
- Use the engine's own internal logging (`GTE_LOG_INFO`/`GTE_LOG_ERROR`,
  `Core/Logging.h`) — never `printf`/`std::cout`/`OutputDebugString`. Fetch
  logs live via `GET /get_logs` (`gte_send_request`) whenever a running
  `GreatTamanaEditor.exe` needs debugging instead of guessing from source
  alone.
