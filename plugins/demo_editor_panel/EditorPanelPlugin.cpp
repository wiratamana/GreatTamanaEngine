// plugins/demo_editor_panel/EditorPanelPlugin.cpp
//
// PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md - Milestone 2's own
// throwaway proof: implements IEditorPanelModule_v1, contributing one
// dockable Editor panel ("Demo Plugin Panel") showing a single trivial
// ImGui::Text() line, drawn ONLY through the curated IPluginPanelDrawContext
// (never a real ImGui::* call - PHASE0_MASTER_STRATEGY.md, Step 2.4). This
// plugin deliberately implements ONLY the editor-tier capability - proving a
// plugin need not implement both halves of a "feature" (see
// demo_render_feature's own IRenderFeatureModule_v1-only shape) to be valid.

#include "../gte_plugin_abi/IPluginModule.h"
#include "../gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
#include "../gte_plugin_abi/IEditorPanelModule.h"
#include "../gte_plugin_abi/IPluginPanelDrawContext.h"

#include <cstring>

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

class DemoEditorPanelPluginModule final : public IPluginModule {
public:
    void* QueryCapability(const char* nameAndVersion) override
    {
        if (std::strcmp(nameAndVersion, kIEditorPanelModule_v1_Name) == 0) {
            return static_cast<IEditorPanelModule_v1*>(&m_panel);
        }
        return nullptr;
    }
    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override
    {
        std::strncpy(outInfo.name, "DemoEditorPanelPlugin", sizeof(outInfo.name) - 1);
        std::strncpy(outInfo.version, "1.0.0", sizeof(outInfo.version) - 1);
        std::strncpy(outInfo.description, "Milestone 2 proof - one dockable panel showing a trivial ImGui::Text() line.",
            sizeof(outInfo.description) - 1);
    }

private:
    DemoEditorPanel m_panel;
};

} // namespace
} // namespace gte

extern "C" {
__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}
__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule()
{
    return new gte::DemoEditorPanelPluginModule();
}
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    delete module;
}
}
