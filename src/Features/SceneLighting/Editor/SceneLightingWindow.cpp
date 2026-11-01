#include "SceneLightingWindow.h"

#include "SceneLightingPanel.h"
#include "../SceneLightingFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void SceneLightingWindow::OnGui(EditorFrame& /*frame*/)
{
    SceneLightingSettings edit = m_feature.Settings();
    if (BuildSceneLightingPanel(edit)) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<EditorWindow> CreateSceneLightingWindow(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<SceneLightingWindow>(fx::FeatureOf<SceneLightingFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("SceneLighting", &CreateSceneLightingWindow);

} // namespace gte
