#pragma once

#include <memory>
#include <string>
#include <vector>

namespace gte {

class IEngineFeatureModule;
class EditorWindow;
class Renderer;
struct EditorContext;

namespace rg { class RenderGraph; } // namespace rg

// Builds the editor window of one feature module. Implemented in the feature's own Editor/ folder; must not throw.
using EditorPanelFactoryForModule = std::unique_ptr<EditorWindow> (*)(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph);

// Factories keyed by IEngineFeatureModule::ModuleName().
class BuiltinFeatureEditorPanelRegistry {
public:
    static BuiltinFeatureEditorPanelRegistry& Instance();

    // Called at static-init time. A duplicate moduleName is refused.
    void RegisterFactory(const char* moduleName, EditorPanelFactoryForModule factory);

    // Returns nullptr when the module has no editor window.
    std::unique_ptr<EditorWindow> TryCreatePanel(
        IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph) const;

private:
    BuiltinFeatureEditorPanelRegistry() = default;
    struct FactoryEntry { std::string moduleName; EditorPanelFactoryForModule factory; };
    std::vector<FactoryEntry> m_factories;
};

struct EditorPanelFactoryAutoRegister {
    EditorPanelFactoryAutoRegister(const char* moduleName, EditorPanelFactoryForModule factory)
    {
        BuiltinFeatureEditorPanelRegistry::Instance().RegisterFactory(moduleName, factory);
    }
};

} // namespace gte

#define GTE_EDITOR_PANEL_FACTORY_CONCAT_INNER(a, b) a##b
#define GTE_EDITOR_PANEL_FACTORY_CONCAT(a, b) GTE_EDITOR_PANEL_FACTORY_CONCAT_INNER(a, b)

#define GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL(ModuleNameStringLiteral, FactoryFunctionPointer) \
    static ::gte::EditorPanelFactoryAutoRegister \
        GTE_EDITOR_PANEL_FACTORY_CONCAT(g_gteEditorPanelFactoryAutoRegister_, __LINE__)( \
            ModuleNameStringLiteral, FactoryFunctionPointer)
