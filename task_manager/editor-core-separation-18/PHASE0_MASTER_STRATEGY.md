# editor-core-separation-18 — PHASE0 MASTER STRATEGY
## On-Engine Project Workflow — BIG-STEP 4 of 5: "Create New Script/Shader Asset" (right-click Create menu)

This is the ORCHESTRATOR file. Read this FIRST, always, before any child
phase file. Every child phase file (`PHASE1_*.md` through `PHASE4_*.md`)
assumes you have already read this one.

Source master-plan documents (read-only, external, do not edit):
- `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-3\PROJECTWORKFLOW_BIGSTEP_01_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
- `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-3\PROJECTWORKFLOW_BIGSTEP_04_CREATE_NEW_SCRIPT_ASSET_SCAFFOLDING_2026-09-28.txt`

Prior campaigns this one directly depends on and reuses (already shipped,
do not redesign):
- `task_manager/editor-core-separation-16/` — BIG-STEP 2, "Create New
  Project". Shipped `ActiveProjectAssemblyState`, `IProjectLifecycleCapability`,
  `IsValidProjectAssemblyIdentifierName()`, `NewProjectWindow`.
- `task_manager/editor-core-separation-17/` — BIG-STEP 3, "Open Project".
  Shipped `OpenProjectWindow`, extended `IProjectLifecycleCapability` with
  `OpenProjectAssembly()`/`OpenProjectAssemblyOnMainThread()`/`ListProjectAssemblies()`,
  and (critically for THIS campaign) confirmed `ActiveProjectAssemblyState`
  ALREADY carries `assetsDirectory`/`isCompiled`/`isLoaded` fields, fully
  working, today.

---

## STEP 1 — THE GOAL (Where are we going?)

Ship a fourth, real, on-engine capability: from the Editor's existing
"Project" panel, right-click on a new, clearly-labeled "[Active Project]
&lt;Name&gt;" row → **Create** → **Render Pass...** / **Compute Shader...** /
**Vertex/Fragment Shader Pair...** → type a name in a small popup window →
a real, immediately-compilable file (or pair of files) is written straight
into the CURRENTLY ACTIVE Project Assembly's own `Assets/` folder. The exact
same action must ALSO be triggerable by a raw HTTP POST
(`/project_assembly/create_asset`), with zero divergence between the two
call paths (LDD-PW5, restated by every prior campaign in this 5-file plan).

Concretely, "done" means:
- A user (or an AI agent driving this purely over HTTP, with no Editor
  window even open) can scaffold a starting Render Pass, Compute Shader, or
  Shader Pair into whichever project is currently active, get a clear
  success/failure answer, and never accidentally clobber an existing
  same-named file (case-insensitively).
- Every scaffolded `.cpp` file (Render Pass / Compute Shader kinds) is
  **inert by design** — it compiles cleanly but registers nothing until the
  user manually adds one call line to their own project's `RegisterProject()`
  function — and this "one `GTE_RegisterProject` per target" constraint is
  mechanically proven, live, not just asserted in a comment (see PHASE4).
- Nothing about the pre-existing "Project" panel's own content-asset browsing
  behavior (the panel this feature extends) changes in any way.

## STEP 2 — THE SITUATION / THE PROBLEM (Where are we now?)

**What already exists and works, confirmed by directly reading the real,
current source (not the master-plan sketch alone) as of this writing:**

- `src/Editor/ActiveProjectAssemblyState.h/.cpp` — the ONE shared "which
  project is active" singleton. Its `ActiveProjectAssemblyInfo` struct
  **already has** `assetsDirectory` (computed as `sourceDirectory / "Assets"`
  inside `GetActive()`), `isCompiled`, and `isLoaded` — BIG-STEP 3 already
  shipped all of this. This campaign only ever READS `GetActive()`; it never
  needs to touch `ActiveProjectAssemblyState.h/.cpp` at all.
- `src/Core/EditorCapabilities.h` — `IProjectLifecycleCapability` exists,
  with `CreateProjectOutcome`, `OpenProjectOutcome`, `ProjectListEntry`, and
  four methods. This campaign adds a **sibling** interface,
  `IAssetScaffoldingCapability`, in the SAME file — never folds a genuinely
  different capability question into that existing interface (this repo's
  own written "one interface per genuinely new capability gap" rule,
  restated at the top of `EditorCapabilities.h` itself).
- `src/Editor/EditorProjectLifecycleCapability.h/.cpp` — the real
  implementation of `IProjectLifecycleCapability`. Already holds
  `m_core`/`m_editorHost` engine references and already knows how to resolve
  the Project Assembly source root and validate names. This campaign extends
  this SAME class to ALSO implement `IAssetScaffoldingCapability` (multiple
  inheritance) — it already has every piece of state
  `CreateAssetScaffold()` needs (the active-project lookup), so a second,
  separate class would only duplicate that access for no benefit.
- `src/Editor/Panels/ProjectPanel.cpp/.h` — the existing two-pane
  Explorer-style content-asset browser (`m_rootPath` = `<exe dir>/Project`,
  a COMPLETELY DIFFERENT folder from any Project Assembly's own
  `Projects/<Name>/Assets/` source folder — this is LDD-PW3's own flagged
  refinement, already locked in by BIG-STEP 1). Its own context menu
  (`RenderContextMenu()`) uses `ImGui::BeginPopupContextWindow(popupId)`,
  called ONCE per pane (left pane, right pane), each already given its own
  distinct popup ID string. **Correction to the master-plan file**: that
  source document's own STEP 1 code sketch used
  `ImGui::BeginPopupContextItem(...)`, extrapolating from a DIFFERENT,
  unrelated precedent. The REAL, current file's own actual convention is
  `BeginPopupContextWindow`, invoked once per pane's own `BeginChild` block.
  PHASE2 designs around the REAL convention, not the sketch — see that
  phase's own "de-risking" section for exactly how, including a concrete,
  mechanical fix for a genuine ImGui double-popup hazard the master-plan
  file glossed over.
- `src/Editor/NewProjectWindow.h/.cpp` and `src/Editor/OpenProjectWindow.h/.cpp`
  — two already-shipped, real, working examples of the "on-demand floating
  ImGui window, parameterized by a capability pointer, one function two
  callers" pattern this campaign's own `CreateAssetWindow` mirrors exactly.
- `src/Editor/EditorLayer.h` (`IEditorLayer`) / `src/Editor/NullEditorLayer.cpp`
  / `src/Editor/ImGuiEditorLayer.cpp` — the exact, already-proven
  "`SetXCapability(IXCapability*)` pure virtual, called once from
  `EditorHost`'s constructor, no-op in `NullEditorLayer`, stored as a
  non-owning member in `ImGuiEditorLayer`" wiring convention
  (`SetProjectLifecycleCapability`, confirmed real at `EditorLayer.h:720`,
  `NullEditorLayer.cpp:101`, `ImGuiEditorLayer.cpp:947`). This campaign adds
  a SECOND such method, `SetAssetScaffoldingCapability`.
- `src/Network/NetworkServer.cpp`'s `RegisterRoutes()` — already takes 9
  capability/bridge pointers (confirmed: `&s_editorProjectLifecycleCapability`
  is explicitly the "ninth argument", `EditorHost.cpp:205-209`). This
  campaign adds a TENTH pointer, `IAssetScaffoldingCapability*`, pointing at
  the SAME `s_editorProjectLifecycleCapability` static object (multiple
  inheritance means one object, two independently-typed pointers to its two
  base sub-objects — both perfectly legal, ordinary C++).
- `cmake/GteProject.cmake`'s `gte_add_project()` — globs
  `Assets/*.cpp` with `CONFIGURE_DEPENDS` (line 35). This means a NEW `.cpp`
  file dropped into an ALREADY-configured, already-`add_subdirectory()`'d
  project's `Assets/` folder is auto-picked-up by the next `cmake --build`
  the SAME way BIG-STEP 2's Finding 1 already proved for a brand-new
  project FOLDER — this campaign therefore needs **no explicit CMake
  reconfigure call** anywhere in `CreateAssetScaffold()` (unlike
  `CreateNewProjectAssembly()`, which explicitly reconfigures because a whole
  new subdirectory needs a fresh `add_subdirectory()` call, a materially
  different problem). This is stated here as a DELIBERATE, load-bearing
  design decision (LDD-CA1 below), not an oversight.
- `cmake/templates/ProjectAssemblyExports.h` /
  `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` — the real, live,
  ALREADY-WORKING proof of exactly how a Project Assembly registers a render
  pass. **This is where the master-plan text file's own Kind-1 template is
  factually WRONG against the current engine, confirmed by directly reading
  `src/Renderer/RenderGraph/RenderPipeline.h`:**
  - `enum class ProviderScope { Once, PerActiveView };` — there is **no
    `ProviderScope::Game`** value. The master-plan file's Kind-1 template
    (`core.RegisterProjectRenderPassProvider("<Name>.RenderPass",
    gte::rg::ProviderScope::Game, ...)`) **will not compile**.
  - `using RenderPassProvider = std::function<void(const RenderPassFrameContext&
    frame, std::vector<RenderPassDesc>& outPasses)>;` — the provider lambda
    takes **two** parameters (the frame context AND an out-vector to append
    zero-or-more `RenderPassDesc` values into), never a single-parameter
    lambda that builds/returns one desc directly. The master-plan file's
    Kind-1 template lambda (`[](const gte::rg::RenderPassFrameContext&
    frame) { ... }`) **will not compile either** — it's missing the second
    parameter entirely.
  - PHASE1 below fixes BOTH of these, grounding the corrected template
    directly in `HelloGame.cpp`'s own real, live, currently-compiling call
    site (`core.RegisterProjectRenderPassProvider("ProjectAssemblyProbe.FillTexture",
    gte::rg::ProviderScope::Once, [&core](const gte::rg::RenderPassFrameContext&
    frame, std::vector<gte::rg::RenderPassDesc>& outPasses) { ... outPasses.push_back(std::move(desc)); });`).

**What does NOT exist yet, and this campaign must build:**
- `AssetScaffoldKind` enum + `IAssetScaffoldingCapability` interface
  (`Core/EditorCapabilities.h`).
- `EditorProjectLifecycleCapability::CreateAssetScaffold()` (the real
  filesystem-writing implementation, with the corrected templates, the
  collision check, and the "no active project" guard).
- The synthetic "[Active Project] &lt;Name&gt;" tree row + its own
  Create-submenu popup inside `ProjectPanel.cpp`.
- `IEditorLayer::SetAssetScaffoldingCapability()` (+ `NullEditorLayer`
  no-op + `ImGuiEditorLayer` storage).
- `src/Editor/CreateAssetWindow.h/.cpp` (mirrors `NewProjectWindow`).
- `POST /project_assembly/create_asset` (`NetworkServer.cpp`), the tenth
  constructor pointer, and `EditorHost.cpp`'s wiring of it.
- New `EditorContext.h` fields for the window's open/kind state.
- Unit tests (template generation, collision logic) + a new
  `tests/Network/CreateAssetEndpointEndToEndTests.cpp`.

## STEP 3 — THE PLAN (the detailed, phase-by-phase strategy)

Four child phases, strictly ordered (each depends on the previous one
compiling):

```
PHASE0 (this file)
   |
   v
PHASE1 — Capability interface + corrected templates + collision logic
   (Core/EditorCapabilities.h, EditorProjectLifecycleCapability.h/.cpp,
   unit tests. No UI, no HTTP, no ProjectPanel change yet.)
   |
   v
PHASE2 — ProjectPanel.cpp synthetic row + Create submenu
   (Depends on PHASE1 only for the AssetScaffoldKind enum forward-decl in
   EditorContext.h — does NOT call CreateAssetScaffold() directly, only
   sets EditorContext fields consumed by PHASE3's own window. Pure ImGui
   changes to ONE existing file + EditorContext.h.)
   |
   v
PHASE3 — IEditorLayer wiring + CreateAssetWindow + HTTP route
   (Depends on PHASE1's capability + PHASE2's EditorContext fields.
   NetworkServer.cpp/.h, EditorHost.cpp, EditorLayer.h, NullEditorLayer.cpp,
   ImGuiEditorLayer.cpp, new CreateAssetWindow.h/.cpp.)
   |
   v
PHASE4 — Tests, live verification, and campaign closeout
   (End-to-end HTTP tests, the load-bearing "two render passes in one
   project both compile cleanly" live proof, full build + full ctest,
   CAMPAIGN_COMPLETION_REPORT.md.)
```

Why no separate "cross-thread bridge" phase (unlike BIG-STEP 3's PHASE2):
`CreateAssetScaffold()` is pure filesystem I/O (write text files, check for
existing paths) plus, per LDD-CA1, a plain in-process CMake glob re-scan
that already happens for free on the NEXT build — it never touches live
`Core`/`Registry`/GPU state the way loading a `.dll` does. It is therefore
safe to call from ANY thread with no bridge at all, exactly like
`CreateNewProjectAssembly()` already is (confirmed by that method's own doc
comment, `EditorCapabilities.h`). This mirrors the master-plan file's own
Step 4 statement almost verbatim.

### Locked Design Decisions for this campaign (LDD-CA#, restated by every
child phase, never re-litigated)

- **LDD-CA1 — No explicit CMake reconfigure after scaffolding.**
  `gte_add_project()`'s own `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` over
  `Assets/*.cpp` (and `gte_add_project_shaders()`'s own equivalent glob over
  `*.vert`/`*.frag`/`*.comp`) already re-triggers CMake's configure step
  automatically on the next `cmake --build` the moment a new matching file
  appears on disk — this is standard, already-relied-upon CMake behavior
  (CMake ≥ 3.12), not a new assumption this campaign introduces. Unlike
  BIG-STEP 2's `CreateNewProjectAssembly()` (which reconfigures because
  `add_subdirectory()` itself, at the ROOT `CMakeLists.txt` level, must run
  for a brand-new PROJECT folder — a materially different, one-time,
  per-project problem), scaffolding one more file into an ALREADY-configured
  project's `Assets/` needs nothing extra. PHASE4's live verification
  proves this mechanically (compile after scaffold, no manual reconfigure
  step performed anywhere in that test).
- **LDD-CA2 — Debug/provider names stay PascalCase, matching `HelloGame.cpp`'s
  own real, live convention exactly** (`"ProjectAssemblyProbe.FillTexture"`)
  — e.g. `"<Name>.RenderPass"`, `"<Name>.ComputePass"`. The master-plan
  file's own STEP 4, item 4 vaguely suggested a lower-case "GLSL-friendly"
  variant for "the texture/shader debug-name string" without ever using one
  in its own templates — this campaign deliberately does NOT invent a
  second, lower-cased naming convention nobody asked for; one PascalCase
  convention, matching the one real working example in this repo, is used
  everywhere.
- **LDD-CA3 — The synthetic tree row's children are DISPLAY-ONLY, never
  independently selectable/navigable/drag-droppable.** The master-plan
  file's own Non-Goals explicitly scope this whole feature as "Create-only"
  for the new subtree. PHASE2 leans into this explicitly: the new row's own
  one-level file listing is plain, static `ImGui::Text`/`ImGui::BulletText`
  rows — NOT `ProjectEntry`/`m_tree`/`m_currentFolderRelativePath` reuse,
  and NOT wired into `Selection`/Inspector/drag-and-drop at all. This is a
  deliberate, disclosed SIMPLIFICATION versus the master-plan file's own
  slightly more ambiguous wording ("its children are... real folder
  contents") — it never said those children needed to be clickable, and
  giving them full navigation semantics would require unifying TWO
  independent filesystem roots (`<exe dir>/Project` vs. a Project Assembly's
  own `Assets/`) under ONE `m_currentFolderRelativePath`/`m_tree` addressing
  scheme, which is a real, unnecessary, high-risk rewrite of an
  already-working, heavily-relied-upon panel for zero requested benefit.
  The Create-submenu context menu attaches to the SYNTHETIC ROOT ROW only,
  never to an individual child file row (right-clicking a specific
  already-scaffolded file is explicitly out of scope, matching Non-Goals:
  "does NOT support deleting/renaming a scaffolded file").
- **LDD-CA4 — One new capability class extension, not a new class.**
  `EditorProjectLifecycleCapability` gains a second base,
  `IAssetScaffoldingCapability`, rather than a new sibling class — see
  STEP 2 above for why (it already owns everything `CreateAssetScaffold()`
  needs).

### Risk register (every real hazard found, and this plan's answer to it)

1. **The "one `GTE_RegisterProject` per target" linker hazard** (master-plan
   file's own STEP 2, restated, LOUD, CORRECT, and fully carried through
   here) — solved structurally: every generated `.cpp` exposes a plain,
   NON-exported, ordinarily-named free function
   (`Register<Name>RenderPass`/`Register<Name>ComputePass`), never its own
   `GTE_DEFINE_PROJECT_EXPORTS_GAME(...)` call. PHASE1's templates enforce
   this; PHASE4's live verification mechanically proves two independently
   scaffolded-and-wired passes compile together without a duplicate-symbol
   error.
2. **The `ProviderScope`/`RenderPassProvider` signature mismatch** (found by
   this phase, Section 2 above) — fixed in PHASE1's corrected templates.
3. **The ImGui double-popup hazard** (found by this phase, Section 2 above)
   — PHASE2 gives a concrete, mechanical, testable fix (a per-frame
   suppression flag), not just an assertion that two differently-named
   popup IDs "never open on top of each other by accident".
4. **`EditorContext.h` bloat** — `EditorContext.h` is included by nearly
   every panel in this codebase. PHASE2 forward-declares
   `enum class AssetScaffoldKind;` inside `EditorContext.h` instead of
   `#include`-ing the whole of `Core/EditorCapabilities.h` (which itself
   pulls in `Logging.h`, `<filesystem>`, etc.) — legal or a plain C++11
   scoped enum with the implicit default `int` underlying type on both the
   forward declaration and the real definition (`Core/EditorCapabilities.h`),
   confirmed by PHASE1 declaring it with no explicit underlying type on
   either side.
5. **Case-insensitive collision checking on a case-sensitive-looking
   Windows filesystem** — Windows' own filesystem is case-INSENSITIVE for
   collision purposes (`"Foo.comp"`/`"foo.comp"` are the same file) — PHASE1
   implements the collision check with an explicit ASCII-lowercase
   comparison, never relying on `std::filesystem::exists()` alone with the
   literal typed case (which, on this platform, would ALSO happen to
   collide correctly today — but PHASE1 makes this explicit/intentional
   rather than an accidental side effect of the OS, so the logic reads
   correctly and is unit-testable independent of any real filesystem).

### Definition of Done (mechanical, checkable, matches master-plan STEP 6)

- [ ] With NO active project set, `POST /project_assembly/create_asset`
      (any `kind`) returns `400` with a "no active project" message, and the
      Editor shows NO synthetic "[Active Project]" row/Create submenu at
      all.
- [ ] Create (or Open) a real project; scaffold ONE of each of the 3 kinds
      via HTTP; every file in the response's `created_files` genuinely
      exists on disk, byte-for-byte matching PHASE1's template
      (substitutions applied correctly).
- [ ] Repeating any ONE kind with the SAME name (including a different-case
      variant) is rejected, case-insensitively, zero files touched.
- [ ] **The load-bearing proof**: scaffold a Render Pass, manually wire its
      one call line into `RegisterProject()`, Compile — succeeds cleanly.
      Scaffold a SECOND Render Pass into the SAME project, also wired by
      hand — ALSO compiles cleanly (no duplicate-`GTE_RegisterProject`
      linker error).
- [ ] ImGui right-click path, HTTP path, and duplicate/collision path each
      exercised at least once, live, against a real running
      `GreatTamanaEditor.exe` (via `gte_send_request`/screenshots and/or
      direct HTTP calls).
- [ ] Full clean build + full `ctest -C Debug --output-on-failure` — zero
      regressions, only newly-explained skips (if any).
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` written (PHASE4's own job), same
      shape/style as `editor-core-separation-16`/`-17`'s own.

Reading order for whoever implements this: this file, then `PHASE1_*.md`,
then `PHASE2_*.md`, then `PHASE3_*.md`, then `PHASE4_*.md`.
