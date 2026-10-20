#include "VolumetricLightPluginPanelModule.h"

#include "VolumetricLightPanel.h"
#include "../VolumetricLightFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void VolumetricLightPluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    VolumetricLightSettings edit = m_feature.Settings();
    if (BuildVolumetricLightPanel(
            edit, m_feature.Snapshot(rg::ViewScope::GameView), m_feature.Snapshot(rg::ViewScope::SceneView))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateVolumetricLightPanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<VolumetricLightPluginPanelModule>(fx::FeatureOf<VolumetricLightFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("VolumetricLight", &CreateVolumetricLightPanelModule);

} // namespace gte
