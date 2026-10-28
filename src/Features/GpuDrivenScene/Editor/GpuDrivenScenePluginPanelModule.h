#pragma once

#include "GpuDrivenScenePanelText.h"
#include "../../../Core/EditorPanelModule.h"

#include <cstdint>

namespace gte {

namespace rg {
class RenderGraph;
}

class GpuDrivenSceneFeature;

// Draws the "Culling" panel through the generic plugin-panel loop. A permanent built-in panel.
class GpuDrivenScenePluginPanelModule final : public IEditorPanelModule_v1 {
public:
    GpuDrivenScenePluginPanelModule(GpuDrivenSceneFeature& feature, const rg::RenderGraph& graph)
        : m_feature(feature)
        , m_graph(graph)
    {
    }

    GpuDrivenScenePluginPanelModule(const GpuDrivenScenePluginPanelModule&) = delete;
    GpuDrivenScenePluginPanelModule& operator=(const GpuDrivenScenePluginPanelModule&) = delete;

    const char* GetPanelName() const override { return "Culling"; }

    void BuildPanel(IPluginPanelDrawContext& ctx) override;

private:
    // Regroups the last frame's per-pass GPU times by view and owning feature.
    void RefreshTimes();

    GpuDrivenSceneFeature& m_feature;
    const rg::RenderGraph& m_graph;
    CullTimeSplits m_times;
    std::uint32_t m_drawsSinceRefresh = 0;
    bool m_hasTimes = false;
};

} // namespace gte
