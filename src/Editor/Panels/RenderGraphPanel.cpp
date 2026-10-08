#include "RenderGraphPanel.h"

#include "../EditorContext.h"
#include "../EditorGpuMemoryNameOverlay.h"
#include "../MemoryPanelData.h"
#include "../RenderGraphDotExport.h"
#include "../../Core/Logging.h"
#include "../../Encoding/CapturedPixelConversion.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h"
#include "../../Renderer/Renderer.h"

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
constexpr float kMinCanvasWidth = 200.0f; // Minimum width always reserved for the graph canvas/table itself.
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
// this resource has no frozen resolution text cached for it.
void BuildResourceResolutionText(
    const std::unordered_map<std::string, std::string>& frozenResolutionText, const std::string& resourceName)
{
    const auto it = frozenResolutionText.find(resourceName);
    if (it == frozenResolutionText.end()) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", it->second.c_str());
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

// Snapshots "<width>x<height>, <format>" for every Texture write/read in
// `regime`, merging into `out` - skips a resource with no live snapshot.
void CollectResourceResolutionText(const rg::RenderGraphRegimeMetadata& regime, const rg::RenderGraph& renderGraph,
    std::unordered_map<std::string, std::string>& out)
{
    const auto collect = [&](const rg::RenderGraphResourceRefMetadata& ref) {
        if (ref.kind != "Texture") {
            return;
        }
        const std::optional<rg::DebugTextureSnapshot> snapshot = renderGraph.DebugTextureSnapshotFor(ref.name);
        if (!snapshot.has_value()) {
            return;
        }
        out[ref.name] = std::to_string(snapshot->target.extent.width) + "x"
            + std::to_string(snapshot->target.extent.height) + ", " + ToString(snapshot->target.format);
    };
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        for (const rg::RenderGraphResourceRefMetadata& write : pass.writes) {
            collect(write);
        }
        for (const rg::RenderGraphResourceRefMetadata& read : pass.reads) {
            collect(read);
        }
    }
}

} // namespace

