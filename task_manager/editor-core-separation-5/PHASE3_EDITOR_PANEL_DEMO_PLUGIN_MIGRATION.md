# PHASE3 — Migrate `demo_editor_panel` onto the new authoring sugar

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read `PHASE1`'s and
`PHASE2`'s own `PHASEn_COMPLETION_REPORT.md` files for continuity clues before
starting.

**Use `ask_questions` if anything below is ambiguous or looks wrong once you have
the real files open — do not silently guess.**

---

## Step 1: The Goal

Rewrite `plugins/demo_editor_panel/EditorPanelPlugin.cpp` to use
`SingleCapabilityPluginModule<IEditorPanelModule_v1>` +
`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`, exactly mirroring Phase 2's
migration but for the `IEditorPanelModule_v1` capability instead of
`IRenderFeatureModule_v1`. Same byte-identical-behavior requirement as every
other phase in this campaign: the panel's displayed name (`"Demo Plugin Panel"`),
its `ImGui::Text()`-equivalent content string (`"Hello from a plugin!"`), and its
`GtePluginModuleInfo` fields must all read identically before and after.

## Step 2: The Situation

Current `plugins/demo_editor_panel/EditorPanelPlugin.cpp` (69 lines) defines:
`DemoEditorPanel : public IEditorPanelModule_v1` (the real logic —
`GetPanelName()` returns `"Demo Plugin Panel"`, `BuildPanel(IPluginPanelDrawContext&)`
calls `ctx.Text("Hello from a plugin!")`), a hand-written
`DemoEditorPanelPluginModule : public IPluginModule` glue class
(`QueryCapability()` string-compare against `kIEditorPanelModule_v1_Name`,
`GetModuleInfo()` with 3 `std::strncpy` calls filling `"DemoEditorPanelPlugin"`
/ `"1.0.0"` /
`"Milestone 2 proof - one dockable panel showing a trivial ImGui::Text() line."`),
and the verbatim `extern "C"` export block. Read the file in full with
`read_file` before editing to have the exact current strings in front of you.

This plugin is the one the running `GreatTamanaEditor.exe` actually surfaces
visibly (a real dockable ImGui panel, listed by `EditorPanelRegistry` and
reachable via `GET /list_tabs`) — this phase's verification uses the LIVE
running Editor, unlike Phase 2's standalone-probe verification.

## Step 3: The Plan

### 3.1 — Rewrite `plugins/demo_editor_panel/EditorPanelPlugin.cpp`

Replace the ENTIRE file content with:

```cpp
// plugins/demo_editor_panel/EditorPanelPlugin.cpp
//
// PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md - Milestone 2's own
// throwaway proof: implements IEditorPanelModule_v1, contributing one
// dockable Editor panel ("Demo Plugin Panel") showing a single trivial
// ImGui::Text() line, drawn ONLY through the curated IPluginPanelDrawContext
// (never a real ImGui::* call). This plugin deliberately implements ONLY the
// editor-tier capability - proving a plugin need not implement both halves
// of a "feature" to be valid.
//
// editor-core-separation-5 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_DEMO_PLUGIN_MIGRATION.md) - migrated onto
// SingleCapabilityPluginModule<T> + GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE
// (see PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md) - removes the
// hand-written IPluginModule glue class and extern "C" block; every string
// literal below (panel name, panel text, module name/version/description) is
// byte-for-byte identical to this file's pre-migration content.

#include "../gte_plugin_abi/IEditorPanelModule.h"
#include "../gte_plugin_abi/IPluginPanelDrawContext.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

class DemoEditorPanel final : public IEditorPanelModule_v1 {
public:
    const char* GetPanelName() const override { return "Demo Plugin Panel"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override
    {
        ctx.Text("Hello from a plugin!");
    }
};

DemoEditorPanel g_panel;
SingleCapabilityPluginModule<IEditorPanelModule_v1> g_module(
    g_panel, kIEditorPanelModule_v1_Name,
    MakeModuleInfo("DemoEditorPanelPlugin", "1.0.0", "Milestone 2 proof - one dockable panel showing a trivial ImGui::Text() line."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
```

### 3.2 — Do not touch `plugins/demo_editor_panel/CMakeLists.txt`

Confirm it still only needs its existing source list, `gte_plugin_abi` link,
`RUNTIME_OUTPUT_DIRECTORY`/`PREFIX ""`, and
`gte_apply_plugin_dll_shared_crt_linkage(demo_editor_panel)` — no change.

Run `git_status` before Step 3.1 (`PHASE0_MASTER_STRATEGY.md` Workflow Rule
10) — confirm the branch still reads `feature/editor-core-separation` and the
tree is clean or contains only Phase 1/2's already-committed diff.

## Verification (live running Editor, plus the isolation probe)

1. Targeted incremental rebuild of the main tree:
   `cmake --build build --target demo_editor_panel GreatTamanaEditor` (only
   rebuilds the one changed plugin `.dll` plus re-links the host executable if
   needed — not a full `build` target rebuild).
2. Confirm zero compile/link errors.
3. `run_app_background` on `build\GreatTamanaEditor.exe`.
4. `gte_send_request` `GET /get_logs?limit=50` — confirm a line reading
   `"Loaded plugin 'DemoEditorPanelPlugin' v1.0.0 from ..."` (or whatever the
   exact current log format is — check a recent `editor-core-separation-4`
   completion report, e.g. `PHASE6_COMPLETION_REPORT.md`, for the exact quoted
   format if unsure) is present, and there are zero new/unexpected warnings —
   a `min_level=warning` follow-up query should show the same warning set as
   before this campaign (the shared-CRT startup warning and, if 2+
   render-feature plugins are present, the multi-render-feature warning — both
   pre-existing, unrelated to this phase).
5. `gte_send_request` `GET /list_tabs` — confirm `"Demo Plugin Panel"` is
   listed among the tabs, exactly as before this migration.
6. If a screenshot/visual endpoint is useful to double-confirm the panel
   actually renders its text (not just that it registered), use
   `gte_send_request` `GET /get_swapchain` (or `/get_game_view` if a dedicated
   Editor-window capture exists) and `load_image`/inspect the result — this is
   optional polish, not required, since `IPluginPanelDrawContext::Text()` is
   already proven correct by prior campaigns; the load-bearing check here is
   steps 4-5 (the plugin still loads and still registers under the same name).
7. Rebuild the isolation probe's own dedicated inner build too (per Phase 2's
   own precedent) — `cmake --build build-plugin-isolation-probe\
   gte_plugin_isolation_inner_build` then re-run
   `gte_plugin_isolation_probe.exe`, confirm `LoadedModuleCount() == 4` and
   `renderFeatureCount == 2` STILL hold (this phase's change must not affect
   either count — `demo_editor_panel` never implemented
   `IRenderFeatureModule_v1`).
8. `stop_app_background` the Editor PID when done.

## Completion

Write `PHASE3_COMPLETION_REPORT.md`: the rewritten file's exact final content,
the exact `GET /get_logs`/`GET /list_tabs` response bodies observed, the
isolation probe's re-confirmed pass, and the line-count reduction achieved
(69→~20, roughly). Run `git_status` (Workflow Rule 10) and confirm the diff
about to be staged is EXACTLY the one rewritten `.cpp` file (plus the
`PHASE3_COMPLETION_REPORT.md` you are about to add) — nothing else. Then
`git_add` + `git_commit` (message referencing PHASE3).
