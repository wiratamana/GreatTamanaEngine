#pragma once

#include "../../Renderer/RenderGraph/RenderGraphMetadata.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../EditorWindow.h"
#include "../RenderGraphDisplayText.h"
#include "../RenderGraphLayout.h"
#include "../RenderGraphTexturePreview.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

struct EditorContext;
class Renderer;

namespace rg {
class RenderGraph;
} // namespace rg

// Pass tree, graph canvas or table, inspector and GPU timeline of one render graph execution.
class RenderGraphPanel final : public EditorWindow {
public:
    RenderGraphPanel() : EditorWindow("Render Graph", WindowDock::Floating, true, 1100.0f, 700.0f) {}

    // Releases the texture preview descriptor. Call before ImGui_ImplVulkan_Shutdown().
    void ReleaseTexturePreview() { m_texturePreview.Release(); }

protected:
    void OnFrame(EditorFrame& frame) override;
    void OnGui(EditorFrame& frame) override;

private:
    enum class ViewMode : std::uint8_t { Graph, Table };
    enum class QueueKindFilter : std::uint8_t { All, Graphics, Compute };

    void BuildToolbar(EditorContext& ctx, const rg::RenderGraphRegimeMetadata& regime);
    void BuildPassTree(const std::vector<RenderGraphGroupedPass>& grouped,
        rg::RenderPassToggleRegistry& toggles);
    // `liveRow` is null when the pass produced no snapshot row this frame; it is still drawn.
    void DrawPassTreeRow(const rg::RenderPassToggleState& state,
        const RenderGraphGroupedPass* liveRow, rg::RenderPassToggleRegistry& toggles);
    void BuildGraphCanvas(const GraphLayout& layout, const rg::RenderGraphRegimeMetadata& regime);
    void BuildPassTable(const std::vector<RenderGraphGroupedPass>& grouped);
    void BuildInspector(const rg::RenderGraphRegimeMetadata& regime, rg::RenderPassToggleRegistry& toggles,
        Renderer& renderer, const rg::RenderGraph& renderGraph);
    void BuildTimeline(const rg::RenderGraphRegimeMetadata& regime);
    void DrawTimelineTrack(const char* label, const rg::RenderGraphRegimeMetadata& regime, const char* kindFilter);
    void RequestTexturePreview(const std::string& resourceName, Renderer& renderer, const rg::RenderGraph& renderGraph);
    void ToggleIsolatePass(const std::string& passName, rg::RenderPassToggleRegistry& toggles);
    std::optional<uint64_t> LookupTrackedResourceSizeByName(const std::string& resourceName, Renderer& renderer) const;

    // Search-text and queue-kind filter shared by the pass tree and the pass table.
    bool PassMatchesFilters(const RenderGraphGroupedPass& row) const;

    bool m_paused = false;
    bool m_forceSingleCapture = false; // "Capture Frame" one-shot refresh-then-freeze.
    rg::RenderGraphMetadata m_frozenMetadata;

    // Resource name -> "<W>x<H>, <FORMAT>"; frozen together with m_frozenMetadata.
    std::unordered_map<std::string, std::string> m_frozenResourceResolutionText;

    QueueKindFilter m_kindFilter = QueueKindFilter::All;
    ViewMode m_viewMode = ViewMode::Graph;
    char m_searchFilter[128] = {};
    float m_zoom = 1.0f;
    float m_leftPaneWidth = 260.0f;
    float m_rightPaneWidth = 320.0f;
    float m_timelineHeight = 120.0f;
    std::optional<std::string> m_selectedPassName;

    // Saved toggle states restored when "Isolate Pass" ends.
    std::optional<std::string> m_isolatedPass;
    std::vector<rg::RenderPassToggleState> m_preIsolationToggleStates;

    RenderGraphTexturePreview m_texturePreview;
};

} // namespace gte
