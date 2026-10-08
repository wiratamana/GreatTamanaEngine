#include "RenderGraphPanel.h"

#include "../EditorContext.h"
#include "../MemoryPanelData.h"
#include "../RenderGraphDotExport.h"
#include "../../Core/Logging.h"
#include "../../Core/Plugins/RenderFeatureCompositor.h"
#include "../../Encoding/CapturedPixelConversion.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h"
#include "../../Renderer/Renderer.h"
#include "../ImGuiUniqueId.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

namespace {

constexpr float kMinPaneWidth = 120.0f; // Never let a splitter collapse a pane to zero/negative width.
constexpr float kSplitterWidth = 6.0f; // Matches the two InvisibleButton splitters used below.

// Linear search - regime.passes is at most a few dozen entries this frame.
const rg::RenderGraphPassMetadata* FindPassByName(const rg::RenderGraphRegimeMetadata& regime, const std::string& name)
{
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (pass.name == name) {
            return &pass;
        }
    }
    return nullptr;
}

// Prints ", <width>x<height>, <format>" on the current line - a no-op if
// this resource has no live DebugTextureSnapshot right now.
void BuildResourceResolutionText(const rg::RenderGraph& renderGraph, const std::string& resourceName)
{
    const std::optional<rg::DebugTextureSnapshot> snapshot = renderGraph.DebugTextureSnapshotFor(resourceName);
    if (!snapshot.has_value()) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%ux%u, %s", snapshot->target.extent.width, snapshot->target.extent.height, ToString(snapshot->target.format).c_str());
}

// Fixed palette, cycles by index - stable color per timeline segment.
ImU32 PickTimelineSegmentColor(std::uint32_t index)
{
    static constexpr ImU32 kPalette[] = {
        IM_COL32(70, 130, 180, 255), IM_COL32(180, 90, 70, 255), IM_COL32(90, 170, 90, 255),
        IM_COL32(170, 140, 60, 255), IM_COL32(140, 90, 170, 255), IM_COL32(60, 160, 160, 255),
    };
    return kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// Lists every toggle-registered built-in pass, enabled or disabled - a pass
// currently absent from this frame's snapshot (disabled, or never declared
// yet this session) still needs to be re-enablable here.
void BuildAllBuiltInPassesSection(rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    const std::vector<rg::RenderPassToggleState> allStates = renderPassToggleRegistry.ListAll();

    ImGui::SeparatorText("All Built-In Passes");
    if (allStates.empty()) {
        ImGui::TextDisabled("No built-in pass has registered a toggle state yet this session.");
        return;
    }
    for (std::size_t i = 0; i < allStates.size(); ++i) {
        const rg::RenderPassToggleState& state = allStates[i];
        ScopedUniqueId idScope(static_cast<int>(i), "RenderGraphPanel::BuildAllBuiltInPassesSection", state.name.c_str());

        bool enabled = state.enabled;
        if (ImGui::Checkbox("##Enabled", &enabled)) {
            renderPassToggleRegistry.SetEnabled(state.name, enabled);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(state.name.c_str());
        if (!state.everDeclaredThisSession) {
            ImGui::SameLine();
            ImGui::TextDisabled("(never run yet this session)");
        }
    }
}

// One line per GPU-driven-eligible batch: "<batch>: N / M instances
// visible" - N is the GPU-computed, culling-survived count, M is the
// CPU-known real instance count. "pending" (never a fabricated 0) for a
// frame visibleCount hasn't been read back yet at all.
void BuildGpuDrivenBatchesSection(const std::vector<GpuDrivenBatchDebugInfo>& batches)
{
    ImGui::SeparatorText("GPU-Driven Batches (instances culled this frame)");
    if (batches.empty()) {
        ImGui::TextDisabled("No GPU-driven-eligible batch is live this frame (Game View only).");
        return;
    }

    for (const GpuDrivenBatchDebugInfo& batch : batches) {
        const std::string name = batch.batchName.empty() ? "(unnamed)" : batch.batchName;
        if (batch.visibleCount.has_value()) {
            const std::uint32_t culled =
                (batch.instanceCount > *batch.visibleCount) ? (batch.instanceCount - *batch.visibleCount) : 0u;
            ImGui::Text("%s: %u / %u instances visible (%u culled)", name.c_str(), *batch.visibleCount,
                batch.instanceCount, culled);
        } else {
            ImGui::Text("%s: pending / %u instances (GPU readback not yet available)", name.c_str(),
                batch.instanceCount);
        }
    }
}

// One row per registered render-feature entry - an "Enabled" checkbox and
// an editable priority field, both live-mutating `renderFeatureCompositor`
// directly. `renderFeatureCompositor == nullptr` (a degraded build) means
// the widgets still render but any edit is silently a no-op.
void BuildRenderFeaturesSection(
    const std::vector<RenderFeatureDebugEntry>& entries, RenderFeatureCompositor* renderFeatureCompositor)
{
    ImGui::SeparatorText("Render Features");
    if (entries.empty()) {
        ImGui::TextDisabled("No render features are currently registered.");
        return;
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RenderFeatureDebugEntry& entry = entries[i];
        ScopedUniqueId idScope(static_cast<int>(i), "RenderGraphPanel::BuildRenderFeaturesSection", entry.name.c_str());

        bool enabled = entry.enabled;
        if (ImGui::Checkbox("##FeatureEnabled", &enabled) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeatureEnabled(entry.name, enabled);
        }
        ImGui::SameLine();

        int priority = entry.priority;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##FeaturePriority", &priority) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeaturePriority(entry.name, priority);
        }
        ImGui::SameLine();

        ImGui::Text("[%s] %s - blend %s%s", entry.stage.c_str(), entry.name.c_str(), entry.blendMode.c_str(),
            entry.enabled ? "" : " (DISABLED)");

        if (entry.isProjectFeature) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.5f, 1.0f), "[Project]");
        }
    }
}

} // namespace

