#include "ShadowPluginPanelModule.h"

#include "ShadowPanel.h"
#include "../ShadowFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void ShadowPluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    ShadowSettings edit = m_feature.Settings();
    if (BuildShadowPanel(m_ctx, edit, m_feature.Snapshot(rg::ViewScope::Primary),
            m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateShadowPanelModule(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<ShadowPluginPanelModule>(ctx, fx::FeatureOf<ShadowFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Shadow", &CreateShadowPanelModule);

} // namespace gte
