#include "ToneMappingWindow.h"

#include "ToneMappingPanel.h"
#include "../ToneMappingFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void ToneMappingWindow::OnGui(EditorFrame& /*frame*/)
{
    ToneMappingSettings edit = m_feature.Settings();
    if (BuildToneMappingPanel(
            edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateToneMappingWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<ToneMappingWindow>(fx::FeatureOf<ToneMappingFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("ToneMapping", &CreateToneMappingWindow);

} // namespace gte
