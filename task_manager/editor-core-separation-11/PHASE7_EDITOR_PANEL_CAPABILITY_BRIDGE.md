# PHASE7 of 8 — CAPABILITY #1: CUSTOM EDITOR PANEL

Read `PHASE0_MASTER_STRATEGY.md` first (Finding A especially — this phase
resolves it concretely).
**Depends on:** PHASE3 (folder/CMake), PHASE5 (runtime loader must call
`GTE_RegisterProject` with a real `gte::EditorHost&` before this phase's own
registration code has anything to register against).
**Blocks:** nothing downstream (PHASE8 is independent of this phase).

End state: `ProjectAssemblyProbe_Editor.dll` registers one real, dockable
Editor panel, titled "Probe Panel", that draws real ImGui widgets, appears
in the Editor's dock layout exactly like a built-in panel, and is fully
interactive (a button click that increments a counter — proving live ImGui
interaction works, not just that a static `ImGui::Text()` call renders
once).

## The gap this phase resolves (Finding A, concretely)

`EditorPanelRegistry::RegisterPluginPanel(const std::string& name,
IEditorPanelModule_v1* module)` is the ONLY real, existing, non-built-in
panel registration entry point today (confirmed, current
`src/Core/EditorPanelRegistry.h`, method at line 43).
`IEditorPanelModule_v1::BuildPanel()` takes `IPluginPanelDrawContext&` — the
curated ABI wrapper a `gte_plugin_abi` `.dll` is forced to draw through
because it never gets real ImGui headers/symbols.

**A Project Assembly is different: it CAN get real ImGui headers and,
once Finding A's ImGui-context risk is resolved (PHASE2, Step 5), it
resolves back to the SAME real, live `ImGuiContext*` the main `.exe`
already uses.** This means a Project Assembly panel does NOT need a
brand-new registration mechanism — it implements `IEditorPanelModule_v1`
EXACTLY as it stands today, receives the `ctx` parameter, simply IGNORES
it, and calls real `ImGui::*` functions directly inside its own
`BuildPanel()` body instead. This requires ZERO changes to
`EditorPanelRegistry` or `DockLayout.cpp` — but it DOES require one small,
targeted fix to `ImGuiEditorLayer.cpp` (Finding G, `PHASE0_MASTER_STRATEGY.md`
§2.4): see STEP 1a below before assuming otherwise.

## STEP 1 — resolve ImGui's headers (without a second compiled copy), for
`_Editor.dll` targets only, in `cmake/GteProject.cmake`

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE7
# (Finding A). ONLY for _Editor.dll targets - a _Game.dll never needs ImGui
# at all. Mirrors PHASE3's own gte_project_assembly_apply_header_paths()
# exactly (same headers-only-via-generator-expression technique) -
# imgui/imguizmo are deliberately excluded from THAT function and added
# here instead, in their OWN, separate, _Editor-only function, so a
# _Game.dll's own configure never even resolves the `imgui`/`imguizmo`
# CMake target names at all.
function(gte_project_assembly_apply_editor_header_paths TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:imguizmo,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()
```

PHASE3's own `gte_add_project()` already calls this (inside the
`if(EDITOR_SOURCES)` branch only, immediately after
`gte_project_assembly_apply_header_paths(${NAME}_Editor)`) — if PHASE3 was
implemented with this call present but the function itself not yet defined,
this phase's Step 1 is what completes it. Confirm it compiles now for real.

## STEP 1a — widen `ImGuiEditorLayer.cpp`'s existing plugin-panel-draw gate
(Finding G, `PHASE0_MASTER_STRATEGY.md` §2.4)

Confirmed, current `src/Editor/ImGuiEditorLayer.cpp`: the per-frame loop that
actually CALLS `entry.module->BuildPanel(drawContext)` for every entry in
`EditorPanelRegistry::Instance().PluginPanels()` is wrapped in
`#if GTE_ENABLE_PLUGINS` (re-locate the exact line via `search_in_dir` for
`PluginPanels()` in that file before editing — do not trust a specific line
number). This is the WRONG flag for a Project Assembly panel: registering
"Probe Panel" via `RegisterPluginPanel()` (STEP 2 below) makes it show up as
a dock tab (`DockLayout.cpp`'s own, separate loop over the same
`PluginPanels()` list, used only to assign a default dock slot, has NO such
gate at all) but its `BuildPanel()` is never actually called whenever a
developer has set `GTE_ENABLE_PLUGINS=OFF` — an empty, permanently-blank
panel that looks like a bug, not a configuration choice.