void RenderGraphPanel::Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry,
    RenderFeatureCompositor* renderFeatureCompositor)
{
    ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Graph")) {
        ImGui::End();
        return;
    }

    if (!m_paused) {
        m_frozenMetadata = rg::BuildRenderGraphMetadata(
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
            gpuDrivenBatchDebugInfo, renderFeatureEntries);
    }

    // Resolves any tab click BEFORE regime/grouped/layout are computed below
    // - otherwise the switch would only visibly take effect next frame.
    BuildRegimeTabs();

    const rg::RenderGraphRegimeMetadata& regime = (m_regimeChoice == RegimeChoice::Offscreen)
        ? m_frozenMetadata.offscreenRegime
        : m_frozenMetadata.presentRegime;
    const std::vector<rg::RenderGraphGroupedPassMetadata> grouped = rg::GroupPassMetadataByName(regime.passes);
    const GraphLayout layout = ComputeGraphLayout(regime);

    // Snapshotted BEFORE any checkbox-drawing section below runs this frame,
    // compared again AFTER the whole body - so a toggle flip anywhere (Pass
    // Tree, "All Built-In Passes", or the Inspector's Render Features tab)
    // is never missed.
    const std::vector<rg::RenderPassToggleState> toggleStatesBefore = renderPassToggleRegistry.ListAll();

    BuildToolbar(regime);
    ImGui::Separator();

    const float bodyHeight = ImGui::GetContentRegionAvail().y - m_timelineHeight;
    ImGui::BeginChild("RenderGraphBody", ImVec2(0.0f, bodyHeight), false);
    // Clamp both splitters so neither pane can be dragged to zero/negative
    // width or crowd its neighbor out entirely - mirrors ProjectPanel.cpp's
    // own splitter-clamp pattern.
    const float totalAvailWidth = ImGui::GetContentRegionAvail().x;
    const float maxPaneWidth = std::max(kMinPaneWidth, totalAvailWidth - kMinPaneWidth - 2.0f * kSplitterWidth);
    m_leftPaneWidth = std::clamp(m_leftPaneWidth, kMinPaneWidth, maxPaneWidth);
    m_rightPaneWidth = std::clamp(m_rightPaneWidth, kMinPaneWidth, maxPaneWidth);

    ImGui::BeginChild("PassTree", ImVec2(m_leftPaneWidth, 0.0f), true);
    BuildPassTree(grouped, renderPassToggleRegistry);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::InvisibleButton("##PassTreeSplitter", ImVec2(kSplitterWidth, bodyHeight));
    if (ImGui::IsItemActive()) {
        m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
    }
    ImGui::SameLine();

    ImGui::BeginChild("GraphCanvas", ImVec2(-m_rightPaneWidth, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    BuildGraphCanvas(layout, regime);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::InvisibleButton("##InspectorSplitter", ImVec2(kSplitterWidth, bodyHeight));
    if (ImGui::IsItemActive()) {
        m_rightPaneWidth -= ImGui::GetIO().MouseDelta.x;
    }
    ImGui::SameLine();

    ImGui::BeginChild("Inspector", ImVec2(0.0f, 0.0f), true);
    BuildInspector(regime, renderPassToggleRegistry, renderer, renderGraph,
        m_frozenMetadata.gpuDrivenBatches, m_frozenMetadata.renderFeatures, renderFeatureCompositor);
    ImGui::EndChild();

    ImGui::EndChild(); // RenderGraphBody

    if (rg::DidRenderPassToggleEnabledStatesChange(toggleStatesBefore, renderPassToggleRegistry.ListAll())) {
        ctx.renderPassToggleRegistryChangedThisFrame = true;
    }

    ImGui::Separator();
    ImGui::BeginChild("Timeline", ImVec2(0.0f, m_timelineHeight), true);
    BuildTimeline(regime);
    ImGui::EndChild();

    ImGui::End();
}

void RenderGraphPanel::BuildRegimeTabs()
{
    if (!ImGui::BeginTabBar("##RegimeTabs")) {
        return;
    }
    if (ImGui::BeginTabItem("Offscreen (Game/Scene View)")) {
        m_regimeChoice = RegimeChoice::Offscreen;
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Present")) {
        m_regimeChoice = RegimeChoice::Present;
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void RenderGraphPanel::BuildToolbar(const rg::RenderGraphRegimeMetadata& regime)
{
    ImGui::Checkbox("Pause", &m_paused);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##Search", "Filter by pass name", m_searchFilter, sizeof(m_searchFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Zoom", &m_zoom, 0.5f, 2.0f, "%.1fx");
    ImGui::SameLine();

    double totalGpuMs = 0.0;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (pass.gpuTimingMilliseconds.has_value()) {
            totalGpuMs += *pass.gpuTimingMilliseconds;
        }
    }
    ImGui::Text("Total GPU: %.2f ms", totalGpuMs);

    ImGui::SameLine();
    if (ImGui::Button("Export DOT")) {
        const std::string path = ExportRenderGraphDotToFile(m_frozenMetadata);
        if (!path.empty()) {
            GTE_LOG_INFO("RenderGraphPanel", "Exported Render Graph DOT file to: " + path);
        } else {
            GTE_LOG_WARNING("RenderGraphPanel", "Failed to export Render Graph DOT file.");
        }
    }

    if (regime.timingSlotBudgetExhausted) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f),
            "Warning: GPU timing slot budget exhausted - some pass timings are stale.");
    }
}

