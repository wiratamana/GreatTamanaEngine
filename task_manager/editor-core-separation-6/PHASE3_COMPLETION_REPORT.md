# PHASE3 — Editor Panel Wiring Migration onto `IPluginCapabilityOrchestrator` — COMPLETION REPORT

**Status: DONE.** Implemented exactly as written in
`PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md`. No deviation from the plan.

## What changed

1. **`src/Editor/EditorHost.cpp` constructor reordered (Step 3.1 / Locked
   Design Decision #6).** The block of 10 (11 with `GTE_ENABLE_PROJECT_PANEL`)
   `EditorPanelRegistry::Instance().RegisterBuiltinPanelName(...)` calls
   (formerly lines ~202-222, AFTER the `#if GTE_ENABLE_PLUGINS ...
   m_core.LoadPlugins(...) ... #endif` block) now runs BEFORE it — a pure
   reordering, same 10/11 lines, same order, same text, only its position in
   the file changed. This preserves `EditorPanelRegistry.h`'s own documented
   invariant ("`AllNames()` always lists every built-in panel first, plugins
   after") now that plugin-panel registration happens automatically INSIDE
   `Core::LoadPlugins()` (see point 3 below) instead of via a separate,
   textually-later loop in this same function.

2. **New files** `src/Core/Plugins/EditorPanelCapabilityOrchestrator.h`/`.cpp`
   — `EditorPanelCapabilityOrchestrator final : public
   IPluginCapabilityOrchestrator`, overriding ONLY `OnPluginsLoaded()` (the
   interface's default no-op `ContributeRenderGraphPasses()` is correct and
   left untouched — editor panels are drawn through Dear ImGui, never through
   the render graph). Its body is a VERBATIM relocation of `EditorHost.cpp`'s
   own former inline `IEditorPanelModule_v1` discovery loop
   (editor-core-separation-3, PHASE4): `for (IPluginModule* module :
   modules) { if (auto* panel = static_cast<IEditorPanelModule_v1*>(
   module->QueryCapability(kIEditorPanelModule_v1_Name))) {
   EditorPanelRegistry::Instance().RegisterPluginPanel(panel->GetPanelName(),
   panel); } }` — same `QueryCapability()` name string, same
   `RegisterPluginPanel()` call, byte-for-byte, with `modules` now this
   method's own real parameter (the exact same underlying vector,
   `m_pluginHost.AllLoadedModules()`, passed in by `Core::LoadPlugins()`,
   PHASE2 Step 3.4) instead of a locally-captured `m_core.GetPluginHost().
   AllLoadedModules()`.

3. **`Core.h`/`Core.cpp` wiring** (no `Core.h` change needed — the plan's own
   Step 3.3 only touches `Core.cpp`, and that held true):
   - `Core.cpp`: added `#include "Plugins/EditorPanelCapabilityOrchestrator.h"`
     (right after the existing `Plugins/LegacyRenderFeatureOrchestrator.h`
     include).
     `RegisterBuiltinCapabilityOrchestrators()` gained its second line, exactly
     matching the phase file's own Step 3.3 code block:
     ```cpp
     void Core::RegisterBuiltinCapabilityOrchestrators()
     {
         m_capabilityOrchestrators.push_back(std::make_unique<LegacyRenderFeatureOrchestrator>(*this));
         m_capabilityOrchestrators.push_back(std::make_unique<EditorPanelCapabilityOrchestrator>());
     }
     ```
     `EditorPanelCapabilityOrchestrator` needs no constructor argument (unlike
     `LegacyRenderFeatureOrchestrator`, which needs `Core&` to reach
     `FindPluginRenderFeatureTarget()`/`GetPluginHost()`) — it only ever
     touches the process-wide `EditorPanelRegistry::Instance()` singleton and
     the `modules` vector `Core::LoadPlugins()` already hands it directly, so
     it has zero dependency on `Core` itself.

4. **`src/Editor/EditorHost.cpp`: deleted the now-dead inline loop.** The
   entire `#if GTE_ENABLE_PLUGINS ... for (IPluginModule* module :
   m_core.GetPluginHost().AllLoadedModules()) { ... } #endif` block (formerly
   lines ~224-235) is gone — fully superseded by `Core::LoadPlugins()`
   internally invoking every registered orchestrator, including the new
   `EditorPanelCapabilityOrchestrator`. Confirmed via `search_in_dir` for
   `IEditorPanelModule_v1`/`kIEditorPanelModule_v1_Name`/
   `IPluginPanelDrawContext`/`IPluginModule` across `EditorHost.cpp` before
   removing the now-unused `#include
   "../../plugins/gte_plugin_abi/IEditorPanelModule.h"` — zero remaining
   real code usage (only one comment mentioning the class name for
   historical context), so that include was removed too.
   `#include "../Core/EditorPanelRegistry.h"` stays — `RegisterBuiltinPanelName()`
   is still called directly from `EditorHost.cpp`'s constructor.

5. **`CMakeLists.txt`** — added `src/Core/Plugins/EditorPanelCapabilityOrchestrator.h`/
   `.cpp` to `gte_core`'s source-file list, immediately after
   `LegacyRenderFeatureOrchestrator.h`/`.cpp`'s own existing entries, same
   relative style/comment convention as PHASE2's own 2 new files.

## Deviations from the plan

**None.** Every file touched, every new class shape, and every relocated
loop body matches the phase file's own Step 3.1–3.4 text exactly. The one
place the phase file itself flagged as needing a "confirm current line
numbers before editing" step (Step 3.1) was followed — `read_file` was used
on `EditorHost.cpp` immediately before editing, and the actual current line
numbers (constructor body starting around line 159, the
`RegisterBuiltinPanelName` block at ~202-222, the plugin-panel loop at
~224-235) matched the phase file's own quoted line numbers closely enough
that no surprise was found.

## Verification evidence

### 1. Incremental build

`cmake --build build` (Ninja/MinGW). **8/8 steps succeeded** on the first
attempt (no compile errors this time — `EditorPanelCapabilityOrchestrator`
needs no forward-declaration/complete-type destructor fix, unlike PHASE2's
`Core`, since it holds no `unique_ptr<IncompleteType>` member itself):

```
[1/8] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/EditorPanelCapabilityOrchestrator.cpp.obj
[2/8] Building CXX object CMakeFiles/gte_core.dir/src/Core/Core.cpp.obj
[3/8] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/EditorHost.cpp.obj
[4/8] Linking CXX static library libgte_core.a
[5/8] Linking CXX static library libgte_editor.a
[6/8] Linking CXX executable GreatTamanaEditor.exe; ...
[7/8] Linking CXX executable tests\GreatTamanaEngineTests.exe; ...
```

(Only pre-existing, unrelated `MingwRuntime.cmake`/KTX version warnings in
`stderr` — no new warnings, no errors.)

### 2. Live smoke test

`run_app_background` on `build\GreatTamanaEditor.exe` (PID 2888), then:

**`GET /list_tabs`** (BEFORE this phase's own PHASE2 report already recorded
the identical baseline):

```json
{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]}
```

**AFTER this phase's changes** — same request, identical result, confirming
every built-in panel name still lists first, in the exact same order, with
`"Demo Plugin Panel"` (from `demo_editor_panel.dll`'s own
`IEditorPanelModule_v1` implementation) still appearing LAST — the exact
invariant this phase's whole reordering fix exists to preserve now that the
registration mechanism is fully different internally:

```json
{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]}
```

**`GET /get_logs?limit=100`** (no `/clear_logs` first, to catch every
startup-time log line) — the plugin-load-time log lines read byte-for-byte
identically to PHASE2's own baseline:

```
[Warning] PluginHost: "This build was NOT linked with shared/DLL CRT (sharedRuntimeLinkage=0)..."
[Info]    PluginHost: "Loaded plugin 'DemoEditorPanelPlugin' v1.0.0 from ...\plugins\demo_editor_panel.dll"
[Info]    PluginHost: "Loaded plugin 'HelloWorldPlugin' v1.0.0 from ...\plugins\demo_hello_world.dll"
[Info]    PluginHost: "Loaded plugin 'DemoRenderFeaturePlugin' v1.0.0 from ...\plugins\demo_render_feature.dll"
[Info]    PluginHost: "Loaded plugin 'DemoRenderFeaturePluginSecond' v1.0.0 from ...\plugins\demo_render_feature_second.dll"
[Warning] PluginHost: "2 loaded plugins implement IRenderFeatureModule_v1 - only the LAST-registered one's render output will be visible this frame (render-graph compositing for multiple render-feature plugins is not implemented - see docs/conventions/plugin-architecture.md)."
[Info]    Network:    "listening on 127.0.0.1:8080"
[Info]    EditorHost: "EditorHost constructed: SdlContext -> Window -> Core -> CreateEditorLayer() -> Core::SetEditorLayerHook() all completed, with a genuinely non-null IEditorLayer*, every automation bridge attached, and NetworkServer started."
```

No new warning/error, no missing plugin-load line, no changed ordering —
`DemoEditorPanelPlugin` still loads and its panel name still appears last in
`GET /list_tabs`, proving `EditorPanelCapabilityOrchestrator::OnPluginsLoaded()`
runs correctly from inside `Core::LoadPlugins()` (called before the
constructor body reaches `#if GTE_ENABLE_NETWORK m_networkServer.Start(8080)
#endif`), and that the built-ins-first reordering fix did not break anything
observable.

`stop_app_background` called at the end of the check (PID 2888).

### 3. `git_status`

Before commit, the diff is exactly:

```
modified:   CMakeLists.txt
modified:   src/Core/Core.cpp
modified:   src/Editor/EditorHost.cpp
new file:   src/Core/Plugins/EditorPanelCapabilityOrchestrator.cpp
new file:   src/Core/Plugins/EditorPanelCapabilityOrchestrator.h
```

— matching the phase file's own "Verification" step 3 exactly
(`EditorHost.cpp`, `Core.cpp`, `CMakeLists.txt`, and the 2 new
`src/Core/Plugins/` files, no other file touched). `Core.h` needed no change
this phase (its `IPluginCapabilityOrchestrator` forward-declaration and
`m_capabilityOrchestrators` member, plus `~Core()`'s explicit
out-of-line-destructor fix, were all already added in PHASE2).

## What this phase does NOT do (confirmed honored)

- Did not change `EditorPanelRegistry`'s own class/methods at all — only WHO
  calls `RegisterPluginPanel()` (now `EditorPanelCapabilityOrchestrator`
  instead of `EditorHost::EditorHost()`'s own inline loop) and WHEN (now
  inside `Core::LoadPlugins()`, reordered so built-ins still register first)
  changed.
- Did not touch `plugins/demo_editor_panel/` at all.
- Did not add anything `_v2`/render-feature-compositor-related (PHASE4's
  job).