Fix: widen the gate to include this campaign's own flag too:

```cpp
#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES
        for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
            if (ImGui::Begin(entry.name.c_str())) {
                PluginPanelDrawContextAdapter drawContext;
                entry.module->BuildPanel(drawContext);
            }
            ImGui::End();
        }
#endif
```

This is safe: the loop body only ever iterates whatever is ACTUALLY present
in the registry at runtime (built-in panels never call `RegisterPluginPanel()`
at all), regardless of which system — the old `gte_plugin_abi` plugin
system, or this campaign's Project Assembly system, or both at once —
populated it. `GTE_ENABLE_PROJECT_ASSEMBLIES` must already be a real,
compiled-in preprocessor definition reaching `gte_editor` by this point
(PHASE5, Step 3's own `target_compile_definitions(gte_core PUBLIC
GTE_ENABLE_PROJECT_ASSEMBLIES=$<BOOL:...>)` addition propagates transitively
into `gte_editor`, exactly like `GTE_ENABLE_PLUGINS` already does) — if that
definition is missing for any reason, this file will fail to compile with an
"undeclared identifier" error, a loud, immediate signal rather than a silent
gap.

Verify concretely: build with `GTE_ENABLE_PLUGINS=OFF` and
`GTE_ENABLE_PROJECT_ASSEMBLIES=ON` (a fresh, separate `cmake -S . -B
build-plugins-off` reconfigure — never reuse/overwrite the main `build/`
tree for this one-off check) and confirm "Probe Panel" still draws its real
content (not just an empty tab) via `GET /activate_tab` + `GET
/get_swapchain`, exactly like STEP 3 below does for the normal
(`GTE_ENABLE_PLUGINS=ON`) configuration.

## STEP 2 — the Project Assembly's own panel `.cpp` (replaces PHASE3's
placeholder `Assets/Editor/HelloEditorPanel.cpp`)

**Confirm the exact real header path for `IEditorPanelModule_v1` before
writing this `#include`** — search `plugins/gte_plugin_abi/` for the exact
file defining it (do not guess the filename from memory).

```cpp
// editor-core-separation-11 campaign (Project Assembly system), PHASE7 -
// a real, live, interactive Editor panel contributed entirely from a
// Project Assembly's own _Editor.dll, calling real ImGui directly (no ABI
// wrapper) - the concrete resolution of Finding A
// (PHASE0_MASTER_STRATEGY.md).
#include "../../Libraries/ProjectAssemblyExports.h"
#include "../../../../src/Core/Core.h"
#include "../../../../src/Editor/EditorHost.h"
#include "../../../../src/Core/EditorPanelRegistry.h"
#include "../../../../plugins/gte_plugin_abi/IEditorPanelModule.h" // confirm exact real path first

#include <imgui.h>

namespace {

class ProbeEditorPanel final : public gte::IEditorPanelModule_v1 {
public:
    const char* GetPanelName() const override { return "Probe Panel"; }

    // `ctx` is DELIBERATELY unused here - this whole phase's point is that a
    // Project Assembly has real ImGui access and does not need the curated
    // ABI wrapper. Do not delete this parameter or change the interface's
    // own signature - IEditorPanelModule_v1 is a FROZEN gte_plugin_abi type,
    // never modified for this system (LDD1).
    void BuildPanel(gte::IPluginPanelDrawContext& /*ctx*/) override {
        ImGui::Text("Hello from a real Project Assembly Editor panel.");
        if (ImGui::Button("Click me")) {
            ++m_clickCount;
        }
        ImGui::Text("Clicked %d time(s)", m_clickCount);
    }

private:
    int m_clickCount = 0;
};

ProbeEditorPanel g_probePanel; // Process-lifetime instance - hand-adapted
                                // static-instance convention (no hot
                                // reload, LDD4, so a plain namespace-scope
                                // static is correct and sufficient).

void RegisterProbeEditor(gte::Core& core, gte::EditorHost& editorHost) {
    (void)core;
    // EditorPanelRegistry::Instance() is a process-wide Meyers singleton
    // (confirmed, current EditorPanelRegistry.h) - directly reachable from
    // ANY translation unit that includes its header and links back to the
    // one real definition, which a Project Assembly does (Phase 2/3 already
    // proved symbols resolve back into GreatTamanaEditor.exe). Confirm this
    // works concretely (Step 3 below) rather than assuming.
    gte::EditorPanelRegistry::Instance().RegisterPluginPanel("Probe Panel", &g_probePanel);
    (void)editorHost;
}

} // namespace

GTE_DEFINE_PROJECT_EXPORTS_EDITOR(RegisterProbeEditor)
```