void RenderGraphPanel::Build(
    EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph, rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    if (!ctx.renderGraphWindowOpen) {
        // Part 4.6 edge case - the window closed (titlebar [x], or the
        // "Window > Render Graph" menu item) while isolating a pass: restore
        // every saved toggle state before returning, exactly like the
        // "Exit Isolation" button press does. Isolation must never silently
        // leak into a closed window's state.
        if (m_isolatedPass.has_value()) {
            ToggleIsolatePass(*m_isolatedPass, renderPassToggleRegistry);
        }
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Graph", &ctx.renderGraphWindowOpen)) {
        ImGui::End();
        return;
    }

    if (!m_paused || m_forceSingleCapture) {
        m_frozenMetadata = rg::BuildRenderGraphMetadata(
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback), {}, {});

        m_frozenResourceResolutionText.clear();
        CollectResourceResolutionText(m_frozenMetadata.offscreenRegime, renderGraph, m_frozenResourceResolutionText);
        CollectResourceResolutionText(m_frozenMetadata.presentRegime, renderGraph, m_frozenResourceResolutionText);
        if (m_forceSingleCapture) {
            m_paused = true;
            m_forceSingleCapture = false;
        }
    }

    const rg::RenderGraphRegimeMetadata& regime = (ctx.renderGraphDisplayedRegime == RenderGraphRegimeChoice::Offscreen)
        ? m_frozenMetadata.offscreenRegime
        : m_frozenMetadata.presentRegime;
    const std::vector<rg::RenderGraphGroupedPassMetadata> grouped = rg::GroupPassMetadataByName(regime.passes);
    const GraphLayout layout = ComputeGraphLayout(regime);

    // Snapshotted BEFORE any checkbox-drawing section below runs this frame,
    // compared again AFTER the whole body - so a toggle flip anywhere (Pass
    // Tree or Isolate Pass) is never missed.
    const std::vector<rg::RenderPassToggleState> toggleStatesBefore = renderPassToggleRegistry.ListAll();

    BuildToolbar(ctx, regime);
    ImGui::Separator();

    const float bodyHeight = ImGui::GetContentRegionAvail().y - m_timelineHeight;
    ImGui::BeginChild("RenderGraphBody", ImVec2(0.0f, bodyHeight), false);
    // Clamp both splitters so neither pane can be dragged to zero/negative
    // width, and the center canvas always keeps a minimum width too - each
    // pane's own bound accounts for the OTHER pane's current width.
    const float totalAvailWidth = ImGui::GetContentRegionAvail().x;
    const float maxLeftPaneWidth =
        std::max(kMinPaneWidth, totalAvailWidth - m_rightPaneWidth - kMinCanvasWidth - 2.0f * kSplitterWidth);
    const float maxRightPaneWidth =
        std::max(kMinPaneWidth, totalAvailWidth - m_leftPaneWidth - kMinCanvasWidth - 2.0f * kSplitterWidth);
    m_leftPaneWidth = std::clamp(m_leftPaneWidth, kMinPaneWidth, maxLeftPaneWidth);
    m_rightPaneWidth = std::clamp(m_rightPaneWidth, kMinPaneWidth, maxRightPaneWidth);

    ImGui::BeginChild("PassTree", ImVec2(m_leftPaneWidth, 0.0f), true);
    BuildPassTree(grouped, renderPassToggleRegistry);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::InvisibleButton("##PassTreeSplitter", ImVec2(kSplitterWidth, bodyHeight));
    if (ImGui::IsItemActive()) {
        m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
    }
    ImGui::SameLine();

    const ImGuiWindowFlags centerFlags = (m_viewMode == ViewMode::Graph) ? ImGuiWindowFlags_HorizontalScrollbar : 0;
    ImGui::BeginChild("GraphCanvas", ImVec2(-m_rightPaneWidth, 0.0f), true, centerFlags);
    if (m_viewMode == ViewMode::Graph) {
        BuildGraphCanvas(layout, regime);
    } else {
        BuildPassTable(grouped);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::InvisibleButton("##InspectorSplitter", ImVec2(kSplitterWidth, bodyHeight));
    if (ImGui::IsItemActive()) {
        m_rightPaneWidth -= ImGui::GetIO().MouseDelta.x;
    }
    ImGui::SameLine();

    ImGui::BeginChild("Inspector", ImVec2(0.0f, 0.0f), true);
    BuildInspector(regime, renderPassToggleRegistry, renderer, renderGraph);
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

void RenderGraphPanel::BuildToolbar(EditorContext& ctx, const rg::RenderGraphRegimeMetadata& regime)
{
    if (ImGui::Button("Capture Frame")) {
        m_forceSingleCapture = true;
        m_paused = false; // Let Build() take one fresh snapshot this frame, then re-freeze.
    }
    ImGui::SameLine();
    ImGui::Checkbox("Pause", &m_paused);
    ImGui::SameLine();

    static constexpr const char* kRegimeLabels[] = { "Offscreen (Game/Scene View)", "Present" };
    int regimeIndex = static_cast<int>(ctx.renderGraphDisplayedRegime);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::Combo("Regime", &regimeIndex, kRegimeLabels, 2)) {
        ctx.renderGraphDisplayedRegime = static_cast<RenderGraphRegimeChoice>(regimeIndex);
    }
    ImGui::SameLine();

    static constexpr const char* kKindLabels[] = { "All", "Graphics", "Compute" };
    int kindIndex = static_cast<int>(m_kindFilter);
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("Queue", &kindIndex, kKindLabels, 3)) {
        m_kindFilter = static_cast<QueueKindFilter>(kindIndex);
    }
    ImGui::SameLine();

    static constexpr const char* kViewLabels[] = { "Graph", "Table" };
    int viewIndex = static_cast<int>(m_viewMode);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("View", &viewIndex, kViewLabels, 2)) {
        m_viewMode = static_cast<ViewMode>(viewIndex);
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##Search", "Filter by pass name", m_searchFilter, sizeof(m_searchFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("Zoom", &m_zoom, 0.5f, 2.0f, "%.1fx");
    ImGui::SameLine();

    double totalGpuMs = 0.0;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (pass.gpuTimingMilliseconds.has_value()) {
            totalGpuMs += *pass.gpuTimingMilliseconds;
        }
    }
    ImGui::Text("Total: %.1f ms", totalGpuMs);

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

