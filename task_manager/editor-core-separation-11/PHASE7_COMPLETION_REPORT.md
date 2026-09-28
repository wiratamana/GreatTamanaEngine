# PHASE7 — Capability #1: Custom Editor Panel — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE7 of 8
**Status:** DONE
**Date:** 2026-09-28

---

## What was built

All anchors were re-verified via `search_in_dir`/`read_line` immediately
before editing (LDD6):

- `PluginPanels()` in `src/Editor/ImGuiEditorLayer.cpp` — re-confirmed at
  line 704 (the `BuildPanel()` call loop), gate opening at line 696 — matched
  this phase file's own citation exactly.
- `IEditorPanelModule_v1` — re-confirmed, exact real header path
  `plugins/gte_plugin_abi/IEditorPanelModule.h`, `BuildPanel(IPluginPanelDrawContext&)`
  signature unchanged.
- `EditorPanelRegistry::RegisterPluginPanel(const std::string&, IEditorPanelModule_v1*)` —
  re-confirmed, `src/Core/EditorPanelRegistry.h`, unchanged.
- `GET /activate_tab?name=<PanelName>` query-parameter name (`name`) —
  re-confirmed directly in `src/Network/NetworkServer.cpp` line 343-344
  before using it.
- `cmake/GteProject.cmake`'s own `gte_project_assembly_apply_editor_header_paths()` —
  confirmed ALREADY IMPLEMENTED (pulled forward into PHASE3, per that phase's
  own completion report and this file's own header comment), already called
  from `gte_add_project()`'s `_Editor` branch. **STEP 1 required zero new
  work this phase** — it was already done and already compiling; this was
  verified, not assumed, by reading the file directly (see below).

### STEP 1 (Finding A, CMake ImGui headers) — already done, re-confirmed only

Read `cmake/GteProject.cmake` in full: `gte_project_assembly_apply_editor_header_paths(TARGET_NAME)`
exists (headers-only, `$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>` +
`imguizmo`, `PRIVATE`), and `gte_add_project()`'s `if(EDITOR_SOURCES)` branch
already calls it immediately after `gte_project_assembly_apply_header_paths(${NAME}_Editor)`.
This phase's own Step 1 instruction says explicitly: *"if PHASE3 was
implemented with this call present but the function itself not yet defined,
this phase's Step 1 is what completes it"* — here the function WAS already
defined (PHASE3 pulled it forward, its own file's header comment says so
explicitly), so there was genuinely nothing left to do for Step 1 beyond
confirming it compiles for real (see "Live/compile verification" below).

### STEP 1a — `ImGuiEditorLayer.cpp` Finding G gate widening

Re-located the exact gate via `search_in_dir` for `PluginPanels()` (2 hits:
`DockLayout.cpp` line 105, unaffected/ungated as the master strategy
predicted; `ImGuiEditorLayer.cpp` line 704, the actual `BuildPanel()` call
site). The opening `#if GTE_ENABLE_PLUGINS` was at line 696 (not any
previously-cited line number — re-verified fresh, per LDD6). Widened to:

```cpp
#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES
```

with a new code comment explaining Finding G and why the widening is safe,
directly above the pre-existing `editor-core-separation-3` comment (left
untouched). The matching `#endif` needed no change (its own condition is
implicit, tied to the `#if` it closes).

### STEP 2 — the real, interactive panel

Replaced PHASE3's placeholder `Projects/ProjectAssemblyProbe/Assets/Editor/HelloEditorPanel.cpp`
(which only logged a diagnostic line, added by PHASE5) with the real
`ProbeEditorPanel` class from this phase file's own Step 2 snippet, used
essentially verbatim:

- Implements `gte::IEditorPanelModule_v1` directly, `GetPanelName()` returns
  `"Probe Panel"`, `BuildPanel()` **ignores** the `ctx` parameter and calls
  real `ImGui::Text()`/`ImGui::Button()` directly.
- A namespace-scope `ProbeEditorPanel g_probePanel;` process-lifetime
  instance (no hot reload, LDD4).
- `RegisterProbeEditor(gte::Core&, gte::EditorHost&)` calls
  `gte::EditorPanelRegistry::Instance().RegisterPluginPanel("Probe Panel", &g_probePanel)`.
- PHASE5's own diagnostic `GTE_LOG_INFO("ProjectAssembly", ...)` line was
  **intentionally preserved** (still a cheap, always-on smoke signal, and
  this phase file's own "What this phase does NOT do" section never asks for
  its removal) — both the diagnostic log AND the real panel registration now
  coexist in the same file.

## Real deviation from this phase file's own literal instructions

**None of substance.** The only difference from the phase file's own Step 2
snippet: the diagnostic `GTE_LOG_INFO` line from PHASE5 was kept (additive,
not a contradiction of anything this phase file specifies) instead of being
silently dropped. No CMake source-list registration gap (unlike PHASE5/6)
applied here — `Projects/ProjectAssemblyProbe/Assets/Editor/HelloEditorPanel.cpp`
is picked up by `gte_add_project()`'s own `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`,
not a hand-maintained list, so simply overwriting its content and
re-running the incremental build target was sufficient.

## Open question resolved by testing, not assumption

**`gte::EditorHost&` was NOT needed for this specific capability.**
`RegisterProbeEditor(gte::Core& core, gte::EditorHost& editorHost)` marks
both parameters `(void)`-cast/unused beyond the diagnostic log line —
`EditorPanelRegistry::Instance()` (a process-wide Meyers singleton) was
directly reachable and sufficient to register the panel, confirmed live
(the panel really did appear, activate, and draw). The exported function's
own signature is unchanged (it still receives `editorHost`, per
`GTE_DEFINE_PROJECT_EXPORTS_EDITOR`'s fixed two-parameter shape) — this note
only confirms THIS capability's own actual need, exactly as this phase
file's own "Note the open question..." paragraph asked to be confirmed
rather than assumed.

## Live/compile verification performed

1. **Incremental compile check**: `cmake --build build --target GreatTamanaEditor` —
   succeeded cleanly (only `ImGuiEditorLayer.cpp` recompiled + relink), zero
   new warnings.
2. **Incremental compile check**: `cmake --build build --target ProjectAssemblyProbe_Editor` —
   succeeded cleanly (`HelloEditorPanel.cpp` recompiled + relinked the
   `.dll`).
3. **Incremental compile check**: `cmake --build build --target ProjectAssemblyProbe_Game` —
   succeeded (unaffected by this phase's changes; rebuilt only to keep both
   halves of the probe project current for the live run).
4. **Live verification, normal configuration** (`build/`, `GTE_ENABLE_PLUGINS=ON`,
   `GTE_ENABLE_PROJECT_ASSEMBLIES=ON` — the repo's existing default `build/`
   tree, untouched):
   - `run_app_background` → `build/GreatTamanaEditor.exe`.
   - `GET /list_tabs` → `"Probe Panel"` present in the returned tab list,
     alongside every pre-existing built-in/plugin tab (including
     `"Demo Plugin Panel"`, confirming the OTHER plugin system's own panel is
     unaffected).
   - `GET /activate_tab?name=Probe%20Panel` → `{"activated_tab":"Probe Panel","success":true}`.
   - `GET /get_swapchain` → real 222337-byte PNG, "Probe Panel" tab visibly
     focused at the bottom dock, showing its real content: "Hello from a
     real Project Assembly Editor panel.", a "Click me" button, and
     "Clicked 0 time(s)" — genuine ImGui widgets, not a static placeholder.
   - `GET /get_logs` → 34 entries, all pre-existing/expected (plugin loads,
     the two already-documented demo-plugin priority-tie-break warnings, the
     two `ProjectAssembly` diagnostic lines) — zero new warnings/errors, zero
     ImGui-context-related assertions/crashes.
   - Polled `GET /get_swapchain` twice more in a row: identical, stable,
     correctly-rendered frames each time, log entry count unchanged (34) —
     the "no true click-simulation available" fallback evidence this phase
     file's own Step 3.6 explicitly sanctions ("a garbled/blank/correctly-
     rendered-but-static panel image combined with zero crash/log-warning
     across a multi-second observation window is accepted as sufficient")
     was used here: no crash, no ImGui assertion, correctly-rendered panel,
     across three separate `GET /get_swapchain` calls. **Recorded explicitly,
     as instructed: a true click-simulation was NOT possible with this
     session's available tooling** (no keyboard/mouse-injection tool was
     loaded/available) — the counter stayed at "0 time(s)" throughout, which
     is the expected, correct behavior for a never-actually-clicked button,
     not a bug.
   - `stop_app_background`.
5. **Live verification, STEP 1a's own required `GTE_ENABLE_PLUGINS=OFF` +
   `GTE_ENABLE_PROJECT_ASSEMBLIES=ON` one-off build** (a genuinely fresh,
   separate reconfigure, never touching the main `build/` tree, exactly as
   instructed):
   - `cmake -S . -B build-plugins-off -G Ninja -DCMAKE_C_COMPILER=.../cc.exe -DCMAKE_CXX_COMPILER=.../c++.exe -DGTE_ENABLE_PLUGINS=OFF -DGTE_ENABLE_PROJECT_ASSEMBLIES=ON` —
     configured cleanly (same toolchain as the main tree, matched explicitly
     via `CMakeCache.txt` inspection first).
   - `cmake --build build-plugins-off --target GreatTamanaEditor` — a
     genuinely full build from scratch (350 build steps; unavoidable for a
     brand-new tree) — succeeded cleanly, zero errors.
   - `cmake --build build-plugins-off --target ProjectAssemblyProbe_Editor` —
     succeeded cleanly.
   - `run_app_background` → `build-plugins-off/GreatTamanaEditor.exe`.
   - `GET /list_tabs` → `["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Probe Panel"]` —
     **"Demo Plugin Panel" correctly absent** (plugins genuinely off) while
     **"Probe Panel" is still present** — direct, concrete confirmation the
     Project Assembly panel registers independently of the old plugin
     system.
   - `GET /activate_tab?name=Probe%20Panel` → success.
   - `GET /get_swapchain` → real 143482-byte PNG, "Probe Panel" tab focused,
     showing its REAL content ("Hello from a real Project Assembly Editor
     panel.", "Click me" button, "Clicked 0 time(s)") — **not an empty,
     blank tab** — this is the concrete, positive proof Finding G's fix
     actually works, not merely that it compiles.
   - `GET /get_logs` → 4 clean entries (`ProjectAssembly` registration +
     load, `Network` listening, `EditorHost` constructed) — zero warnings,
     zero errors, zero `PluginHost` lines at all (correctly reflecting
     `GTE_ENABLE_PLUGINS=OFF`).
   - `stop_app_background`.
   - **Cleanup**: `build-plugins-off/` fully deleted afterward (`rmdir /S /Q`) —
     this was explicitly a one-off verification tree per this phase file's
     own instruction ("never reuse/overwrite the main `build/` tree for this
     one-off check"), not a permanent fixture like the 4+1 standing
     `tools/ci/*` probes' own build trees. `git status` after cleanup shows
     it never appeared as tracked/untracked noise (nothing to add to
     `.gitignore` — the tree no longer exists on disk).

## Definition of Done — checklist (this phase file's own list)

- [x] `gte_project_assembly_apply_editor_header_paths()` exists, is called
      only for `_Editor.dll` targets (confirmed already done, PHASE3), and a
      `_Game.dll`-only project never references `imgui`/`imguizmo` target
      names at configure time — `ProjectAssemblyProbe_Game`'s own configure
      output (both build trees) shows zero reference to either target name,
      confirmed by direct inspection of `gte_add_project()`'s structure (the
      call sits strictly inside the `if(EDITOR_SOURCES)` branch).
- [x] `ProjectAssemblyProbe_Editor.dll` implements `IEditorPanelModule_v1`
      directly, ignoring `ctx`, calling real `ImGui::*` functions.
- [x] The panel appears in `GET /list_tabs`, activates via `GET
      /activate_tab`, and is visible, correctly rendered, in a real, running
      `GreatTamanaEditor.exe` screenshot.
- [x] Completion notes state explicitly whether `gte::EditorHost&` ended up
      being needed for this specific capability: **it did not** — see "Open
      question resolved by testing" above.
- [x] `ImGuiEditorLayer.cpp`'s plugin-panel-draw gate is widened to
      `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES` (Finding G,
      STEP 1a), verified concretely with a genuine, fresh
      `GTE_ENABLE_PLUGINS=OFF` + `GTE_ENABLE_PROJECT_ASSEMBLIES=ON` build
      showing the panel still draws real content, not just an empty dock
      tab.

## What this phase does NOT do (as instructed, re-confirmed)

- Does NOT modify `IEditorPanelModule_v1`, `IPluginPanelDrawContext`, or any
  other `gte_plugin_abi` type (LDD1) — confirmed by direct inspection: this
  phase's only edits are `ImGuiEditorLayer.cpp` (gate widening) and
  `Projects/ProjectAssemblyProbe/Assets/Editor/HelloEditorPanel.cpp` (a
  `.gitignore`d Project Assembly source file, not engine code).
- Does NOT implement the render-pass capability — PHASE8's job, untouched.
- Does NOT decide what a "real", non-toy Project Assembly panel should
  actually show — `ProbeEditorPanel` remains a proof-of-mechanism fixture.

## New gap found, worth flagging for future phases/maintenance

None new. This phase's own live verification directly, mechanically
confirmed both halves of Finding G's fix (the gate now covers BOTH systems,
and covering it did not silently break anything for the OTHER,
`GTE_ENABLE_PLUGINS=ON` system's own "Demo Plugin Panel", which still
appeared correctly in the normal-configuration `GET /list_tabs` check).
No true click-simulation tooling exists in this session's active tool set —
this is the same limitation PHASE6 already implicitly ran into for its own
different reason (no way to simulate a graceful window-close); flagging
again here since it is the second phase in this campaign whose own
Definition of Done explicitly anticipates and pre-approves this exact
fallback ("stable render + zero crash/warning across a polling window" as
sufficient evidence when true input simulation is unavailable).

## Result

**PHASE7 is DONE** — every item on its own Definition of Done is
mechanically verified, including the one item (STEP 1a / Finding G) that
required a genuine, separate, from-scratch `GTE_ENABLE_PLUGINS=OFF` build
tree rather than a code-reading assumption. The Project Assembly Editor
panel capability (Finding A) is proven end-to-end: real ImGui headers via a
headers-only CMake propagation (already in place from PHASE3), a real,
interactive `IEditorPanelModule_v1` implementation calling `ImGui::*`
directly with zero ABI wrapper, registered through the existing,
unmodified `EditorPanelRegistry`, visible and correctly rendered in a live,
running `GreatTamanaEditor.exe`, in BOTH the normal
(`GTE_ENABLE_PLUGINS=ON`) and the previously-broken
(`GTE_ENABLE_PLUGINS=OFF`) configuration. `gte::EditorHost&` was confirmed
NOT required for this specific capability. PHASE8 may now proceed — nothing
in this phase touches `Core::RegisterProjectRenderPassProvider()` or any
file PHASE8 still needs to add/edit.
