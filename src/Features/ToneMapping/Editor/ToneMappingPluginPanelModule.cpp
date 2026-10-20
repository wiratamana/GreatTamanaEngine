#include "ToneMappingPluginPanelModule.h"

#include "ToneMappingPanel.h"
#include "../ToneMappingFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void ToneMappingPluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    ToneMappingSettings edit = m_feature.Settings();
    if (BuildToneMappingPanel(
            edit, m_feature.Snapshot(rg::ViewScope::GameView), m_feature.Snapshot(rg::ViewScope::SceneView))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateToneMappingPanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<ToneMappingPluginPanelModule>(fx::FeatureOf<ToneMappingFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("ToneMapping", &CreateToneMappingPanelModule);

} // namespace gte
