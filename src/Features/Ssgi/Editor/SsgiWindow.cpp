#include "SsgiWindow.h"

#include "SsgiPanel.h"
#include "../SsgiFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void SsgiWindow::OnGui(EditorFrame& /*frame*/)
{
    SsgiSettings edit = m_feature.Settings();
    if (BuildSsgiPanel(edit, m_feature.Snapshot(rg::ViewScope::Primary), m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateSsgiWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<SsgiWindow>(fx::FeatureOf<SsgiFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Ssgi", &CreateSsgiWindow);

} // namespace gte
