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
