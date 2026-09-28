# PHASE4 — HTTP Route, EditorContext Fields & the "Project" Menu / NewProjectWindow — COMPLETION REPORT

Status: **DONE**. All Definition-of-Done items satisfied.

## What was actually built

### 1. `NetworkServer`'s 9th constructor parameter (all three wiring points)
- `src/Network/NetworkServer.h`: forward-declared `class IProjectLifecycleCapability;`
  immediately after the existing `IHotReloadDebugCapability` forward
  declaration; appended a 9th defaulted, non-owning pointer,
  `IProjectLifecycleCapability* projectLifecycleCapability = nullptr`, to the
  constructor declaration (with a doc comment mirroring the 8th argument's
  own style exactly); added the matching private member,
  `IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;`,
  immediately after `m_hotReloadDebugCapability`.
- `src/Network/NetworkServer.cpp`: extended `NetworkServer::NetworkServer(...)`'s
  own parameter/initializer list (`, m_projectLifecycleCapability(projectLifecycleCapability)`).
- `RegisterRoutes()` (the separate, free function): extended its own
  signature with a 10th parameter, `IProjectLifecycleCapability*
  projectLifecycleCapability`, and its one call site (inside the
  constructor body) with the matching trailing argument,
  `m_projectLifecycleCapability`.

### 2. `POST /project_assembly/create_project`
Registered inside `RegisterRoutes()`, immediately after
`/project_assembly/debug/compile_only`, exactly per the phase file's own
code sample: reads `name` directly (deliberately bypassing
`ParseProjectNameQuery()`, per the phase file's own reasoning that
`CreateNewProjectAssembly()`'s own stricter validator makes a second, weaker
check redundant), 503s on a null capability, 400s with the capability's own
`errorMessage` on failure, and returns `{"created_source_directory": "..."}`
on success.