bool RenderGraphPanel::PassMatchesFilters(const rg::RenderGraphGroupedPassMetadata& row) const
{
    if (m_searchFilter[0] != '\0' && row.name.find(m_searchFilter) == std::string::npos) {
        return false;
    }
    if (m_kindFilter == QueueKindFilter::All) {
        return true;
    }
    if (row.instances.empty()) {
        return false;
    }
    const std::string& kind = row.instances.front().kind;
    return (m_kindFilter == QueueKindFilter::Graphics) ? (kind == "Graphics") : (kind == "Compute");
}

void RenderGraphPanel::BuildPassTree(
    const std::vector<rg::RenderGraphGroupedPassMetadata>& grouped, rg::RenderPassToggleRegistry& toggles)
{
    std::unordered_map<std::string, const rg::RenderGraphGroupedPassMetadata*> liveByName;
    for (const rg::RenderGraphGroupedPassMetadata& row : grouped) {
        liveByName[row.name] = &row;
        if (!row.instances.empty() && row.instances.front().tagGroupLabel.has_value()) {
            m_lastKnownGroupLabel[row.name] = *row.instances.front().tagGroupLabel;
        }
    }

    // Every known pass, always visible - grouped by its real tag label,
    // never by snapshot presence. `toggles.ListAll()` already includes
    // every pass ever declared or ever mutated this session.
    std::unordered_map<std::string, std::vector<rg::RenderPassToggleState>> byGroup;
    for (const rg::RenderPassToggleState& state : toggles.ListAll()) {
        const auto liveIt = liveByName.find(state.name);
        const rg::RenderGraphGroupedPassMetadata* live = (liveIt == liveByName.end()) ? nullptr : liveIt->second;

        if (live != nullptr) {
            if (!PassMatchesFilters(*live)) {
                continue;
            }
        } else {
            if (m_searchFilter[0] != '\0' && state.name.find(m_searchFilter) == std::string::npos) {
                continue;
            }
            if (m_kindFilter != QueueKindFilter::All) {
                continue; // Queue kind is unknown for a pass not running this frame.
            }
        }

        const auto labelIt = m_lastKnownGroupLabel.find(state.name);
        byGroup[labelIt != m_lastKnownGroupLabel.end() ? labelIt->second : "Ungrouped"].push_back(state);
    }

    // Sorted heading order - deterministic, independent of unordered_map's
    // own bucket layout (which otherwise could visibly jitter row order
    // between sessions for no reason).
    std::vector<std::string> headings;
    headings.reserve(byGroup.size());
    for (const auto& entry : byGroup) {
        headings.push_back(entry.first);
    }
    std::sort(headings.begin(), headings.end());

    for (const std::string& label : headings) {
        if (!ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        for (const rg::RenderPassToggleState& state : byGroup[label]) {
            const auto liveIt = liveByName.find(state.name);
            DrawPassTreeRow(state, liveIt == liveByName.end() ? nullptr : liveIt->second, toggles);
        }
    }
}

// One row for one pass, in every case - no parallel "disabled row"
// function. Checkbox always reflects the registry's own real flag.
void RenderGraphPanel::DrawPassTreeRow(const rg::RenderPassToggleState& state,
    const rg::RenderGraphGroupedPassMetadata* liveRow, rg::RenderPassToggleRegistry& toggles)
{
    ImGui::PushID(state.name.c_str());
    bool enabled = state.enabled;
    if (ImGui::Checkbox("##Enabled", &enabled)) {
        toggles.SetEnabled(state.name, enabled);
    }
    ImGui::SameLine();

    std::string label = state.name;
    bool dim = false;
    if (liveRow == nullptr) {
        // Never say "disabled" unless the registry's own flag is false.
        label += state.enabled
            ? (state.everDeclaredThisSession ? " (not active this frame)" : " (never run yet)")
            : " (disabled)";
        dim = true;
    } else if (liveRow->isCulled) {
        label += " (CULLED)";
        dim = true;
    }

    if (dim) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
    }
    const bool isSelected = m_selectedPassName.has_value() && *m_selectedPassName == state.name;
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        m_selectedPassName = state.name;
    }
    if (dim) {
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

void RenderGraphPanel::BuildGraphCanvas(const GraphLayout& layout, const rg::RenderGraphRegimeMetadata& regime)
{
    constexpr float kColumnWidth = 220.0f;
    constexpr float kRowHeight = 110.0f;
    constexpr float kNodeWidth = 180.0f;
    constexpr float kBaseNodeHeight = 32.0f;
    constexpr float kLineHeight = 14.0f;
    constexpr std::size_t kMaxResourceLines = 6;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    std::unordered_map<std::string, ImVec2> centerByPass;
    std::uint32_t maxColumn = 0;
    std::uint32_t maxRow = 0;

    for (const GraphNodeLayout& node : layout.nodes) {
        maxColumn = std::max(maxColumn, node.column);
        maxRow = std::max(maxRow, node.row);

        const rg::RenderGraphPassMetadata* pass = FindPassByName(regime, node.passName);

        // Real read/write count this node actually has, clamped so one
        // pathological pass with dozens of resources can't blow out the
        // whole row layout - overflow shows "+N more" instead.
        const std::size_t totalRefs = pass != nullptr ? (pass->reads.size() + pass->writes.size()) : 0;
        const std::size_t shownRefs = std::min(totalRefs, kMaxResourceLines);
        const float nodeHeight = kBaseNodeHeight + kLineHeight * static_cast<float>(shownRefs + (totalRefs > shownRefs ? 1 : 0));

        const ImVec2 topLeft(
            origin.x + static_cast<float>(node.column) * kColumnWidth * m_zoom,
            origin.y + static_cast<float>(node.row) * kRowHeight * m_zoom);
        const ImVec2 size(kNodeWidth * m_zoom, nodeHeight * m_zoom);
        const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
        centerByPass[node.passName] = ImVec2((topLeft.x + bottomRight.x) * 0.5f, (topLeft.y + bottomRight.y) * 0.5f);

        const bool culled = pass != nullptr && pass->isCulled;
        const bool selected = m_selectedPassName.has_value() && *m_selectedPassName == node.passName;

        const ImU32 fillColor = culled ? IM_COL32(60, 60, 60, 160) : IM_COL32(70, 110, 160, 220);
        const ImU32 borderColor = selected ? IM_COL32(255, 200, 0, 255) : IM_COL32(20, 20, 20, 255);

        drawList->AddRectFilled(topLeft, bottomRight, fillColor, 4.0f);
        drawList->AddRect(topLeft, bottomRight, borderColor, 4.0f, 0, selected ? 2.5f : 1.0f);
        drawList->AddText(ImVec2(topLeft.x + 6.0f, topLeft.y + 6.0f), IM_COL32_WHITE, node.passName.c_str());

        // Per-node I/O lines - "In : <name>" / "Out: <name>", matching the
        // mockup's own node face layout.
        if (pass != nullptr) {
            float lineY = topLeft.y + 24.0f * m_zoom;
            std::size_t linesDrawn = 0;
            for (const rg::RenderGraphResourceRefMetadata& read : pass->reads) {
                if (linesDrawn >= kMaxResourceLines) {
                    break;
                }
                drawList->AddText(ImVec2(topLeft.x + 6.0f, lineY), IM_COL32(200, 200, 255, 255),
                    ("In : " + read.name).c_str());
                lineY += kLineHeight * m_zoom;
                ++linesDrawn;
            }
            for (const rg::RenderGraphResourceRefMetadata& write : pass->writes) {
                if (linesDrawn >= kMaxResourceLines) {
                    break;
                }
                drawList->AddText(ImVec2(topLeft.x + 6.0f, lineY), IM_COL32(200, 255, 200, 255),
                    ("Out: " + write.name).c_str());
                lineY += kLineHeight * m_zoom;
                ++linesDrawn;
            }
            if (totalRefs > kMaxResourceLines) {
                drawList->AddText(ImVec2(topLeft.x + 6.0f, lineY), IM_COL32(180, 180, 180, 255),
                    ("+" + std::to_string(totalRefs - kMaxResourceLines) + " more").c_str());
            }
        }

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

void RenderGraphPanel::BuildPassTable(const std::vector<rg::RenderGraphGroupedPassMetadata>& grouped)
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("PassTable", 4, flags)) {
        return;
    }
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_PreferSortDescending);
    ImGui::TableSetupColumn("Culled");
    ImGui::TableHeadersRow();

    std::vector<const rg::RenderGraphGroupedPassMetadata*> rows;
    for (const rg::RenderGraphGroupedPassMetadata& row : grouped) {
        if (PassMatchesFilters(row)) {
            rows.push_back(&row);
        }
    }

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
        const bool descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        const int column = specs->Specs[0].ColumnIndex;
        std::sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) {
            if (column == 2) {
                const double av = a->instances.empty() ? 0.0 : a->instances.front().gpuTimingMilliseconds.value_or(0.0);
                const double bv = b->instances.empty() ? 0.0 : b->instances.front().gpuTimingMilliseconds.value_or(0.0);
                return descending ? av > bv : av < bv;
            }
            return descending ? a->name > b->name : a->name < b->name;
        });
    }

    for (const rg::RenderGraphGroupedPassMetadata* row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool isSelected = m_selectedPassName.has_value() && *m_selectedPassName == row->name;
        if (ImGui::Selectable(row->name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            m_selectedPassName = row->name;
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row->instances.empty() ? "?" : row->instances.front().kind.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row->gpuTimingText.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row->isCulled ? "CULLED" : "-");
    }
    ImGui::EndTable();
}

