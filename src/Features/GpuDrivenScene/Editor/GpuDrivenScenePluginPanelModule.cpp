#include "GpuDrivenScenePluginPanelModule.h"

#include "GpuDrivenScenePanel.h"
#include "../GpuDrivenSceneFeature.h"
#include "../../../Core/FeatureKit/FeatureRegistration.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"
#include "../../../Renderer/RenderGraph/RenderGraph.h"
#include "../../../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <memory>
#include <vector>

namespace gte {

namespace {

// The grouping reads a snapshot's strings; once per this many panel draws is plenty.
constexpr std::uint32_t kRefreshEveryDraws = 15;

CullTimeView ViewOf(rg::ViewScope scope) noexcept
{
    switch (scope) {
    case rg::ViewScope::Primary: return CullTimeView::Primary;
    case rg::ViewScope::Secondary: return CullTimeView::Secondary;
    case rg::ViewScope::Shared: break;
    }
    return CullTimeView::Other;
}

} // namespace

void GpuDrivenScenePluginPanelModule::RefreshTimes()
{
    const rg::RenderGraphMetadata metadata = rg::BuildRenderGraphMetadata(
        m_graph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
        m_graph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback), {});
    std::vector<CullTimeRow> rows;
    rows.reserve(metadata.offscreenRegime.passes.size());
    for (const rg::RenderGraphPassMetadata& pass : metadata.offscreenRegime.passes) {
        CullTimeRow row;
        row.view = ViewOf(pass.viewScope);
        row.owner = pass.owningFeatureName;
        row.culled = pass.isCulled;
        // A folded record stands for several passes: its time cannot be given to one view.
        if (pass.gpuFoldedPasses == 0) {
            row.ms = pass.gpuTimingMilliseconds;
        }
        rows.push_back(row);
    }
    m_times = SplitCullTime(rows, kGpuDrivenSceneFeatureName);
    m_hasTimes = true;
}

void GpuDrivenScenePluginPanelModule::BuildPanel(IPluginPanelDrawContext& /*ctx*/)
{
    if (!m_hasTimes || ++m_drawsSinceRefresh >= kRefreshEveryDraws) {
        RefreshTimes();
        m_drawsSinceRefresh = 0;
    }
    GpuDrivenSceneSettings edit = m_feature.Settings();
    if (BuildGpuDrivenScenePanel(edit, m_feature.Supported(), m_feature.Snapshot(rg::ViewScope::Primary),
            m_feature.Snapshot(rg::ViewScope::Secondary), m_times)) {
        m_feature.ApplySettings(edit);
    }
}

namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateGpuDrivenScenePanelModule(
    IEngineFeatureModule& module, EditorContext&, Renderer&, const rg::RenderGraph& graph)
{
    return std::make_unique<GpuDrivenScenePluginPanelModule>(fx::FeatureOf<GpuDrivenSceneFeature>(module), graph);
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("GpuDrivenScene", &CreateGpuDrivenScenePanelModule);

} // namespace gte
