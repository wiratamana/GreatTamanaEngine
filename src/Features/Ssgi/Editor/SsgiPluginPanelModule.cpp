#include "SsgiPluginPanelModule.h"

#include "SsgiPanel.h"
#include "../SsgiFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void SsgiPluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    SsgiSettings edit = m_feature.Settings();
    if (BuildSsgiPanel(edit, m_feature.Snapshot(rg::ViewScope::GameView), m_feature.Snapshot(rg::ViewScope::SceneView))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateSsgiPanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<SsgiPluginPanelModule>(fx::FeatureOf<SsgiFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Ssgi", &CreateSsgiPanelModule);

} // namespace gte
