# editor-core-separation-19 — PHASE0 MASTER STRATEGY
## On-Engine Project Workflow — BIG-STEP 5 of 5: "Compile" Menu Item

Parent master plan (read first, outside this repo):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-3\PROJECTWORKFLOW_BIGSTEP_01_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
(Locked Design Decisions LDD-PW1..LDD-PW5 live there — this campaign obeys all
of them, restated only where directly relevant below.)

This phase's own source instruction (read second):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-3\PROJECTWORKFLOW_BIGSTEP_05_COMPILE_MENU_2026-09-28.txt`

Prior, already-completed campaigns in this same 5-file series (for context —
their `CAMPAIGN_COMPLETION_REPORT.md` files were read in full before writing
this strategy):
- `task_manager/editor-core-separation-16` — BIG-STEP 2, "Create New Project"
- `task_manager/editor-core-separation-17` — BIG-STEP 3, "Open Project"
- `task_manager/editor-core-separation-18` — BIG-STEP 4, "Create New Script/Shader Asset"

## Child phase files (read in this order)
- `PHASE1_CAPABILITY_WIRING_AND_COMPILE_MENU_ITEM.md` — all production code.
- `PHASE2_TESTS_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` — new tests, full
  live HTTP/screenshot verification, full clean build, full `ctest`
  regression, `CAMPAIGN_COMPLETION_REPORT.md`.

Only 2 implementation phases. This is deliberate, not lazy — see Step 3 below
for exactly why this campaign's real scope is small, and Section "New,
real gap this Phase0 found" for the one place it is NOT as small as the
source `.txt` file assumed.

---

## Step 1: The Goal (Where are we going?)

A real, clickable "Project > Compile '<ActiveProjectName>'" menu item, in the
Editor's own menu bar, that:
1. Is **visible but disabled** (grayed out) whenever there is no active
   Project Assembly (`ActiveProjectAssemblyState::Instance().GetActive().hasActiveProject == false`).
2. Becomes **enabled**, with the active project's real name in its own label,
   the moment `CreateNewProjectAssembly()` (BIG-STEP 2) or
   `OpenProjectAssembly()`/`OpenProjectAssemblyOnMainThread()` (BIG-STEP 3)
   sets one active.
3. When clicked, triggers the EXACT SAME `TriggerCompileOnly(projectName)`
   call `POST /project_assembly/debug/compile_only?name=<X>` already
   triggers — one function, two callers, per LDD-PW5. **Zero new HTTP route
   is added by this campaign** — that route already exists, today, unmodified.
4. Shows a short-lived, colored status toast afterward (reusing
   `EditorContext::projectWorkflowStatusMessage`/`...IsError`/`...SetTime`,
   the exact field family `NewProjectWindow.cpp`/`OpenProjectWindow.cpp`
   already use), and, as a small, genuinely low-risk UX polish item, appends
   `" (compiling...)"` to its own label while a build for that exact project
   is already in flight (never disabling the item for this reason — see
   Step 3.4 below for exactly why disabling would be wrong).

This closes the whole 5-file "On-Engine Project Workflow" plan — after this
campaign, Create/Open/Script-scaffold/Compile are ALL reachable both from
inside the running Editor and from a raw HTTP POST, with no external text
editor or manually-typed `cmake` command required for any of the four.

---

## Step 2: The Situation (Where are we now?)

Everything BIG-STEP 5's own source `.txt` file (Finding 7, and its own STEP 1)
claims already exists was independently re-confirmed, directly, by reading
the real, current source tree before writing this strategy:

- `TriggerProjectAssemblyCompile(projectName, buildDirectory)`
  (`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`) — real, working,
  background-thread `cmake --build <dir> --target <Name>_Game` (then
  `_Editor`), guarded against overlapping concurrent triggers for the SAME
  project via a private, anonymous-namespace `g_inFlightProjects` set
  (`g_inFlightMutex`/`TryMarkInFlight()`/`ClearInFlight()`,
  `ProjectAssemblyBuildRunner.cpp` lines 32-49). **Confirmed: no public
  accessor for "is a build in flight for this project" exists today** — this
  is BIG-STEP 5's own "New thing 2 (optional polish)", and this campaign
  DOES build it (Step 3.2 below), since it costs almost nothing and directly
  improves the one new UI surface this campaign adds.
- `IHotReloadDebugCapability::TriggerCompileOnly(const std::string&)`
  (`src/Core/EditorCapabilities.h` lines 173-256) — real, already implemented
  by `EditorHotReloadDebugCapability::TriggerCompileOnly()`
  (`src/Editor/EditorHotReloadDebugCapability.cpp` lines 101-108), which
  resolves the build directory via `ResolveCMakeBuildDirectory(gte::ExecutableDirectory())`
  and calls `TriggerProjectAssemblyCompile()` — confirmed, unchanged.
- `POST /project_assembly/debug/compile_only?name=<X>`
  (`src/Network/NetworkServer.cpp` lines 1314-1331) — real, registered,
  already calls `hotReloadDebugCapability->TriggerCompileOnly(parsed.projectName)`
  and returns `BuildCompileOnlyTriggerResponseJson(started, ...)`. **Zero
  changes needed to this route by this campaign.**
