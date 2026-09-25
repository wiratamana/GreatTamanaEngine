#include "EditorPanelCapabilityOrchestrator.h"

#include "../EditorPanelRegistry.h"

#include "../../../plugins/gte_plugin_abi/IEditorPanelModule.h"
#include "../../../plugins/gte_plugin_abi/IPluginModule.h"

namespace gte {

// editor-core-separation-6 campaign, PHASE3 - a VERBATIM relocation of
// EditorHost.cpp's own former inline IEditorPanelModule_v1 discovery loop
// (editor-core-separation-3, PHASE4) - same QueryCapability() name, same
// EditorPanelRegistry::Instance().RegisterPluginPanel() call, byte-for-byte.
void EditorPanelCapabilityOrchestrator::OnPluginsLoaded(const std::vector<IPluginModule*>& modules)
{
    for (IPluginModule* module : modules) {
        if (auto* panel = static_cast<IEditorPanelModule_v1*>(
                module->QueryCapability(kIEditorPanelModule_v1_Name))) {
            EditorPanelRegistry::Instance().RegisterPluginPanel(panel->GetPanelName(), panel);
        }
    }
}

} // namespace gte
