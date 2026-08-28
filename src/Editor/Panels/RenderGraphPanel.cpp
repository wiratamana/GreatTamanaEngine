#include "RenderGraphPanel.h"

#include "../EditorContext.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"

#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>

namespace gte {

namespace {

// Mirrors Panels/ProfilerPanel.cpp's own FormatGpuTimingLine() convention
// exactly (never a fabricated "0.00 ms" for Absent/Unsupported - see
// AGENTS.md, "Profiling") - kept as its own small local helper rather than
// reused from ProfilerPanelData.h, since that header's own
// FormatGpuTimingLine() takes a Profiling::GpuPassSample (a different,
// Profiler-specific type this panel has no reason to depend on) - this one
// takes a plain gte::GpuTimingSample instead, exactly what
// rg::PassGpuStats::timing already is.
std::string FormatGpuTiming(const GpuTimingSample& timing)
{
    switch (timing.status) {
    case GpuTimingSample::Status::Present: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", timing.milliseconds);
        return std::string(buffer);
    }
    case GpuTimingSample::Status::Unsupported:
        return "Unsupported";
    case GpuTimingSample::Status::Absent:
    default:
        return "N/A";
    }
}

std::string JoinNames(const std::vector<std::string>& names)
{
    if (names.empty()) {
        return "-";
    }
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += name.empty() ? "(unnamed)" : name;
    }
    return joined;
}

void BuildPassRow(const rg::RenderGraphPassSnapshot& pass)
{
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (pass.isCulled) {
        ImGui::TextDisabled("%s", pass.name.empty() ? "(unnamed)" : pass.name.c_str());
    } else {
        ImGui::TextUnformatted(pass.name.empty() ? "(unnamed)" : pass.name.c_str());
    }

    ImGui::TableSetColumnIndex(1);
    if (pass.isCulled) {
        ImGui::TextDisabled("culled");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Culled: no path from this pass's declared writes to this call's own "
                               "final output(s) was found - see RenderGraphCompiler.h.");
        }
    } else {
        ImGui::Text("%u", pass.stats.drawStats.drawCallCount);
    }

    ImGui::TableSetColumnIndex(2);
    if (!pass.isCulled) {
        ImGui::Text("%u", pass.stats.drawStats.triangleCount);
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(3);
    if (!pass.isCulled) {
        const std::string timingText = FormatGpuTiming(pass.stats.timing);
        ImGui::TextUnformatted(timingText.c_str());
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(4);
    const std::string reads = JoinNames(pass.readNames);
    ImGui::TextUnformatted(reads.c_str());

    ImGui::TableSetColumnIndex(5);
    const std::string writes = JoinNames(pass.writeNames);
    ImGui::TextUnformatted(writes.c_str());
}

void BuildPassTable(const char* tableId, const rg::RenderGraphSnapshot& snapshot)
{
    if (snapshot.passesInExecutionOrder.empty()) {
        ImGui::TextDisabled("No passes were declared the last time this regime ran.");
        return;
    }

    // ImGuiTableFlags_NoSavedSettings is REQUIRED here, not cosmetic - see the
    // matching comment on BuildResourceTable()'s own tableFlags below for the
    // full "why": without it, a column's width/weight can get corrupted (an
    // observed real case: the two stretch columns below, "Reads"/"Writes",
    // persisted into imgui.ini with Weight=nan after this table was first
    // laid out at a degenerate zero/near-zero available width - e.g. the
    // very first frame this panel's dock tab existed but wasn't yet the
    // visible/selected one) and, once written to disk, silently keeps
    // reloading that same NaN weight on every future launch - collapsing
    // both columns down to an unreadable "..", and reportedly crashing the
    // app outright the moment a user tries to drag (expand) one of them
    // back out, since ImGui's stretch-weight redistribution math has no
    // NaN-recovery path. NoSavedSettings makes this table always start each
    // session from the sane, freshly-computed proportional widths declared
    // below, so a corrupted weight can never survive to be reloaded.
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable(tableId, 6, tableFlags)) {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Reads");
        ImGui::TableSetupColumn("Writes");
        ImGui::TableHeadersRow();

        for (const rg::RenderGraphPassSnapshot& pass : snapshot.passesInExecutionOrder) {
            BuildPassRow(pass);
        }

        ImGui::EndTable();
    }
}

