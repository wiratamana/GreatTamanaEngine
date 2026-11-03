#include "TemporalAaWindow.h"

#include "TemporalAaPanel.h"
#include "../TemporalAaFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void TemporalAaWindow::OnGui(EditorFrame& /*frame*/)
{
    TemporalAaSettings edit = m_feature.Settings();
    if (BuildTemporalAaPanel(
            edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateTemporalAaWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<TemporalAaWindow>(fx::FeatureOf<TemporalAaFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("TemporalAa", &CreateTemporalAaWindow);

} // namespace gte
