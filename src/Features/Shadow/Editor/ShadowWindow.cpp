#include "ShadowWindow.h"

#include "ShadowPanel.h"
#include "../ShadowFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void ShadowWindow::OnGui(EditorFrame& /*frame*/)
{
    ShadowSettings edit = m_feature.Settings();
    if (BuildShadowPanel(edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateShadowWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<ShadowWindow>(fx::FeatureOf<ShadowFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Shadow", &CreateShadowWindow);

} // namespace gte
