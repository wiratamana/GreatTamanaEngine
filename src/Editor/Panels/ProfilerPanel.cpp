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

void BuildGpuTimingSection()
{
    ImGui::SeparatorText("GPU Timing");

    // No GPU timing source feeds the profiler yet, so every row states that.
    for (const char* label : { "Game View", "Scene View", "Present" }) {
        ImGui::Text("%s: %s", label, FormatGpuTimingLine(Profiling::SampleStatus::Absent, 0.0).c_str());
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
        m_frozenRows = BuildSortedCpuScopeRows(profiler.LastCompletedFrame());
        m_frozenSummary = profiler.LastCompletedFrame().summary;
    }
    m_wasPaused = paused;

    std::vector<Profiling::FrameGraphPoint> livePoints;
    std::vector<CpuScopeRow> liveRows;
    const std::vector<Profiling::FrameGraphPoint>* points = &m_frozenPoints;
    const std::vector<CpuScopeRow>* rows = &m_frozenRows;
    const Profiling::FrameSummary* summary = &m_frozenSummary;
    if (!paused) {
        livePoints = Profiling::BuildFrameGraphPoints(profiler);
        liveRows = BuildSortedCpuScopeRows(profiler.LastCompletedFrame());
        points = &livePoints;
        rows = &liveRows;
        summary = &profiler.LastCompletedFrame().summary;
    }

    const std::span<const Profiling::FrameGraphPoint> windowed = WindowedTail(*points);

    BuildCpuFrameTimeGraph(windowed, *summary, m_cpuGraphScratch);
    BuildCpuScopeTable(*rows);
    BuildGpuMemorySection(windowed, *summary, m_memoryGraphScratch);
    BuildGpuTimingSection();

    ImGui::End();
}

} // namespace gte