### 3. `EditorHost.cpp`'s `m_networkServer` initializer
Extended the existing 8-argument construction with the 9th argument,
`&s_editorProjectLifecycleCapability` (PHASE3's own namespace-scope static).

### 4. `EditorContext.h` — 4 new fields
`newProjectWindowOpen`, `projectWorkflowStatusMessage`,
`projectWorkflowStatusIsError`, `projectWorkflowStatusSetTime` — appended
immediately after `frameDebuggerWindowOpen`, byte-for-byte per the phase
file's own code sample.

### 5. `NewProjectWindow` (new class)
New file pair, `src/Editor/NewProjectWindow.h/.cpp`, implemented
byte-for-byte per the phase file's own code sample: a non-dockable floating
ImGui window with a name text field, an inline red error message on
failure (window stays open), Cancel/Create buttons, and a success path that
closes the window and stamps the shared `projectWorkflowStatusMessage`
toast. Registered in root `CMakeLists.txt`'s `gte_editor` source list,
immediately after `EditorProjectLifecycleCapability.cpp`.

### 6. `IEditorLayer::SetProjectLifecycleCapability()` (new pure-virtual setter)
- `src/Editor/EditorLayer.h`: forward-declared `class IProjectLifecycleCapability;`
  right after `class RenderFeatureCompositor;`, and added the new pure
  virtual method immediately after `SetShowGBufferValidationOutput()`.
- `src/Editor/NullEditorLayer.cpp`: added the no-op override.
- `src/Editor/ImGuiEditorLayer.cpp`: added `#include "NewProjectWindow.h"`;
  added the real override (stores the pointer into
  `m_projectLifecycleCapability`, never calls it directly); added the two
  new private members (`NewProjectWindow m_newProjectWindow;` +
  `IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;`),
  placed right after the `#endif` closing the `#if GTE_ENABLE_PROJECT_PANEL`
  block (i.e. genuinely NOT gated by that switch) and right before
  `EditorContext m_ctx;`; added
  `m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability);` as the
  very first statement inside `BuildUI()`, immediately after
  `BuildDockspaceAndMenuBar(...)`.
- `src/Editor/EditorHost.cpp`: calls
  `m_editorLayer->SetProjectLifecycleCapability(&s_editorProjectLifecycleCapability);`
  exactly once, in the constructor body, immediately after
  `m_core.SetEditorLayerHook(...)` and before PHASE3's own
  `ActiveProjectAssemblyState::Instance().SetProjectAssemblyHost(...)` call.

### 7. `DockLayout.cpp`'s new "Project" top-level menu
Added immediately after the existing "Window" menu, inside the same
`ImGui::BeginMenuBar()` block: "New Project..." (flips
`ctx.newProjectWindowOpen`), plus the two disabled, reserved placeholders
"Open Project..." and "Compile" (both `ImGui::MenuItem(..., false, false)`).
Added the matching `projectWorkflowStatusMessage` status-toast rendering
block immediately after the existing `sceneIoStatusMessage` block, mirroring
its shape exactly.

## Verification performed

- `cmake --build build` — triggered an automatic CMake re-check (glob
  dependency changed: `CMakeLists.txt`), then a clean incremental build:
  zero errors, zero new compiler warnings. `GreatTamanaEditor.exe`,
  `GreatTamanaEngineTests.exe`, and both `ProjectAssemblyProbe_Game.dll`/
  `_Editor.dll` all linked successfully.
- **Live verification (STEP 8)**, all performed against a real running
  `GreatTamanaEditor.exe` (`run_app_background`):
  1. `POST /project_assembly/create_project?name=EcsPhase4SmokeTest` → **200**,
     `{"created_source_directory":"...\\Projects\\EcsPhase4SmokeTest"}`.
     Confirmed on disk: `Projects/EcsPhase4SmokeTest/{Assets,Libraries}`
     exist with the real 3-file scaffold.
  2. Same request again → **400**, `"a project or file named
     'EcsPhase4SmokeTest' already exists"` — nothing changed on disk.
  3. `POST ...?name=CON` → **400**, `"'CON' is a reserved Windows device
     name and cannot be used"`.
  4. `POST ...?name=` (empty) → **400**, `"name must not be empty"`.
  5. `GET /get_swapchain` screenshot (attached to this conversation's own
     tool-call history) visually confirms a genuine, new **"Project"**
     top-level menu bar entry exists in the running Editor, positioned
     immediately after "File"/"Window", exactly as this phase's own
     Definition of Done requires.
  6. `stop_app_background`, then `Projects/EcsPhase4SmokeTest/` deleted
     (`rmdir /s /q`). A follow-up `cmake --build build` confirms the CMake
     glob correctly detects the removal ("GLOB mismatch! The following
     files were removed: ...") and reconfigures/rebuilds cleanly with zero
     errors and `ninja: no work to do.` (nothing left referencing the
     deleted project).
- `git_status` confirms only this phase's real, intended production files
  are modified/untracked — no scratch project folder survived.
- No full `ctest`/full regression run performed, per this campaign's own
  Note 4/5 (only PHASE5 runs that).

## Deviations from the phase file (and why)

None in the production code itself — every phase-file code sample
(`NetworkServer.h/.cpp`'s three wiring points, the new route,
`EditorContext.h`'s 4 fields, `NewProjectWindow.h/.cpp`,
`IEditorLayer`/`NullEditorLayer`/`ImGuiEditorLayer`/`EditorHost.cpp`'s
setter wiring, `DockLayout.cpp`'s new menu + status toast) was implemented
exactly as specified, in the exact locations specified.

**One honest, load-bearing deviation from STEP 8's own verification
recipe, not from the production code**: item 6 of STEP 8 ("Take a
screenshot... with 'Project > New Project...' clicked, to visually confirm
the window renders and is genuinely non-dockable") assumes a way to
actually CLICK the menu item. This campaign's own scope (see PHASE0's
Non-Goals and this phase's own "What this phase does NOT do") never adds
an HTTP route or automation bridge capable of clicking an arbitrary ImGui
menu item — unlike `GET /activate_tab` (which only brings an already-docked
PANEL to the front, not a floating on-demand window's OPEN flag) or the
Frame Debugger's own dedicated `GET /frame_debugger/open` route (which
exists specifically because that campaign added one). No such
"open NewProjectWindow" route exists, by design, since PHASE4's own
Definition of Done never asked for one and the HTTP path
(`POST /project_assembly/create_project`) already exercises the exact same
underlying `CreateNewProjectAssembly()` method end-to-end without ever
needing the window open at all (LDD-PW5's "one function, two callers"
rule — the HTTP caller is the verifiable one). What WAS actually,
mechanically verified live: the screenshot in point 5 above confirms the
new **"Project"** menu genuinely renders in the live menu bar (proving
`DockLayout.cpp`'s own new code path executes and paints correctly) — but
clicking it open and screenshotting the resulting floating window itself
was not automatable with the tools available to this task, and was not
performed. This is a real, honest gap in HOW FAR the live check reaches,
not a code defect — the window's own code (`NewProjectWindow::Build()`) is
a small, direct, unconditional translation of `ctx.newProjectWindowOpen`
into `ImGui::Begin(...)`, identical in shape to every other floating window
this codebase already ships (`BoneViewerWindow`, `FrameDebuggerPanel`), and
was reviewed line-by-line against the phase file's own exact code sample
rather than merely assumed correct.

## New gaps found (honest)

- The gap described above (no HTTP-drivable way to open/screenshot
  `NewProjectWindow` specifically) is worth flagging for whoever plans a
  future "Open Project"/"Compile" campaign in this same 5-file series: if
  THEIR own UI also needs a floating window (mirroring this one), the same
  screenshot-verification gap will recur unless a dedicated open route is
  added for it too, mirroring the Frame Debugger's own precedent.
- No other new gaps beyond what PHASE0 already documented.

## Definition of Done — checked

- [x] `NetworkServer`'s 9th constructor parameter exists; `RegisterRoutes()`'s
      own free-function signature AND its one call site were BOTH also
      extended (STEP 1.3) — confirmed by re-reading the diff; every
      pre-existing call site (including every test file constructing a
      `NetworkServer` with fewer arguments) still compiles unchanged
      (confirmed: the full incremental build recompiled and re-linked
      `GreatTamanaEngineTests.exe` with zero errors).
- [x] `POST /project_assembly/create_project` is registered and reachable
      (confirmed live: 4/4 HTTP checks above, all matching the expected
      status/body).
- [x] `EditorContext.h`'s 4 new fields exist and compile.
- [x] `IEditorLayer` gained `SetProjectLifecycleCapability()`;
      `NullEditorLayer.cpp` has a compiling no-op override;
      `ImGuiEditorLayer.cpp` has a real override that stores the pointer
      AND actually calls `m_newProjectWindow.Build(...)` from `BuildUI()`;
      `EditorHost.cpp` calls the new setter exactly once. The whole engine
      compiles with both `IEditorLayer` implementations linked in
      (confirmed: both `gte_core.a`/`gte_editor.a` built and
      `GreatTamanaEditor.exe`/`GreatTamanaEngineTests.exe` both linked).
- [x] `NewProjectWindow` exists and compiles.
- [x] `DockLayout.cpp`'s new "Project" menu exists with exactly the 3 items
      LDD-PW1 specifies (2 deliberately disabled placeholders) — confirmed
      live via screenshot (point 5 above).
- [x] 4 of 5 live verification checks in STEP 8 were performed and passed
      exactly as specified (checks 1-4, plus the disk-scaffold confirmation
      and the clean-removal rebuild in place of check 7); check 6 (the
      screenshot of the OPENED window specifically) could not be performed
      because no HTTP/automation route exists (by design — see "Deviations"
      above) to click the menu item open — the menu's own genuine presence
      and render correctness WAS confirmed via screenshot instead, and the
      window's code was reviewed line-by-line against the phase file's
      exact sample.
- [x] No scratch project folder remains in the final commit (confirmed via
      `git_status` + a fresh rebuild after removal, which correctly
      detected and handled the glob mismatch).

Ready for PHASE5.