void RenderGraphPanel::BuildInspector(const rg::RenderGraphRegimeMetadata& regime, rg::RenderPassToggleRegistry& toggles,
    Renderer& renderer, const rg::RenderGraph& renderGraph)
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

    ImGui::Text("Selected: %s", pass->name.c_str());
    ImGui::Separator();
    ImGui::Text("Pass Type : %s", pass->drawKind.c_str());
    ImGui::Text("Queue     : %s", pass->kind.c_str());
    ImGui::Text("GPU Time  : %s", pass->gpuTimingText.c_str());
    ImGui::Text("Status    : %s", pass->isCulled ? "Culled" : "Active (Not Culled)");

    ImGui::Separator();
    ImGui::TextUnformatted("INPUT RESOURCES:");
    for (const rg::RenderGraphResourceRefMetadata& read : pass->reads) {
        ImGui::BulletText("%s : %s (%s)", read.name.c_str(), read.kind.c_str(), read.bindingStageLabel.c_str());
    }
    if (pass->reads.empty()) {
        ImGui::TextDisabled("(none declared)");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("OUTPUT TARGETS:");
    for (const rg::RenderGraphResourceRefMetadata& write : pass->writes) {
        ImGui::BulletText("%s (%s)", write.name.c_str(), write.kind.c_str());
        BuildResourceResolutionText(m_frozenResourceResolutionText, write.name);
        if (const std::optional<VkDeviceSize> bytes = LookupTrackedResourceSizeByName(write.name, renderer)) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", FormatBytes(*bytes).c_str());
        }
        if (write.barrierTransitionLabel.has_value()) {
            ImGui::Indent();
            ImGui::TextDisabled("Barrier: %s", write.barrierTransitionLabel->c_str());
            ImGui::Unindent();
        }
    }
    if (pass->writes.empty()) {
        ImGui::TextDisabled("(none declared)");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("MEMORY LIFETIME:");
    ImGui::TextUnformatted("Aliasing: not supported yet (pool only reuses entries across frames).");

    ImGui::Separator();
    if (!pass->isCulled && ImGui::Button("Disable This Pass")) {
        toggles.SetEnabled(pass->name, false);
    }
    ImGui::SameLine();
    if (!pass->writes.empty() && ImGui::SmallButton("View Texture")) {
        RequestTexturePreview(pass->writes.front().name, renderer, renderGraph);
    }
    ImGui::SameLine();
    const bool isolating = m_isolatedPass.has_value() && *m_isolatedPass == pass->name;
    if (ImGui::Button(isolating ? "Exit Isolation" : "Isolate Pass")) {
        ToggleIsolatePass(pass->name, toggles);
    }

    if (m_texturePreview.Descriptor() != VK_NULL_HANDLE) {
        ImGui::Separator();
        ImGui::Text("Preview: %s (%dx%d)", m_texturePreview.ResourceName().c_str(), m_texturePreview.Width(),
            m_texturePreview.Height());
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_texturePreview.Descriptor())),
            ImVec2(256.0f, 256.0f));
    }
}

