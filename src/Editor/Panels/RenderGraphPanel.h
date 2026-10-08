#pragma once

#include "../../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../../Renderer/Culling/GpuDrivenBatchDebugInfo.h"
#include "../../Renderer/RenderGraph/RenderGraphMetadata.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../RenderGraphLayout.h"
#include "../RenderGraphTexturePreview.h"
#include "../EditorLayer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

struct EditorContext;
class Renderer;
class RenderFeatureCompositor;

namespace rg {
class RenderGraph;
} // namespace rg

// Node-canvas view of one RenderGraph::Execute() call: pass tree (left),
// graph canvas (center), inspector (right), GPU timeline (bottom). Both the
// Offscreen (Game/Scene View) and Present regimes stay reachable via a
// toolbar tab - this panel never hides either one.
//
// A floating window, not docked into the bottom strip with Memory/Profiler/
// Jobs/Log - a node canvas needs real screen space (see DockLayout.cpp).
class RenderGraphPanel {
public:
    void Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph,
        const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
        const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
        rg::RenderPassToggleRegistry& renderPassToggleRegistry,
        RenderFeatureCompositor* renderFeatureCompositor);

    // Releases m_texturePreview's live ImGui/Vulkan descriptor, if any.
    // Call explicitly BEFORE ImGui_ImplVulkan_Shutdown() - this class is
    // declared after the ImGui context, so its own destructor alone runs
    // too late.
    void ReleaseTexturePreview() { m_texturePreview.Release(); }

private:
    enum class RegimeChoice : std::uint8_t { Offscreen, Present };

    void BuildRegimeTabs();
    void BuildToolbar(const rg::RenderGraphRegimeMetadata& regime);
    void BuildPassTree(const std::vector<rg::RenderGraphGroupedPassMetadata>& grouped,
        rg::RenderPassToggleRegistry& toggles);
    void BuildGraphCanvas(const GraphLayout& layout, const rg::RenderGraphRegimeMetadata& regime);
    void BuildInspector(const rg::RenderGraphRegimeMetadata& regime, rg::RenderPassToggleRegistry& toggles,
        Renderer& renderer, const rg::RenderGraph& renderGraph,
        const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
        const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
        RenderFeatureCompositor* renderFeatureCompositor);
    void BuildSelectedPassTab(const rg::RenderGraphRegimeMetadata& regime, rg::RenderPassToggleRegistry& toggles,
        Renderer& renderer, const rg::RenderGraph& renderGraph);
    void BuildTimeline(const rg::RenderGraphRegimeMetadata& regime);
    void RequestTexturePreview(const std::string& resourceName, Renderer& renderer, const rg::RenderGraph& renderGraph);

    bool m_paused = false;
    rg::RenderGraphMetadata m_frozenMetadata;

    // Resource name -> "<W>x<H>, <FORMAT>" text, rebuilt only while unpaused.
    // Kept in lockstep with m_frozenMetadata so pausing freezes both alike.
    std::unordered_map<std::string, std::string> m_frozenResourceResolutionText;

    RegimeChoice m_regimeChoice = RegimeChoice::Present;
    char m_searchFilter[128] = {};
    float m_zoom = 1.0f;
    float m_leftPaneWidth = 260.0f;
    float m_rightPaneWidth = 320.0f;
    float m_timelineHeight = 120.0f;
    std::optional<std::string> m_selectedPassName;

    RenderGraphTexturePreview m_texturePreview;
};

} // namespace gte
