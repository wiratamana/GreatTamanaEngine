#include "AtmospherePluginPanelModule.h"

#include "../AtmosphereFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

namespace gte {

void AtmospherePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    AtmosphereSettings edit = m_feature.Settings();
    if (BuildAtmospherePanel(m_ctx, edit, m_feature.AerialSnapshot(rg::ViewScope::Primary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateAtmospherePanelModule(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer&, const rg::RenderGraph&)
{
    // Only invoked for the module registered as "Atmosphere", which holds an AtmosphereFeature.
    return std::make_unique<AtmospherePluginPanelModule>(ctx, fx::FeatureOf<AtmosphereFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Atmosphere", &CreateAtmospherePanelModule);

} // namespace gte
