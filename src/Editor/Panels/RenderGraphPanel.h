#pragma once

#include "../../Renderer/RenderGraph/RenderGraphMetadata.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
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

// Node-canvas view of one RenderGraph::Execute() call: pass tree (left),
// graph canvas or sortable table (center), inspector (right), GPU timeline
// (bottom). Both the Offscreen (Game/Scene View) and Present regimes stay
// reachable via the toolbar's own Regime combo - this panel never hides
// either one.
//
// A floating window, not docked into the bottom strip with Memory/Profiler/
// Jobs/Log - a node canvas needs real screen space (see DockLayout.cpp).
//
// Single responsibility: inspecting ONE selected pass's real render-graph
// data. GPU-driven-batch culling stats and render-feature priority editing
// live in their own panel (Panels/RenderFeaturesPanel.h) - this panel never
// mixes "inspect a pass" with "author a render feature" again.
class RenderGraphPanel {
public:
    void Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph,
        rg::RenderPassToggleRegistry& renderPassToggleRegistry);

    // Releases m_texturePreview's live ImGui/Vulkan descriptor, if any.
    // Call explicitly BEFORE ImGui_ImplVulkan_Shutdown() - this class is
    // declared after the ImGui context, so its own destructor alone runs
    // too late.
    void ReleaseTexturePreview() { m_texturePreview.Release(); }

private:
    enum class RegimeChoice : std::uint8_t { Offscreen, Present };
    enum class ViewMode : std::uint8_t { Graph, Table };
    enum class QueueKindFilter : std::uint8_t { All, Graphics, Compute };

    void BuildToolbar(const rg::RenderGraphRegimeMetadata& regime);
    void BuildPassTree(const std::vector<rg::RenderGraphGroupedPassMetadata>& grouped,
        rg::RenderPassToggleRegistry& toggles);
    void DrawPassTreeRow(const rg::RenderGraphGroupedPassMetadata& row, rg::RenderPassToggleRegistry& toggles);
    void DrawDisabledPassRow(const rg::RenderPassToggleState& state, rg::RenderPassToggleRegistry& toggles);
    void BuildGraphCanvas(const GraphLayout& layout, const rg::RenderGraphRegimeMetadata& regime);
    void BuildPassTable(const std::vector<rg::RenderGraphGroupedPassMetadata>& grouped);
    void BuildInspector(const rg::RenderGraphRegimeMetadata& regime, rg::RenderPassToggleRegistry& toggles,
        Renderer& renderer, const rg::RenderGraph& renderGraph);
    void BuildTimeline(const rg::RenderGraphRegimeMetadata& regime);
    void DrawTimelineTrack(const char* label, const rg::RenderGraphRegimeMetadata& regime, const char* kindFilter);
    void RequestTexturePreview(const std::string& resourceName, Renderer& renderer, const rg::RenderGraph& renderGraph);
    void ToggleIsolatePass(const std::string& passName, rg::RenderPassToggleRegistry& toggles);
    std::optional<VkDeviceSize> LookupTrackedResourceSizeByName(const std::string& resourceName, Renderer& renderer) const;

    // Shared search-text + queue-kind predicate for both the pass tree and
    // the pass table - one filter, never two independently-maintained ones.
    bool PassMatchesFilters(const rg::RenderGraphGroupedPassMetadata& row) const;

    bool m_paused = false;
    bool m_forceSingleCapture = false; // "Capture Frame" one-shot refresh-then-freeze.
    rg::RenderGraphMetadata m_frozenMetadata;

    // Resource name -> "<W>x<H>, <FORMAT>" text, rebuilt only while unpaused.
    // Kept in lockstep with m_frozenMetadata so pausing freezes both alike.
    std::unordered_map<std::string, std::string> m_frozenResourceResolutionText;

    RegimeChoice m_regimeChoice = RegimeChoice::Present;
    QueueKindFilter m_kindFilter = QueueKindFilter::All;
    ViewMode m_viewMode = ViewMode::Graph;
    char m_searchFilter[128] = {};
    float m_zoom = 1.0f;
    float m_leftPaneWidth = 260.0f;
    float m_rightPaneWidth = 320.0f;
    float m_timelineHeight = 120.0f;
    std::optional<std::string> m_selectedPassName;

    // Reversible mass-disable state for "Isolate Pass" - see
    // ToggleIsolatePass()'s own doc comment.
    std::optional<std::string> m_isolatedPass;
    std::vector<rg::RenderPassToggleState> m_preIsolationToggleStates;

    RenderGraphTexturePreview m_texturePreview;
};

} // namespace gte