// Resolves a resource's first/last-use POSITION (an index into
// snapshot.passesInExecutionOrder's own surviving prefix - see
// RenderGraphResourceSnapshot's own doc comment) back into that pass's real
// NAME - a raw integer index is meaningless to a human reader; a pass name
// is what actually answers "when is this resource alive".
const char* PassNameAtSurvivingIndex(const rg::RenderGraphSnapshot& snapshot, std::int32_t index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= snapshot.passesInExecutionOrder.size()) {
        return "?";
    }
    const std::string& name = snapshot.passesInExecutionOrder[static_cast<std::size_t>(index)].name;
    return name.empty() ? "(unnamed)" : name.c_str();
}

void BuildResourceTable(const char* tableId, const rg::RenderGraphSnapshot& snapshot)
{
    if (snapshot.resources.empty()) {
        ImGui::TextDisabled("No resources were declared the last time this regime ran.");
        return;
    }

    // ImGuiTableFlags_NoSavedSettings is REQUIRED here, not cosmetic - see
    // BuildPassTable()'s own tableFlags comment above for the full "why":
    // this table's own "Lifetime" stretch column hit the exact same
    // persisted-NaN-weight corruption (confirmed directly in a real
    // imgui.ini: "[Table][0xF8B6D9C2,3] ... Column 2 Weight=nan") - fixed the
    // same way, for the same reason.
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable(tableId, 3, tableFlags)) {
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Lifetime");
        ImGui::TableHeadersRow();

        for (const rg::RenderGraphResourceSnapshot& resource : snapshot.resources) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(resource.name.empty() ? "(unnamed)" : resource.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(resource.isImported ? "Imported" : "Transient");

            ImGui::TableSetColumnIndex(2);
            if (resource.firstUsePassIndex < 0) {
                ImGui::TextDisabled("never used (fully culled)");
            } else {
                ImGui::Text("%s -> %s", PassNameAtSurvivingIndex(snapshot, resource.firstUsePassIndex),
                    PassNameAtSurvivingIndex(snapshot, resource.lastUsePassIndex));
            }
        }

        ImGui::EndTable();
    }
}

void BuildRegimeSection(const char* label, const char* idSuffix, const rg::RenderGraphSnapshot& snapshot)
{
    ImGui::SeparatorText(label);

    const std::string passTableId = std::string("RgPasses##") + idSuffix;
    BuildPassTable(passTableId.c_str(), snapshot);

    ImGui::Spacing();
    ImGui::TextDisabled("Resources");
    const std::string resourceTableId = std::string("RgResources##") + idSuffix;
    BuildResourceTable(resourceTableId.c_str(), snapshot);
}

} // namespace

void RenderGraphPanel::Build(EditorContext& /*ctx*/, const rg::RenderGraph& renderGraph)
{
    ImGui::Begin("Render Graph");

    const bool wasPaused = m_paused;
    ImGui::Checkbox("Pause", &m_paused);
    ImGui::SameLine();
    ImGui::TextDisabled("(freezes only this panel's own display - the graph keeps running underneath)");

    // See RenderGraphPanel.h's own doc comment for why direction 2 (staying
    // paused) and direction 3 (un-pausing) both need no code here at all -
    // every section below simply reads m_paused's current value each frame,
    // exactly like ProfilerPanel::Build() already does for its own Pause.
    if (m_paused && !wasPaused) {
        m_frozenOffscreenSnapshot = renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);
        m_frozenPresentSnapshot = renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback);
    }

    const rg::RenderGraphSnapshot& offscreenSnapshot = m_paused
        ? m_frozenOffscreenSnapshot
        : renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);
    const rg::RenderGraphSnapshot& presentSnapshot = m_paused
        ? m_frozenPresentSnapshot
        : renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback);

    BuildRegimeSection("Offscreen Regime (Game View + Scene View)", "Offscreen", offscreenSnapshot);
    ImGui::Spacing();
    BuildRegimeSection("Pipelined Regime (Present)", "Present", presentSnapshot);

    ImGui::Spacing();
    ImGui::SeparatorText("Export");
    ImGui::BeginDisabled();
    ImGui::Button("Export DOT");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Planned for Phase 9, once this panel's own ImGui-list data model has proven itself - "
                           "see RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md.");
    }

    ImGui::End();
}

} // namespace gte
