#include "EditorPanelRegistry.h"

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
