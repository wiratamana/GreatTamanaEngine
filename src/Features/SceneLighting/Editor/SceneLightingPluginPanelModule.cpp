#include "SceneLightingPluginPanelModule.h"

#include "SceneLightingPanel.h"
#include "../SceneLightingFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void SceneLightingPluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    SceneLightingSettings edit = m_feature.Settings();
    if (BuildSceneLightingPanel(edit)) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateSceneLightingPanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<SceneLightingPluginPanelModule>(fx::FeatureOf<SceneLightingFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("SceneLighting", &CreateSceneLightingPanelModule);

} // namespace gte