// Reversible mass-disable: reuses the existing, already-correct toggle
// registry. No new mechanism, no new HTTP route, no new engine state.
void RenderGraphPanel::ToggleIsolatePass(const std::string& passName, rg::RenderPassToggleRegistry& toggles)
{
    if (m_isolatedPass.has_value() && *m_isolatedPass == passName) {
        for (const rg::RenderPassToggleState& saved : m_preIsolationToggleStates) {
            toggles.SetEnabled(saved.name, saved.enabled);
        }
        m_preIsolationToggleStates.clear();
        m_isolatedPass.reset();
        return;
    }

    m_preIsolationToggleStates = toggles.ListAll();
    for (const rg::RenderPassToggleState& state : m_preIsolationToggleStates) {
        if (state.name != passName) {
            toggles.SetEnabled(state.name, false); // No-op for deny-listed names, by design.
        }
    }
    m_isolatedPass = passName;
}

// This engine's RenderGraphResourcePool only reuses a pool entry across
// FRAMES when a new request matches an identical TextureDesc/BufferDesc -
// it does NOT alias two differently-shaped resources within one frame.
// Label it honestly; never draw this as cross-pass aliasing.
void RenderGraphPanel::BuildTimeline(const rg::RenderGraphRegimeMetadata& regime)
{
    DrawTimelineTrack("Graphics Track", regime, "Graphics");
    DrawTimelineTrack("Compute  Track", regime, "Compute");
    ImGui::Separator();
    ImGui::TextDisabled(
        "VRAM Aliasing: not supported yet - pool only reuses entries across frames, never within one frame.");
}