**Note the open question resolved by testing, not assumption:**
`RegisterProbeEditor()` above may not even need the `editorHost` parameter
for THIS specific capability, since `EditorPanelRegistry::Instance()` is
directly reachable with no hook through `EditorHost` at all. Confirm this by
testing (Step 3) rather than assuming a hook through `EditorHost` is required
just because the exported function's own signature includes it (the
signature stays as designed either way — a future capability may genuinely
need a live `EditorHost&` for something `EditorPanelRegistry::Instance()`
alone cannot provide; this note only clarifies THIS capability's own actual
need).

## STEP 3 — build, launch, and verify the ImGui-context risk concretely, for
real, not just via PHASE2's isolated micro-probe

1. Rebuild `ProjectAssemblyProbe_Editor` (`cmake --build build --target
   ProjectAssemblyProbe_Editor`).
2. `run_app_background` → `build/GreatTamanaEditor.exe`.
3. `gte_send_request` → `GET /list_tabs` — confirm `"Probe Panel"` appears in
   the returned list of known panel names (this repo's own existing HTTP
   diagnostics endpoint for exactly this check — confirmed present,
   `AGENTS.md` "Networking" section).
4. `gte_send_request` → `GET /activate_tab?name=Probe%20Panel` (confirm the
   exact real query-parameter name/casing via `search_in_dir` for
   `/activate_tab` in `NetworkRoutes.cpp` before trusting this literally) to
   bring it to the front.
5. `gte_send_request` → `GET /get_swapchain` (or `/get_game_view`, whichever
   captures the Editor UI itself) — confirm the panel's text/button are
   visible in the returned image (`load_image`-style content block — inspect
   it directly).
6. Since a raw screenshot cannot itself click a button, ALSO confirm the
   underlying interaction path is sound via `GET /get_logs` for zero
   unexpected ImGui-context-related warnings/errors during this whole
   sequence (an ImGui-context mismatch typically manifests as either a
   crash, a garbled/blank panel, or a Dear ImGui assertion — any of the three
   is a direct, real finding: stop, go back to PHASE2 Step 5, and resolve it
   there before continuing). If this repo's HTTP surface has no direct
   "simulate a click" endpoint, a garbled/blank/correctly-rendered-but-static
   panel image (Step 5) combined with zero crash/log-warning across a
   multi-second observation window (poll `GET /get_swapchain` a few times in
   a row, confirm the frame keeps advancing normally) is accepted as
   sufficient live evidence for this phase's own Definition of Done — record
   this explicitly if a true click-simulation was not possible.
7. `stop_app_background`.

## Definition of Done

- [ ] `gte_project_assembly_apply_editor_header_paths()` exists, is called
      only for `_Editor.dll` targets, and a `_Game.dll`-only project (no
      `Editor/` folder) never references `imgui`/`imguizmo` target names at
      configure time at all.
- [ ] `ProjectAssemblyProbe_Editor.dll` implements `IEditorPanelModule_v1`
      directly, ignoring `ctx`, calling real `ImGui::*` functions.
- [ ] The panel appears in `GET /list_tabs`, activates via `GET
      /activate_tab`, and is visible, correctly rendered, in a real,
      running `GreatTamanaEditor.exe` screenshot.
- [ ] Completion notes state explicitly whether `gte::EditorHost&` ended up
      being needed for this specific capability or not.
- [ ] `ImGuiEditorLayer.cpp`'s plugin-panel-draw gate is widened to
      `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES` (Finding G,
      STEP 1a), verified concretely with a `GTE_ENABLE_PLUGINS=OFF` +
      `GTE_ENABLE_PROJECT_ASSEMBLIES=ON` build showing the panel still draws
      real content, not just an empty dock tab.

## What this phase does NOT do

- Does NOT modify `IEditorPanelModule_v1`, `IPluginPanelDrawContext`, or any
  other `gte_plugin_abi` type (LDD1) — this phase reuses the existing
  interface completely unmodified.
- Does NOT implement the render-pass capability — PHASE8.
- Does NOT decide what a "real", non-toy Project Assembly panel should
  actually show — `ProbeEditorPanel` above is a proof-of-mechanism fixture
  only, mirroring this repo's own existing `plugins/demo_editor_panel/`
  fixture's identical role for the OTHER system.
