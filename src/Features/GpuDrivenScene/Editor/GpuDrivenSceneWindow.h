#pragma once

#include "GpuDrivenScenePanelText.h"
#include "../../../Editor/EditorWindow.h"

#include <cstdint>

namespace gte {

namespace rg {
class RenderGraph;
}

class GpuDrivenSceneFeature;

// The "Culling" settings and GPU time window.
class GpuDrivenSceneWindow final : public EditorWindow {
public:
    GpuDrivenSceneWindow(GpuDrivenSceneFeature& feature, const rg::RenderGraph& graph)
        : EditorWindow("Culling", WindowDock::Bottom)
        , m_feature(feature)
        , m_graph(graph)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

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
