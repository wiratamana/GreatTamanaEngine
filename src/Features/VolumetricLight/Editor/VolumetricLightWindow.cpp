#include "VolumetricLightWindow.h"

#include "VolumetricLightPanel.h"
#include "../VolumetricLightFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void VolumetricLightWindow::OnGui(EditorFrame& /*frame*/)
{
    VolumetricLightSettings edit = m_feature.Settings();
    if (BuildVolumetricLightPanel(
            edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateVolumetricLightWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<VolumetricLightWindow>(fx::FeatureOf<VolumetricLightFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("VolumetricLight", &CreateVolumetricLightWindow);

} // namespace gte