void RenderGraphPanel::BuildPassTree(
    const std::vector<rg::RenderGraphGroupedPassMetadata>& grouped, rg::RenderPassToggleRegistry& toggles)
{
    std::unordered_map<std::string, std::vector<const rg::RenderGraphGroupedPassMetadata*>> byCategory;
    for (const rg::RenderGraphGroupedPassMetadata& row : grouped) {
        if (m_searchFilter[0] != '\0' && row.name.find(m_searchFilter) == std::string::npos) {
            continue;
        }
        const std::string category = row.instances.empty() ? "Unknown" : row.instances.front().category;
        byCategory[category].push_back(&row);
    }

    for (const auto& [category, rows] : byCategory) {
        if (!ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        for (const rg::RenderGraphGroupedPassMetadata* row : rows) {
            ImGui::PushID(row->name.c_str());

            bool enabled = toggles.IsEnabled(row->name);
            if (ImGui::Checkbox("##Enabled", &enabled)) {
                toggles.SetEnabled(row->name, enabled);
            }
            ImGui::SameLine();

            const bool isSelected = m_selectedPassName.has_value() && *m_selectedPassName == row->name;
            if (ImGui::Selectable(row->name.c_str(), isSelected)) {
                m_selectedPassName = row->name;
            }

            ImGui::PopID();
        }
    }

    // The one path that still shows a pass after it has disabled itself out
    // of `grouped` above.
    BuildAllBuiltInPassesSection(toggles);
}

void RenderGraphPanel::BuildGraphCanvas(const GraphLayout& layout, const rg::RenderGraphRegimeMetadata& regime)
{
    constexpr float kColumnWidth = 220.0f;
    constexpr float kRowHeight = 90.0f;
    constexpr float kNodeWidth = 180.0f;
    constexpr float kNodeHeight = 56.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    std::unordered_map<std::string, ImVec2> centerByPass;
    std::uint32_t maxColumn = 0;
    std::uint32_t maxRow = 0;

    for (const GraphNodeLayout& node : layout.nodes) {
        maxColumn = std::max(maxColumn, node.column);
        maxRow = std::max(maxRow, node.row);

        const ImVec2 topLeft(
            origin.x + static_cast<float>(node.column) * kColumnWidth * m_zoom,
            origin.y + static_cast<float>(node.row) * kRowHeight * m_zoom);
        const ImVec2 size(kNodeWidth * m_zoom, kNodeHeight * m_zoom);
        const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
        centerByPass[node.passName] = ImVec2((topLeft.x + bottomRight.x) * 0.5f, (topLeft.y + bottomRight.y) * 0.5f);

        const rg::RenderGraphPassMetadata* pass = FindPassByName(regime, node.passName);
        const bool culled = pass != nullptr && pass->isCulled;
        const bool selected = m_selectedPassName.has_value() && *m_selectedPassName == node.passName;

        const ImU32 fillColor = culled ? IM_COL32(60, 60, 60, 160) : IM_COL32(70, 110, 160, 220);
        const ImU32 borderColor = selected ? IM_COL32(255, 200, 0, 255) : IM_COL32(20, 20, 20, 255);

        drawList->AddRectFilled(topLeft, bottomRight, fillColor, 4.0f);
        drawList->AddRect(topLeft, bottomRight, borderColor, 4.0f, 0, selected ? 2.5f : 1.0f);
        drawList->AddText(ImVec2(topLeft.x + 6.0f, topLeft.y + 6.0f), IM_COL32_WHITE, node.passName.c_str());

        ImGui::SetCursorScreenPos(topLeft);
        ImGui::PushID(node.passName.c_str());
        ImGui::InvisibleButton("##Node", size);
        if (ImGui::IsItemClicked()) {
            m_selectedPassName = node.passName;
        }
        ImGui::PopID();
    }

    for (const GraphEdgeLayout& edge : layout.edges) {
        const auto fromIt = centerByPass.find(edge.fromPass);
        const auto toIt = centerByPass.find(edge.toPass);
        if (fromIt == centerByPass.end() || toIt == centerByPass.end()) {
            continue; // Endpoint culled out of this layout - nothing to draw.
        }
        const ImVec2 p1 = fromIt->second;
        const ImVec2 p4 = toIt->second;
        const ImVec2 p2(p1.x + (p4.x - p1.x) * 0.5f, p1.y);
        const ImVec2 p3(p1.x + (p4.x - p1.x) * 0.5f, p4.y);
        drawList->AddBezierCubic(p1, p2, p3, p4, IM_COL32(200, 200, 200, 160), 1.5f);
    }

    const float canvasWidth = layout.nodes.empty() ? 0.0f : static_cast<float>(maxColumn + 1) * kColumnWidth * m_zoom;
    const float canvasHeight = layout.nodes.empty() ? 0.0f : static_cast<float>(maxRow + 1) * kRowHeight * m_zoom;
    ImGui::Dummy(ImVec2(canvasWidth, canvasHeight)); // Reserves scroll extent for ImGuiWindowFlags_HorizontalScrollbar.
}

void RenderGraphPanel::BuildInspector(const rg::RenderGraphRegimeMetadata& regime,
    rg::RenderPassToggleRegistry& toggles, Renderer& renderer, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    RenderFeatureCompositor* renderFeatureCompositor)
{
    if (!ImGui::BeginTabBar("##InspectorTabs")) {
        return;
    }

    if (ImGui::BeginTabItem("Pass")) {
        BuildSelectedPassTab(regime, toggles, renderer, renderGraph);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Features && Batches")) {
        BuildGpuDrivenBatchesSection(gpuDrivenBatchDebugInfo);
        ImGui::Spacing();
        BuildRenderFeaturesSection(renderFeatureEntries, renderFeatureCompositor);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

void RenderGraphPanel::BuildSelectedPassTab(const rg::RenderGraphRegimeMetadata& regime,
    rg::RenderPassToggleRegistry& toggles, Renderer& renderer, const rg::RenderGraph& renderGraph)
{
    if (!m_selectedPassName.has_value()) {
        ImGui::TextDisabled("Select a pass to inspect it.");
        return;
    }

    const rg::RenderGraphPassMetadata* pass = FindPassByName(regime, *m_selectedPassName);
    if (pass == nullptr) {
        ImGui::TextDisabled("Pass not present in the current snapshot.");
        return;
    }

    ImGui::TextUnformatted(pass->name.c_str());
    ImGui::Separator();
    ImGui::Text("Kind: %s", pass->kind.c_str());
    ImGui::Text("Category: %s", pass->category.c_str());
    ImGui::Text("Draw kind: %s", pass->drawKind.c_str());
    ImGui::Text("View scope: %s", pass->viewScope.c_str());
    ImGui::Text("Culled: %s", pass->isCulled ? "yes" : "no");
    ImGui::Text("Draw calls: %u", pass->drawCallCount);
    ImGui::Text("Triangles: %u", pass->triangleCount);
    ImGui::Text("GPU time: %s", pass->gpuTimingText.c_str());

    if (!pass->isCulled && ImGui::Button("Disable This Pass")) {
        toggles.SetEnabled(pass->name, false);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Writes");
    for (const rg::RenderGraphResourceRefMetadata& write : pass->writes) {
        ImGui::BulletText("%s (%s)", write.name.c_str(), write.kind.c_str());
        if (write.kind == "Texture") {
            BuildResourceResolutionText(renderGraph, write.name);
            ImGui::SameLine();
            ImGui::PushID(write.name.c_str());
            if (ImGui::SmallButton("View")) {
                RequestTexturePreview(write.name, renderer, renderGraph);
            }
            ImGui::PopID();
        }
    }

    ImGui::TextUnformatted("Reads");
    for (const rg::RenderGraphResourceRefMetadata& read : pass->reads) {
        ImGui::BulletText("%s (%s)", read.name.c_str(), read.kind.c_str());
        if (read.kind == "Texture") {
            BuildResourceResolutionText(renderGraph, read.name);
        }
    }

    if (m_texturePreview.Descriptor() != VK_NULL_HANDLE) {
        ImGui::Separator();
        ImGui::Text("Preview: %s (%dx%d)", m_texturePreview.ResourceName().c_str(),
            m_texturePreview.Width(), m_texturePreview.Height());
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_texturePreview.Descriptor())),
            ImVec2(256.0f, 256.0f));
    }
}

// This engine's RenderGraphResourcePool only reuses a pool entry across
// FRAMES when a new request matches an identical TextureDesc/BufferDesc -
// it does NOT alias two differently-shaped resources within one frame.
// Label it honestly; never draw this as cross-pass aliasing.
void RenderGraphPanel::BuildTimeline(const rg::RenderGraphRegimeMetadata& regime)
{
    double totalGpuMs = 0.0;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (pass.gpuTimingMilliseconds.has_value()) {
            totalGpuMs += *pass.gpuTimingMilliseconds;
        }
    }
    if (totalGpuMs <= 0.0) {
        ImGui::TextDisabled("No GPU timing data for this regime yet.");
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float barWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kBarHeight = 28.0f;

    float x = cursor.x;
    std::uint32_t segmentIndex = 0;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (!pass.gpuTimingMilliseconds.has_value()) {
            continue;
        }
        const float segmentWidth = static_cast<float>(*pass.gpuTimingMilliseconds / totalGpuMs) * barWidth;
        const ImU32 color = PickTimelineSegmentColor(segmentIndex++);
        const ImVec2 segMin(x, cursor.y);
        const ImVec2 segMax(x + segmentWidth, cursor.y + kBarHeight);

        drawList->AddRectFilled(segMin, segMax, color);
        if (segmentWidth > 40.0f) {
            drawList->AddText(ImVec2(segMin.x + 4.0f, segMin.y + 6.0f), IM_COL32_BLACK, pass.name.c_str());
        }
        if (ImGui::IsMouseHoveringRect(segMin, segMax)) {
            ImGui::SetTooltip("%s: %.3f ms", pass.name.c_str(), *pass.gpuTimingMilliseconds);
        }
        x += segmentWidth;
    }
    ImGui::Dummy(ImVec2(barWidth, kBarHeight));
}