- `ActiveProjectAssemblyState` (`src/Editor/ActiveProjectAssemblyState.h/.cpp`)
  — real, already carries `hasActiveProject`/`name`/`sourceDirectory`/
  `assetsDirectory`/`isCompiled`/`isLoaded`, `GetActive()` re-derives
  `isCompiled`/`isLoaded` fresh every call, confirmed unchanged since
  `editor-core-separation-17`.
- The "Project" menu's own disabled placeholder — confirmed, VERBATIM, at
  `src/Editor/DockLayout.cpp` line 196:
  `if (ImGui::MenuItem("Compile", nullptr, false, false)) {}` — exactly the
  one line this campaign's Phase1 replaces.
- `EditorContext::projectWorkflowStatusMessage`/`...IsError`/`...SetTime`
  (`src/Editor/EditorContext.h` lines 302-311) and its own rendering block
  (`DockLayout.cpp` lines 297-310) — real, already shared by New/Open
  Project, confirmed reusable as-is, zero changes needed to the toast
  mechanism itself.

### New, real gap this Phase0 found (the source `.txt` file's own STEP 2 code sketch does NOT compile as written, and one real wiring path is entirely missing)

The source `.txt` file's own "New thing 1" code sketch
(`PROJECTWORKFLOW_BIGSTEP_05...txt`, lines 51-65) assumes
`hotReloadDebugCapability` is simply an already-in-scope local variable
inside `DockLayout.cpp`'s menu-bar code. **This is false, confirmed by
direct inspection**:

- `BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer)`
  (`src/Editor/DockLayout.h` line 16, `DockLayout.cpp` line 112) takes
  exactly THREE parameters — no capability pointer of any kind flows into
  this function today. `NewProjectWindow`/`OpenProjectWindow`/
  `CreateAssetWindow` each solve this the SAME way — `ImGuiEditorLayer::BuildUI()`
  calls each window's own `Build(m_ctx, m_projectLifecycleCapability)` /
  `Build(m_ctx, m_assetScaffoldingCapability)` SEPARATELY, AFTER
  `BuildDockspaceAndMenuBar()` returns (`ImGuiEditorLayer.cpp` lines 529-546)
  — but the "Compile" menu ITEM ITSELF must render and react INSIDE the
  menu bar, i.e. inside `BuildDockspaceAndMenuBar()`, not in a separate,
  later `Build()` call — there is no floating window to defer this to.
- `IEditorLayer` (`src/Editor/EditorLayer.h`) has exactly two capability
  setters today — `SetProjectLifecycleCapability()` (line 726) and
  `SetAssetScaffoldingCapability()` (line 738) — **confirmed, by
  `search_in_dir`, that no `SetHotReloadDebugCapability`-shaped method
  exists anywhere in this codebase.** `ImGuiEditorLayer` therefore has NO
  member pointer to `IHotReloadDebugCapability` at all today — it is wired
  directly into `NetworkServer`'s constructor (`EditorHost.cpp` line 214,
  the 8th argument, `&s_editorHotReloadDebugCapability`) but never handed to
  `m_editorLayer` the way the two Project-Workflow capabilities already are.

**This means Phase1's real scope is: (a) the trivial in-flight-query
function BIG-STEP 5's own file already anticipated as optional, PLUS
(b) a genuinely new, non-optional capability-wiring path — a THIRD
`IEditorLayer` capability setter, mirroring the exact two that already
exist — that the source `.txt` file's own STEP 2 silently assumed was
unnecessary.** This is disclosed here, loudly, exactly the way
`editor-core-separation-17`/`-18` each disclosed their own master-plan
corrections in their own PHASE0/completion reports, per this repo's own
established campaign discipline — never silently patched over.

### Second, smaller correction: the source `.txt` file's own code sketch forgets `projectWorkflowStatusSetTime`

`PROJECTWORKFLOW_BIGSTEP_05...txt` lines 58-63 set
`ctx.projectWorkflowStatusMessage`/`...IsError` but never
`ctx.projectWorkflowStatusSetTime`. Every existing caller of this shared
toast (`NewProjectWindow.cpp` line 46, `OpenProjectWindow.cpp`, confirmed by
direct read) DOES set it, and `DockLayout.cpp`'s own rendering block (lines
301-310) uses it to decide whether the message has expired
(`kProjectWorkflowStatusLifetime`, 4000ms) — omitting it would leave the
toast permanently stuck at whatever `std::chrono::steady_clock::time_point`'s
own default-constructed (effectively epoch/zero) value is, meaning the
"already expired" branch (`ctx.projectWorkflowStatusMessage.clear()`) would
fire on the VERY NEXT FRAME after the message is set — the toast would
never actually be visible for its intended 4 seconds. Phase1 sets this
field explicitly, exactly like the two existing callers.

---

## Step 3: The Plan (detailed strategy)

### 3.1 — Dependency graph (2 phases, strictly sequential)

