#include "GpuDrivenScenePluginPanelModule.h"

#include "GpuDrivenScenePanel.h"
#include "../GpuDrivenSceneFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"

#include <memory>

namespace gte {

void GpuDrivenScenePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    GpuDrivenSceneSettings edit = m_feature.Settings();
    if (BuildGpuDrivenScenePanel(edit, m_feature.Supported(), m_feature.Snapshot(rg::ViewScope::Primary),
            m_feature.Snapshot(rg::ViewScope::Secondary))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateGpuDrivenScenePanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<GpuDrivenScenePluginPanelModule>(fx::FeatureOf<GpuDrivenSceneFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("GpuDrivenScene", &CreateGpuDrivenScenePanelModule);

} // namespace gte