// General-purpose "preview ANY pass's output, regardless of its current
// Vulkan layout" path: a one-shot CPU capture + re-upload. Only resources
// the engine explicitly finalizes for external sampling carry a guaranteed
// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL - directly wrapping an arbitrary
// pass's own live imageView would be a validation-layer violation waiting
// to happen. The single vkDeviceWaitIdle() per click is a reasonable price
// for a rare, human-driven debug action, never a hot path.
void RenderGraphPanel::RequestTexturePreview(
    const std::string& resourceName, Renderer& renderer, const rg::RenderGraph& renderGraph)
{
    const std::optional<rg::DebugTextureSnapshot> snapshot = renderGraph.DebugTextureSnapshotFor(resourceName);
    if (!snapshot.has_value()) {
        m_texturePreview.Release(); // Safe: gated internally on "is there anything to wait for".
        return;
    }

    renderer.WaitForGpuIdle(); // One-shot, user-driven - not a per-frame cost.
    const Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(
        snapshot->target.image, VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format,
        snapshot->target.extent, snapshot->colorState);
    if (raw.pixels.empty()) {
        m_texturePreview.Release();
        return;
    }

    const std::vector<std::uint8_t> rgba8 = Encoding::ConvertCapturedPixelsToRgba8(raw);
    if (rgba8.empty()) {
        m_texturePreview.Release(); // Unrecognized format.
        return;
    }

    try {
        m_texturePreview.Request(renderer.GetVulkanContextInfo().device, resourceName,
            renderer.CreateTexture2D(rgba8.data(), raw.width, raw.height, "RenderGraphPreview"));
    } catch (const std::exception&) {
        m_texturePreview.Release(); // vmaCreateImage/vkCreateImageView failure - rare, not fatal.
    }
}

} // namespace gte
