#include "GpuDrivenScenePluginPanelModule.h"

#include "GpuDrivenScenePanel.h"
#include "../GpuDrivenSceneFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"
#include "../../../Editor/EditorContext.h"

#include <memory>

namespace gte {

GpuDrivenScenePluginPanelModule::GpuDrivenScenePluginPanelModule(EditorContext& ctx, GpuDrivenSceneFeature& feature)
    : m_ctx(ctx)
    , m_feature(feature)
{
    m_ctx.viewCull = m_feature.Link();
}

void GpuDrivenScenePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    m_feature.FlushRequest();
    GpuDrivenSceneSettings edit = m_feature.Settings();
    if (BuildGpuDrivenScenePanel(m_ctx, edit, m_feature.Supported(), m_feature.Snapshot(rg::ViewScope::GameView),
            m_feature.Snapshot(rg::ViewScope::SceneView))) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateGpuDrivenScenePanelModule(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<GpuDrivenScenePluginPanelModule>(ctx, fx::FeatureOf<GpuDrivenSceneFeature>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("GpuDrivenScene", &CreateGpuDrivenScenePanelModule);

} // namespace gte
