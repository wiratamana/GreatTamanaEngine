#include "GtaoPluginPanelModule.h"

#include "GtaoPanel.h"
#include "../GtaoFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void GtaoPluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    GtaoSettings edit = m_feature.Settings();
    if (BuildGtaoPanel(edit, m_feature.Snapshot(rg::ViewScope::GameView), m_feature.Snapshot(rg::ViewScope::SceneView))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateGtaoPanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<GtaoPluginPanelModule>(fx::FeatureOf<GtaoFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Gtao", &CreateGtaoPanelModule);

} // namespace gte
