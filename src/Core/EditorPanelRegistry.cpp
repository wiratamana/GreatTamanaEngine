#include "EditorPanelRegistry.h"
#include "Logging.h"
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3 - RecordPanel()'s own no-op-outside-a-bracket call,
// added to RegisterPluginPanel() below.
#include "Plugins/ProjectAssemblyRegistrationLedger.h"

#include <algorithm>

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
    ProjectAssemblyRegistrationLedger::Instance().RecordPanel(name); // editor-core-separation-13, PHASE3.
}

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2, Hazard 2 fix) - see EditorPanelRegistry.h's own doc comment on
// this method for the full reasoning. Removes `name` from BOTH
// m_pluginPanels AND m_allNames, so (a) the dangling IEditorPanelModule_v1*
// is never iterated again after its owning Project Assembly .dll is
// FreeLibrary()'d, and (b) IsKnownName()'s own collision guard does not
// permanently block a future re-registration under this exact same name -
// a deliberate, documented deviation from this feature's external design
// doc, which left m_allNames untouched (see this campaign's PHASE2 strategy
// file, Step 2, for the full reasoning on why that would be a guaranteed
// regression). Silent no-op if `name` was never registered.
void EditorPanelRegistry::UnregisterPluginPanel(const std::string& name)
{
    m_pluginPanels.erase(
        std::remove_if(m_pluginPanels.begin(), m_pluginPanels.end(),
            [&name](const PluginPanelEntry& e) { return e.name == name; }),
        m_pluginPanels.end());
    m_allNames.erase(
        std::remove_if(m_allNames.begin(), m_allNames.end(),
            [&name](const std::string& candidate) { return candidate == name; }),
        m_allNames.end());
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