void RenderGraphPanel::DrawTimelineTrack(
    const char* label, const rg::RenderGraphRegimeMetadata& regime, const char* kindFilter)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140.0f);

    double totalMs = 0.0;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (pass.kind == kindFilter && pass.gpuTimingMilliseconds.has_value()) {
            totalMs += *pass.gpuTimingMilliseconds;
        }
    }
    if (totalMs <= 0.0) {
        ImGui::TextDisabled("(no GPU timing data)");
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float barWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kBarHeight = 24.0f;

    float x = cursor.x;
    std::uint32_t segmentIndex = 0;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        if (pass.kind != kindFilter || !pass.gpuTimingMilliseconds.has_value()) {
            continue;
        }
        const float segmentWidth = static_cast<float>(*pass.gpuTimingMilliseconds / totalMs) * barWidth;
        const ImU32 color = PickTimelineSegmentColor(segmentIndex++);
        const ImVec2 segMin(x, cursor.y);
        const ImVec2 segMax(x + segmentWidth, cursor.y + kBarHeight);
        drawList->AddRectFilled(segMin, segMax, color);
        if (segmentWidth > 40.0f) {
            drawList->AddText(ImVec2(segMin.x + 4.0f, segMin.y + 4.0f), IM_COL32_BLACK, pass.name.c_str());
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
        snapshot->target.image, VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format, snapshot->target.extent,
        snapshot->colorState);
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

// Reuses the existing central GpuMemoryTracker (via Renderer::GetMemoryResources())
// - the same data the "Memory" panel already reads. No parallel tracker, no
// new bookkeeping. Linear scan over a tracker that holds, at most, a few
// hundred live GPU resources, called once per selected-pass-inspector-frame
// (not per resource per node per frame) - cache-irrelevant, not a hot path.
std::optional<VkDeviceSize> RenderGraphPanel::LookupTrackedResourceSizeByName(
    const std::string& resourceName, Renderer& renderer) const
{
    for (const GpuMemoryTracker::Entry& entry : renderer.GetMemoryResources()) {
        if (EditorGpuMemoryNameOverlay::GetDebugName(entry.handle) == resourceName) {
            return entry.record.sizeBytes;
        }
    }
    return std::nullopt;
}

} // namespace gte
