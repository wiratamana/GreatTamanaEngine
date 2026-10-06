#include "AtmospherePluginPanelModule.h"

#include "../AtmosphereFeature.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

namespace gte {

void AtmospherePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    // Ignores the restricted ctx parameter (established convention - see
    // IPluginPanelDrawContext's own doc comment) and calls the real panel
    // builder directly.
    BuildAtmospherePanel(m_ctx, m_feature.Settings(), m_engineRenderer, m_feature.Renderer(),
        m_lastValidationResult, m_lastAerialInspectionResult, m_renderGraph, m_lastSkyPurityResult);
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateAtmospherePanelModule(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph)
{
    // Safe: the ONLY IEngineFeatureModule ever registered under the exact
    // name "Atmosphere" is AtmosphereFeature itself - this factory is only
    // ever invoked for a module whose ModuleName() == "Atmosphere".
    return std::make_unique<AtmospherePluginPanelModule>(
        ctx, renderer, renderGraph, static_cast<AtmosphereFeature&>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Atmosphere", &CreateAtmospherePanelModule);

} // namespace gte
