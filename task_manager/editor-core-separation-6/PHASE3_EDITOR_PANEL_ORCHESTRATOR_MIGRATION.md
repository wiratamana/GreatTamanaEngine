# PHASE3 — Editor Panel Wiring Migration onto `IPluginCapabilityOrchestrator`

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST, especially Locked
Design Decisions #4 and #6). Also read `PHASE2_COMPLETION_REPORT.md` before
starting.

## Step 1: The Goal

Prove `IPluginCapabilityOrchestrator` (PHASE2) truly generalizes beyond
render features by migrating the EXISTING `IEditorPanelModule_v1` wiring
onto it too — with **zero observable behavior change** — including a real,
necessary reordering fix inside `EditorHost.cpp`'s constructor that this
campaign's own research discovered (Step 2 below).

## Step 2: The Situation

Confirmed by direct read, `src/Editor/EditorHost.cpp` constructor (current
line numbers ~188-235):

```cpp
m_core.SetPresentImGuiRecorder([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });

#if GTE_ENABLE_PLUGINS
    m_core.LoadPlugins(gte::ExecutableDirectory() / "plugins");           // <- line 199
#endif

EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Hierarchy");    // <- lines 210-222
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Inspector");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Scene");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Game");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Memory");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Profiler");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Render Graph");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Jobs");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Atmosphere");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Log");
#if GTE_ENABLE_PROJECT_PANEL
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Project");
#endif

#if GTE_ENABLE_PLUGINS
    for (IPluginModule* module : m_core.GetPluginHost().AllLoadedModules()) {   // <- lines 229-234
        if (auto* panel = static_cast<IEditorPanelModule_v1*>(
                module->QueryCapability(kIEditorPanelModule_v1_Name))) {
            EditorPanelRegistry::Instance().RegisterPluginPanel(panel->GetPanelName(), panel);
        }
    }
#endif
```

