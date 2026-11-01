#include "EditorPanelRegistry.h"

#include "Logging.h"
#include "Plugins/ProjectAssemblyRegistrationLedger.h"

#include <algorithm>

namespace gte {

EditorPanelRegistry& EditorPanelRegistry::Instance()
{
    static EditorPanelRegistry instance;
    return instance;
}

void EditorPanelRegistry::RegisterWindow(const std::string& name, EditorWindow* window)
{
    if (window == nullptr || name.empty()) {
        GTE_LOG_WARNING("EditorPanelRegistry", "Refusing to register a window without a name or an object.");
        return;
    }
    if (IsKnownName(name)) {
        GTE_LOG_WARNING("EditorPanelRegistry", "Refusing to register window '" + name + "': the name is already taken.");
        return;
    }
    m_windows.push_back(WindowEntry{ name, window });
    ProjectAssemblyRegistrationLedger::Instance().RecordPanel(name);
}

void EditorPanelRegistry::UnregisterWindow(const std::string& name)
{
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(),
                        [&name](const WindowEntry& entry) { return entry.name == name; }),
        m_windows.end());
}

bool EditorPanelRegistry::IsKnownName(const std::string& name) const noexcept
{
    return FindWindow(name) != nullptr;
}

EditorWindow* EditorPanelRegistry::FindWindow(const std::string& name) const noexcept
{
    for (const WindowEntry& entry : m_windows) {
        if (entry.name == name) {
            return entry.window;
        }
    }
    return nullptr;
}

std::vector<std::string> EditorPanelRegistry::AllNames() const
{
    std::vector<std::string> names;
    names.reserve(m_windows.size());
    for (const WindowEntry& entry : m_windows) {
        names.push_back(entry.name);
    }
    return names;
}

} // namespace gte
