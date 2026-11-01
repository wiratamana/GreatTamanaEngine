#include "GtaoWindow.h"

#include "GtaoPanel.h"
#include "../GtaoFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void GtaoWindow::OnGui(EditorFrame& /*frame*/)
{
    GtaoSettings edit = m_feature.Settings();
    if (BuildGtaoPanel(edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateGtaoWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<GtaoWindow>(fx::FeatureOf<GtaoFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Gtao", &CreateGtaoWindow);

} // namespace gte
