#include "VolumetricCloudWindow.h"

#include "VolumetricCloudPanel.h"
#include "../VolumetricCloudFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void VolumetricCloudWindow::OnGui(EditorFrame& /*frame*/)
{
    VolumetricCloudSettings edit = m_feature.Settings();
    if (BuildVolumetricCloudPanel(
            edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateVolumetricCloudWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<VolumetricCloudWindow>(fx::FeatureOf<VolumetricCloudFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("VolumetricCloud", &CreateVolumetricCloudWindow);

} // namespace gte