```
PHASE1 (production code: backend query fn, 3rd IEditorLayer capability
        setter, DockLayout.cpp real Compile menu item)
        |
        v
PHASE2 (new Tier-1 test file, live HTTP+screenshot verification, full clean
        build, full ctest regression, CAMPAIGN_COMPLETION_REPORT.md)
```

Nothing in this campaign depends on BIG-STEP 4's own scaffolding feature at
all (per the source file's own header: "otherwise almost entirely
independent of BIG-STEP 2/3/4's own file-writing machinery") — only on
`ActiveProjectAssemblyState` (BIG-STEP 2) existing, which it already does.

### 3.2 — Why the "optional" in-flight query is built anyway

BIG-STEP 5's own file frames `IsProjectAssemblyBuildInFlight()` as optional
polish. This campaign builds it anyway because: (a) it is a 4-line,
zero-risk, read-only wrapper over an already-existing, already-correctly-
guarded private set; (b) leaving the menu item's label static ("Compile
'X'") the whole time a background build is running is a real, avoidable UX
gap this campaign is explicitly here to close; (c) it gives Phase2 a
genuinely testable new unit of behavior instead of a phase with zero new
backend logic to verify. It is NOT wired into `IProjectLifecycleCapability`
or `IAssetScaffoldingCapability` — it is a new method on
`IHotReloadDebugCapability` (`IsCompileInFlight`), since that interface is
the one that already owns `TriggerCompileOnly`/build-triggering concerns.

### 3.3 — The 3rd `IEditorLayer` capability setter, named and shaped precisely

`SetHotReloadDebugCapability(IHotReloadDebugCapability* capability)` —
chosen to mirror `SetProjectLifecycleCapability`/`SetAssetScaffoldingCapability`
byte-for-byte in naming convention (`Set<InterfaceNameMinusThe-I-Prefix>`).
Stored as `m_hotReloadDebugCapability` in `ImGuiEditorLayer`, a no-op in
`NullEditorLayer`, wired exactly once from `EditorHost`'s constructor body
using the SAME already-existing `&s_editorHotReloadDebugCapability` pointer
that's already passed into `NetworkServer`'s 8th constructor argument today
— no new static object, no new lifetime concern, purely an additional
pointer handed to an object that didn't have it yet.

### 3.4 — Why the Compile menu item stays ENABLED while a build is in flight (a deliberate, disclosed design choice)

BIG-STEP 5's own STEP 3 "Definition of Done" explicitly requires: *"Clicking
it a second time WHILE the first build is still running shows the 'already
in progress' status message... and does not spawn a second overlapping
child process."* This is only observable through the ImGui menu item itself
if the item stays CLICKABLE while a build runs. This campaign therefore
does NOT disable the menu item while `IsCompileInFlight()` is true — it only
appends `" (compiling...)"` to the label as an informational cue. Disabling
it would silently violate the master plan's own explicit Definition of Done
bullet — flagged here so nobody "improves" this later without re-reading
that bullet first.

### 3.5 — Explicit non-goals (restated from the source file, unchanged)

- No progress bar / percentage.
- No cancel-mid-build button.
- Does NOT touch, rename, or fold together the existing, separate
  "Compile & Reload" hot-reload feature or its own
  `POST /project_assembly/hot_reload` route.
- Does NOT add any new HTTP route (the existing `compile_only` route is
  reused, completely unmodified).
- Does NOT change `ActiveProjectAssemblyState`'s own shape or contract.

### 3.6 — File manifest (every file this whole campaign touches or adds)

Modified (no new production files needed beyond the 2 setters/1 query fn):
- `src/Core/Plugins/ProjectAssemblyBuildRunner.h` / `.cpp`
- `src/Core/EditorCapabilities.h`
- `src/Editor/EditorHotReloadDebugCapability.h` / `.cpp`
- `src/Editor/EditorLayer.h`
- `src/Editor/NullEditorLayer.cpp`
- `src/Editor/ImGuiEditorLayer.cpp`
- `src/Editor/EditorHost.cpp`
- `src/Editor/DockLayout.h` / `.cpp`

New (Phase2 only):
- `tests/Core/Plugins/ProjectAssemblyBuildRunnerInFlightQueryTests.cpp`
- `tests/CMakeLists.txt` (append the one new line for the file above — this
  is a hand-maintained list, confirmed by `search_in_dir`, NOT a glob, and
  `editor-core-separation-18`'s own PHASE3 already independently rediscovered
  this exact gotcha for the ROOT `CMakeLists.txt`'s `gte_editor` list; this
  campaign adds zero new PRODUCTION `.cpp`/`.h` files, so only `tests/CMakeLists.txt`
  is at risk here, not the root list)
- `task_manager/editor-core-separation-19/CAMPAIGN_COMPLETION_REPORT.md`

### 3.7 — Report-back convention

Mirrors every prior campaign in this series exactly: each phase, once its
own compile check passes, is `git_add`/`git_commit`ed with a message naming
the phase; Phase2 additionally writes the final `CAMPAIGN_COMPLETION_REPORT.md`
covering both phases, any real deviation found during implementation (there
may be more beyond the two already disclosed above — report them honestly,
exactly like every prior campaign's own report did), and the final full
build/regression numbers.
