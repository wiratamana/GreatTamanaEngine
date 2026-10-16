#include "AtmospherePluginPanelModule.h"

#include "../AtmosphereFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

namespace gte {

void AtmospherePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    BuildAtmospherePanel(m_ctx, m_feature.Settings());
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateAtmospherePanelModule(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph)
{
    // Only invoked for the module registered as "Atmosphere", which holds an AtmosphereFeature.
    return std::make_unique<AtmospherePluginPanelModule>(
        ctx, renderer, renderGraph, fx::FeatureOf<AtmosphereFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Atmosphere", &CreateAtmospherePanelModule);

} // namespace gte
