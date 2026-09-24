#include "EditorPanelRegistry.h"

#include "Logging.h"

namespace gte {

EditorPanelRegistry& EditorPanelRegistry::Instance()
{
    static EditorPanelRegistry instance;
    return instance;
}

void EditorPanelRegistry::RegisterBuiltinPanelName(const std::string& name)
{
    m_allNames.push_back(name);
}

void EditorPanelRegistry::RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module)
{
    // editor-core-separation-4 campaign, PHASE3
    // (PHASE3_EDITOR_PANEL_NAME_COLLISION_PROTECTION.md) - a plugin's own
    // IEditorPanelModule_v1::GetPanelName() can return ANY string (there is
    // no admin gate under this system's own "always all-in, zero manifest"
    // design - Locked Design Decision #1) - including, by accident or on
    // purpose, a name that already belongs to a built-in panel or an
    // earlier-loaded plugin's panel. Accepting it anyway would make
    // DockLayout.cpp call ImGui::DockBuilderDockWindow() twice for the
    // identical window title, and ImGuiEditorLayer.cpp call ImGui::Begin()
    // twice with the identical title in the same frame - a well-known Dear
    // ImGui ID-collision hazard. Refuse (log + skip) rather than silently
    // accept, mirroring this exact codebase's own established "clean skip,
    // never crash, never silently corrupt state" convention already used for
    // PluginHost's fingerprint-mismatch/missing-export/decline-to-load
    // paths (src/Core/Plugins/PluginHost.cpp).
    if (IsKnownName(name)) {
        GTE_LOG_WARNING("EditorPanelRegistry",
            "Refusing to register plugin panel '" + name + "' - a panel "
            "with this exact name is already registered (either a built-in "
            "panel or an earlier-loaded plugin's own panel). This plugin's "
            "panel will not be shown. Pick a unique panel name to fix this.");
        return;
    }

    m_allNames.push_back(name);
    m_pluginPanels.push_back(PluginPanelEntry{ name, module });
}

bool EditorPanelRegistry::IsKnownName(const std::string& name) const noexcept
{
    for (const std::string& candidate : m_allNames) {
        if (name == candidate) {
            return true;
        }
    }
    return false;
}

} // namespace gte
