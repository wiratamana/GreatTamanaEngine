#include "BuiltinFeatureEditorPanelRegistry.h"

#include "../Core/Logging.h"
#include "../Core/EditorPanelModule.h"
#include "../Core/Plugins/IEngineFeatureModule.h"

#include <cassert>

namespace gte {

BuiltinFeatureEditorPanelRegistry& BuiltinFeatureEditorPanelRegistry::Instance()
{
    static BuiltinFeatureEditorPanelRegistry s_instance;
    return s_instance;
}

void BuiltinFeatureEditorPanelRegistry::RegisterFactory(const char* moduleName, EditorPanelFactoryForModule factory)
{
    for (const FactoryEntry& entry : m_factories) {
        if (entry.moduleName == moduleName) {
            GTE_LOG_ERROR("BuiltinFeatureEditorPanelRegistry",
                std::string("RegisterFactory: duplicate name '") + moduleName + "' - refused.");
            assert(false && "BuiltinFeatureEditorPanelRegistry: duplicate name - see GTE_LOG_ERROR above.");
            return;
        }
    }
    m_factories.push_back({ moduleName, factory });
}

std::unique_ptr<IEditorPanelModule_v1> BuiltinFeatureEditorPanelRegistry::TryCreatePanel(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph) const
{
    for (const FactoryEntry& entry : m_factories) {
        if (entry.moduleName == module.ModuleName()) {
            return entry.factory(module, ctx, renderer, renderGraph);
        }
    }
    return nullptr;
}

} // namespace gte
