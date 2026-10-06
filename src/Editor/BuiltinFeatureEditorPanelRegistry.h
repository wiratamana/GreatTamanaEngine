#pragma once

#include <memory>
#include <string>
#include <vector>

namespace gte {

class IEngineFeatureModule;
class IEditorPanelModule_v1;
class Renderer;
struct EditorContext;

namespace rg { class RenderGraph; } // namespace rg

// Given a live IEngineFeatureModule& (already constructed by
// BuiltinFeatureModuleRegistry) plus this session's real EditorContext&/
// Renderer&/RenderGraph&, builds that module's own Editor panel. Only ever
// implemented inside a feature's own Editor/ subfolder .cpp. Expected to be
// a simple, non-throwing constructor call - unlike
// BuiltinFeatureModuleRegistry::CreateAll(), this is NOT wrapped in
// try/catch: a panel failing to construct is a bug in the panel's own
// constructor to fix directly.
using EditorPanelFactoryForModule = std::unique_ptr<IEditorPanelModule_v1> (*)(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph);

// Meyers-singleton, mirrors BuiltinFeatureModuleRegistry's shape, keyed by
// the SAME ModuleName() string rather than a compile-time type.
class BuiltinFeatureEditorPanelRegistry {
public:
    static BuiltinFeatureEditorPanelRegistry& Instance();

    // Called by a feature's own Editor-tier self-registration macro, at
    // static-init time. A duplicate moduleName is refused exactly like
    // BuiltinFeatureModuleRegistry::RegisterFactory() refuses one.
    void RegisterFactory(const char* moduleName, EditorPanelFactoryForModule factory);

    // Looks up module.ModuleName(); returns nullptr (expected - most
    // modules have no panel) if nothing was registered under that exact
    // name, otherwise invokes the matching factory.
    std::unique_ptr<IEditorPanelModule_v1> TryCreatePanel(
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
