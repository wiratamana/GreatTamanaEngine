#include "ProfilerPanel.h"

#include "../EditorContext.h"
#include "../MemoryPanelData.h"
#include "../ProfilerPanelData.h"
#include "../../Profiling/FrameProfiler.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

namespace gte {

namespace {

// Newest frames shown in the graphs. The graph is a few hundred pixels wide.
constexpr std::size_t kGraphWindowFrames = 240;

std::span<const Profiling::FrameGraphPoint> WindowedTail(const std::vector<Profiling::FrameGraphPoint>& points)
{
    const std::span<const Profiling::FrameGraphPoint> all(points);
    return points.size() <= kGraphWindowFrames ? all : all.subspan(points.size() - kGraphWindowFrames);
}

void BuildCaptureAndPauseControls(Profiling::FrameProfiler& profiler)
{
    bool captureEnabled = profiler.IsCaptureEnabled();
    if (ImGui::Checkbox("Capture", &captureEnabled)) {
        profiler.SetCaptureEnabled(captureEnabled);
    }
    if (!captureEnabled) {
        ImGui::TextDisabled("Capture disabled - no new frames are being recorded.");
    }

    ImGui::SameLine();

    bool paused = profiler.IsDisplayPaused();
    if (ImGui::Checkbox("Pause", &paused)) {
        profiler.SetDisplayPaused(paused);
    }
}

void BuildCpuFrameTimeGraph(
    std::span<const Profiling::FrameGraphPoint> windowed, const Profiling::FrameSummary& latest, std::vector<float>& scratch)
{
    ImGui::SeparatorText("CPU Frame Time");

    if (windowed.empty()) {
        ImGui::TextDisabled("Waiting for profiler data...");
        return;
    }

    scratch.clear();
    scratch.reserve(windowed.size());
    for (const Profiling::FrameGraphPoint& point : windowed) {
        scratch.push_back(static_cast<float>(point.cpuMilliseconds));
    }

    const Profiling::FrameGraphRange range = Profiling::ComputeCpuMillisecondsRange(windowed);
    const float scaleMin = range.hasData ? std::min(0.0f, static_cast<float>(range.minMilliseconds)) : 0.0f;
    const float scaleMax = range.hasData ? static_cast<float>(range.maxMilliseconds) : 1.0f;

    ImGui::PlotLines("##CpuFrameTimeGraph", scratch.data(), static_cast<int>(scratch.size()), 0, nullptr, scaleMin,
        scaleMax, ImVec2(0.0f, 80.0f));

    ImGui::Text("%s", FormatFrameTimeSummary(latest.cpuMilliseconds).c_str());
    if (range.hasData) {
        ImGui::Text("Range: %s - %s", FormatDuration(range.minMilliseconds).c_str(),
            FormatDuration(range.maxMilliseconds).c_str());
    }

    // Anything dropped makes the numbers below lower bounds.
    if (latest.lateScopes != 0 || latest.foreignScopes != 0 || latest.lostWorkerEvents != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Dropped: %u late scopes, %u off-thread scopes, %u worker events",
            latest.lateScopes, latest.foreignScopes, latest.lostWorkerEvents);
    }
}

void BuildCpuScopeTable(const std::vector<CpuScopeRow>& rows)
{
    ImGui::SeparatorText("CPU Scopes");

    if (rows.empty()) {
        ImGui::TextDisabled("%s", CpuScopeTableEmptyMessage());
        return;
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("CpuScopesTable", 4, tableFlags, ImVec2(0.0f, 150.0f))) {
        ImGui::TableSetupColumn("Scope");
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const CpuScopeRow& row : rows) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (row.kind == Profiling::ScopeKind::Blocked) {
                ImGui::TextDisabled("%s (blocked)", row.name);
            } else {
                ImGui::TextUnformatted(row.name);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(FormatDuration(row.totalMilliseconds).c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(FormatDuration(row.selfMilliseconds).c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", row.callCount);
        }

        ImGui::EndTable();
    }
}

void BuildChecksSection(const ProfilerFrameView& view)
{
    ImGui::SeparatorText("Checks");

    const std::size_t issues = view.checks.IssueCount();
    if (issues == 0) {
        ImGui::TextDisabled("All %zu checks pass.", view.checks.items.size());
    }
    for (const Profiling::FrameCheck& check : view.checks.items) {
        if (check.status == Profiling::CheckStatus::Ok) {
            continue;
        }
        const ImVec4 color = check.status == Profiling::CheckStatus::Fail ? ImVec4(1.0f, 0.35f, 0.3f, 1.0f)
                                                                           : ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
        ImGui::TextColored(color, "%s", FormatCheckLine(check).c_str());
    }
    ImGui::TextDisabled("%s", FormatBlockedLine(view.totals).c_str());
}

void BuildCpuTreeTable(const ProfilerFrameView& view)
{
    ImGui::SeparatorText("CPU Scopes");

    if (view.tree.empty()) {
        ImGui::TextDisabled("%s", CpuScopeTableEmptyMessage());
        return;
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("CpuTreeTable", 4, tableFlags, ImVec2(0.0f, 220.0f))) {
        ImGui::TableSetupColumn("Scope");
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("% frame", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        for (const CpuTreeRow& row : view.tree) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Indent(static_cast<float>(row.depth) * 12.0f);
            if (row.untracked) {
                ImGui::TextDisabled("%s", row.name);
            } else if (row.kind == Profiling::ScopeKind::Blocked) {
                ImGui::TextDisabled("%s (blocked)", row.name);
            } else {
                ImGui::TextUnformatted(row.name);
            }
            ImGui::Unindent(static_cast<float>(row.depth) * 12.0f);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(FormatDuration(row.totalMilliseconds).c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(FormatDuration(row.selfMilliseconds).c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f%%", row.percentOfFrame);
        }

        ImGui::EndTable();
    }
}

void BuildGpuMemorySection(std::span<const Profiling::FrameGraphPoint> windowed, const Profiling::FrameSummary& latest,
    std::vector<float>& scratch)
{
    ImGui::SeparatorText("GPU Memory");

    const Profiling::MemorySnapshot& memory = latest.memory;
    if (memory.status == Profiling::SampleStatus::Present) {
        ImGui::Text("Current:  %s", FormatBytes(memory.totalBytes).c_str());
        ImGui::Text("Buffers:  %s", FormatBytes(memory.bufferBytes).c_str());
        ImGui::Text("Textures: %s", FormatBytes(memory.textureBytes).c_str());
    } else {
        ImGui::TextDisabled("No memory snapshot this frame.");
    }

    if (windowed.empty()) {
        return;
    }

    // A frame without a snapshot repeats the last value; the range below skips those frames.
    scratch.clear();
    scratch.reserve(windowed.size());
    float lastKnown = 0.0f;
    for (const Profiling::FrameGraphPoint& point : windowed) {
        if (point.memory.status == Profiling::SampleStatus::Present) {
            lastKnown = static_cast<float>(point.memory.totalBytes);
        }
        scratch.push_back(lastKnown);
    }

    const Profiling::MemoryBytesRange range = Profiling::ComputeMemoryBytesRange(windowed);
    if (!range.hasData) {
        ImGui::TextDisabled("Waiting for memory history...");
        return;
    }

    ImGui::PlotLines("##GpuMemoryGraph", scratch.data(), static_cast<int>(scratch.size()), 0, nullptr,
        static_cast<float>(range.minBytes), static_cast<float>(range.maxBytes), ImVec2(0.0f, 60.0f));
}

void BuildGpuTimingSection(const Profiling::GpuFrameMetrics& gpu)
{
    ImGui::SeparatorText("GPU Timing");

    // Labels live here; the profiler module only knows primary and secondary.
    struct Row {
        const char* label;
        const Profiling::GpuViewMetric* metric;
    };
    const Row rows[] = {
        { "Game View", &gpu.views[static_cast<std::size_t>(Profiling::ViewSlot::Primary)] },
        { "Scene View", &gpu.views[static_cast<std::size_t>(Profiling::ViewSlot::Secondary)] },
        { "Shared passes", &gpu.views[static_cast<std::size_t>(Profiling::ViewSlot::Shared)] },
        { "Frame span", &gpu.span },
    };
    for (const Row& row : rows) {
        ImGui::Text("%s: %s", row.label, FormatGpuMetric(*row.metric).c_str());
    }

    const std::uint32_t primaryIndirect = gpu.indirectDraws[static_cast<std::size_t>(Profiling::ViewSlot::Primary)];
    const std::uint32_t secondaryIndirect = gpu.indirectDraws[static_cast<std::size_t>(Profiling::ViewSlot::Secondary)];
    ImGui::TextDisabled("Indirect draws: Game %u, Scene %u", primaryIndirect, secondaryIndirect);
    ImGui::TextDisabled("%s", FormatGpuCoverage(gpu).c_str());
    if (gpu.droppedFrames != 0) {
        ImGui::TextDisabled("GPU frames dropped: %u", gpu.droppedFrames);
    }
}

} // namespace

void ProfilerPanel::Build(EditorContext& /*ctx*/)
{
    ImGui::Begin("Profiler");

    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    BuildCaptureAndPauseControls(profiler);

    const bool paused = profiler.IsDisplayPaused();
    if (paused && !m_wasPaused) {
        m_frozenPoints = Profiling::BuildFrameGraphPoints(profiler);
        m_frozenView = BuildProfilerFrameView(profiler.LastCompletedFrame(), profiler.IsCaptureEnabled());
    }
    m_wasPaused = paused;

    std::vector<Profiling::FrameGraphPoint> livePoints;
    ProfilerFrameView liveView;
    const std::vector<Profiling::FrameGraphPoint>* points = &m_frozenPoints;
    const ProfilerFrameView* view = &m_frozenView;
    if (!paused) {
        livePoints = Profiling::BuildFrameGraphPoints(profiler);
        liveView = BuildProfilerFrameView(profiler.LastCompletedFrame(), profiler.IsCaptureEnabled());
        points = &livePoints;
        view = &liveView;
    }

    const std::span<const Profiling::FrameGraphPoint> windowed = WindowedTail(*points);

    BuildCpuFrameTimeGraph(windowed, view->summary, m_cpuGraphScratch);
    BuildChecksSection(*view);
    ImGui::Checkbox("Group by name", &m_groupByName);
    if (m_groupByName) {
        BuildCpuScopeTable(view->rows);
    } else {
        BuildCpuTreeTable(*view);
    }
    BuildGpuMemorySection(windowed, view->summary, m_memoryGraphScratch);
    BuildGpuTimingSection(view->summary.gpu);

    ImGui::End();
}

} // namespace gte