**Real, confirmed problem this campaign's research found:** `EditorPanelRegistry.h`'s
own doc comment states the invariant "`AllNames()` always lists every
built-in panel first, plugins after" — currently true only because
`RegisterBuiltinPanelName(...)` (lines 210-222) happens to be written BEFORE
the plugin-panel loop (lines 229-234) in this same function, textually. If
this campaign naively makes `Core::LoadPlugins()` automatically invoke
EVERY registered `IPluginCapabilityOrchestrator` (as PHASE2 already made it
do, and as this phase's own plugin-panel orchestrator would join), the
plugin-panel registration would now happen INSIDE `m_core.LoadPlugins(...)`
at line 199 — which is BEFORE the `RegisterBuiltinPanelName(...)` calls at
lines 210-222 — silently breaking the "built-ins first" invariant for the
very first time any `IEditorPanelModule_v1` plugin is loaded.

**Confirmed, also important:** `EditorPanelRegistry.h` already lives at
`src/Core/EditorPanelRegistry.h` (gte_core tier, NOT `src/Editor/`) — so a
new `gte_core`-tier orchestrator class CAN legitimately
`#include "../EditorPanelRegistry.h"` and call it directly; this is not a
layering violation. `EditorPanelRegistry` is also already a process-wide
Meyers singleton (`EditorPanelRegistry::Instance()`), reachable from
anywhere, so no new plumbing is needed to reach it from `src/Core/Plugins/`.

**Confirmed, also important:** `Core::LoadPlugins()` has exactly ONE call
site in the entire engine — `EditorHost.cpp` line 199 — confirmed via
`search_in_dir` for `LoadPlugins(` across `src/`. There is no Player-host
call site to worry about; making the editor-panel orchestrator's
`OnPluginsLoaded()` run unconditionally inside `Core::LoadPlugins()` is safe
precisely because that method itself is only ever invoked from
`EditorHost.cpp`, exactly mirroring `IEditorPanelModule_v1` itself only ever
being queried from `gte_editor`-tier code today.

## Step 3: The Plan

### Step 3.1 — Fix the ordering in `EditorHost.cpp`'s constructor FIRST

Move the entire block of 10 (11 with `GTE_ENABLE_PROJECT_PANEL`)
`EditorPanelRegistry::Instance().RegisterBuiltinPanelName(...)` calls
(currently lines ~210-222) to execute BEFORE the
`#if GTE_ENABLE_PLUGINS ... m_core.LoadPlugins(...) ... #endif` block
(currently lines ~198-200). Use `read_file` on `EditorHost.cpp` immediately
before editing to get the CURRENT exact line numbers (this phase's own
edits, and PHASE2's Core.cpp changes, may have shifted them slightly from
the numbers quoted here) — then use `edit_line` to relocate the block
verbatim (same 10/11 lines, same order, same text, only its position in the
file changes). This is a pure reordering: nothing else touches
`EditorPanelRegistry` between the old and new position, so `AllNames()`'s
final content is unaffected by this move alone.

### Step 3.2 — New files: `src/Core/Plugins/EditorPanelCapabilityOrchestrator.h` + `.cpp`

```cpp
#pragma once

#include "IPluginCapabilityOrchestrator.h"

namespace gte {

// editor-core-separation-6 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md) - a VERBATIM relocation of
// EditorHost.cpp's own former inline IEditorPanelModule_v1 discovery loop
// (editor-core-separation-3, PHASE4) into the IPluginCapabilityOrchestrator
// shape - ZERO observable behavior change. Lives in gte_core (src/Core/
// Plugins/), NOT gte_editor, exactly like EditorPanelRegistry.h itself
// already does - see this phase's own Step 2 evidence for why this is not
// a layering violation. Overrides ONLY OnPluginsLoaded() - editor panels
// are drawn through Dear ImGui, never through the render graph, so this
// class never overrides ContributeRenderGraphPasses() (the interface's own
// default no-op is correct and sufficient).
class EditorPanelCapabilityOrchestrator final : public IPluginCapabilityOrchestrator {
public:
    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
};

} // namespace gte
```

`.cpp` — `OnPluginsLoaded()`'s ENTIRE body is EXACTLY the loop quoted in
Step 2 above (the `for (IPluginModule* module : modules)` /
`QueryCapability(kIEditorPanelModule_v1_Name)` /
`EditorPanelRegistry::Instance().RegisterPluginPanel(...)` logic), with
`modules` as this method's own real parameter instead of
`m_core.GetPluginHost().AllLoadedModules()` (the SAME underlying vector —
`IPluginCapabilityOrchestrator::OnPluginsLoaded()` is called by
`Core::LoadPlugins()` with exactly `m_pluginHost.AllLoadedModules()`, PHASE2
Step 3.4 — so this is not a behavior change, only where the same reference
is obtained from). Needs `#include "../EditorPanelRegistry.h"` and
`#include "../../../plugins/gte_plugin_abi/IEditorPanelModule.h"` (mirror
`Core.cpp`'s own existing relative-include style for `plugins/gte_plugin_abi/`
headers).

### Step 3.3 — Register it, and delete the now-dead inline loop

`Core.cpp`'s `RegisterBuiltinCapabilityOrchestrators()` (PHASE2) gains a
second line:

```cpp
void Core::RegisterBuiltinCapabilityOrchestrators()
{
    m_capabilityOrchestrators.push_back(std::make_unique<LegacyRenderFeatureOrchestrator>(*this));
    m_capabilityOrchestrators.push_back(std::make_unique<EditorPanelCapabilityOrchestrator>());
}
```

`EditorHost.cpp`: DELETE the entire `for (IPluginModule* module :
m_core.GetPluginHost().AllLoadedModules()) { ... }` block (Step 2's second
quoted block) — it is now dead code, fully superseded by
`Core::LoadPlugins()` internally invoking every orchestrator, including
this new one. Confirm via `search_in_dir` that `IEditorPanelModule_v1`/
`kIEditorPanelModule_v1_Name`/`IPluginPanelDrawContext` are not referenced
anywhere ELSE in `EditorHost.cpp` before removing any now-unused
`#include`.

### Step 3.4 — CMake

Add `src/Core/Plugins/EditorPanelCapabilityOrchestrator.h`/`.cpp` to
`gte_core`'s source-file list in the root `CMakeLists.txt`, same style as
PHASE2's 2 new files.

### Verification

1. Incremental build: `cmake --build build`.
2. Live smoke test: `run_app_background` on `build\GreatTamanaEditor.exe`,
   `GET /list_tabs` — confirm the exact same panel list as before this
   phase, in the exact same order (every built-in panel name first —
   `"Hierarchy"`, `"Inspector"`, `"Scene"`, `"Game"`, `"Memory"`,
   `"Profiler"`, `"Render Graph"`, `"Jobs"`, `"Atmosphere"`, `"Log"`,
   optionally `"Project"` — THEN `"Demo Plugin Panel"` from
   `demo_editor_panel`, appearing last). `GET /get_logs?limit=100` —
   confirm the existing `"Loaded plugin 'DemoEditorPanelPlugin' v1.0.0
   from ..."`-style startup log lines still read identically.
   `stop_app_background` afterward.
3. `git_status` — confirm the diff touches only `EditorHost.cpp`, `Core.cpp`,
   `CMakeLists.txt`, and the 2 new `src/Core/Plugins/` files.

### What this phase does NOT do

- Does not change `EditorPanelRegistry`'s own class/methods at all — only
  WHO calls `RegisterPluginPanel()` and WHEN (reordered so built-ins still
  register first) changes.
- Does not touch `plugins/demo_editor_panel/` at all.
- Does not add anything `_v2`/render-feature-compositor-related (PHASE4).

### Completion

Write `PHASE3_COMPLETION_REPORT.md` (exact `GET /list_tabs` response body,
before/after comparison confirming identical ordering), then `git_add` +
`git_commit`.
