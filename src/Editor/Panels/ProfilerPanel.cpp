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

// How many of the most recent frames the CPU/memory graphs show - enough
// for a few seconds' worth at a typical frame rate without unbounded
// growth, same reasoning as FrameProfiler's own kMaxFrameHistory (see
// ProfilingTypes.h) but deliberately smaller: the graph itself is only a
// few hundred pixels wide, so plotting all of kMaxFrameHistory (300) buys
// nothing a shorter window doesn't already show just as well.
constexpr std::size_t kGraphWindowFrames = 240;

// Narrows `points` down to at most its last kGraphWindowFrames entries -
// zero-copy (std::span), oldest-to-newest, matching every other
// oldest-to-newest convention in this module.
std::span<const Profiling::FrameGraphPoint> WindowedTail(
    const std::vector<Profiling::FrameGraphPoint>& points)
{
    if (points.size() <= kGraphWindowFrames) {
        return std::span<const Profiling::FrameGraphPoint>(points);
    }
    return std::span<const Profiling::FrameGraphPoint>(points).subspan(points.size() - kGraphWindowFrames);
}

// --- Section 1: Capture / Pause controls -----------------------------------

void BuildCaptureAndPauseControls(Profiling::FrameProfiler& profiler, bool& paused,
    std::vector<Profiling::FrameGraphPoint>& frozenPoints, Profiling::FrameSample& frozenLatestFrame)
{
    bool captureEnabled = profiler.IsCaptureEnabled();
    if (ImGui::Checkbox("Capture", &captureEnabled)) {
        profiler.SetCaptureEnabled(captureEnabled);
    }
    if (!captureEnabled) {
        // Re-confirmed against FrameProfiler::BeginFrame()/EndFrame()'s own
        // body (FrameProfiler.cpp) before writing this text (see
        // PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Step 2.4/Changelog
        // #6): with capture disabled, BeginFrame()/EndFrame() both early-
        // return without advancing the ring buffer OR the frame index at
        // all, and RecordCpuScope()/SetGpuPassTiming()/SetGpuPassDrawStats()/
        // SetMemorySnapshot() all no-op the same way - i.e. NO new
        // FrameSample is recorded at all while this is off, not merely a
        // partial suppression of some fields.
        ImGui::TextDisabled("Capture disabled - no new frames are being recorded at all.");
    }

    ImGui::SameLine();

    const bool wasPaused = paused;
    ImGui::Checkbox("Pause", &paused);

    // Direction 1: false -> true (pause just engaged) - capture a frozen
    // snapshot once, this frame only. See ProfilerPanel.h's own comment for
    // why direction 2 (staying true) and direction 3 (true -> false, un-
    // pause) both need no code here at all: every section below simply
    // reads `paused`'s current value each frame.
    if (paused && !wasPaused) {
        frozenPoints = Profiling::BuildFrameGraphPoints(profiler);
        frozenLatestFrame = profiler.LastCompletedFrame();
    }
}

// --- Section 2: CPU frame-time graph ---------------------------------------

void BuildCpuFrameTimeGraph(std::span<const Profiling::FrameGraphPoint> windowed,
    const Profiling::FrameSample& latestFrame, std::vector<float>& scratch)
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

    ImGui::PlotLines("##CpuFrameTimeGraph", scratch.data(), static_cast<int>(scratch.size()), 0, nullptr,
        scaleMin, scaleMax, ImVec2(0.0f, 80.0f));

    ImGui::Text("%s", FormatFrameTimeSummary(latestFrame.cpuFrameMilliseconds).c_str());
    if (range.hasData) {
        ImGui::Text(
            "Range: %s - %s", FormatDuration(range.minMilliseconds).c_str(), FormatDuration(range.maxMilliseconds).c_str());
    }
}

// --- Section 3: CPU scope breakdown table ----------------------------------

void BuildCpuScopeTable(const Profiling::FrameSample& latestFrame)
{
    ImGui::SeparatorText("CPU Scopes");

    const std::vector<Profiling::CpuScopeSample> rows = BuildSortedCpuScopeRows(latestFrame);
    if (rows.empty()) {
        // Distinguishes "scope instrumentation is compiled out entirely"
        // from "genuinely no scopes recorded yet this frame" - see
        // ProfilerPanelData.h's CpuScopeTableEmptyMessage() and
        // PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Changelog #1.
        ImGui::TextDisabled("%s", CpuScopeTableEmptyMessage());
        return;
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("CpuScopesTable", 3, tableFlags, ImVec2(0.0f, 150.0f))) {
        ImGui::TableSetupColumn("Scope");
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const Profiling::CpuScopeSample& row : rows) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.name != nullptr ? row.name : "(unnamed)");

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(FormatDuration(row.totalMilliseconds).c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", row.callCount);
        }

        ImGui::EndTable();
    }
}

// --- Section 4: draw calls / triangles --------------------------------------

void BuildDrawStatsLine(const Profiling::FrameSample& latestFrame, Profiling::GpuPass pass, bool primary)
{
    const GpuPassCountDisplay display = ResolveGpuPassCounts(latestFrame, pass);
    std::string line;
    if (display.available) {
        line = std::string(ToString(pass)) + ": " + FormatCount(display.drawCallCount) + " draw calls, "
            + FormatCount(display.triangleCount) + " triangles";
    } else {
        // Absent means the pass simply didn't run this frame (e.g. a
        // hidden Editor panel, or a minimized OS window) - never a
        // misleading "0 draw calls, 0 triangles" (see ProfilingTypes.h's
        // own tri-state rule).
        line = std::string(ToString(pass)) + ": N/A";
    }

    if (primary) {
        ImGui::TextUnformatted(line.c_str());
    } else {
        // De-emphasized secondary context (see
        // PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Step 3.3 item 4).
        ImGui::TextDisabled("%s", line.c_str());
    }
}

void BuildDrawStatsSection(const Profiling::FrameSample& latestFrame)
{
    ImGui::SeparatorText("Draw Calls / Triangles");

    // Game View first, unconditionally - the primary gameplay number (see
    // PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Step 3.3 item 4).
    BuildDrawStatsLine(latestFrame, Profiling::GpuPass::GameView, true);
    BuildDrawStatsLine(latestFrame, Profiling::GpuPass::SceneView, false);
    BuildDrawStatsLine(latestFrame, Profiling::GpuPass::Present, false);
}

// --- Section 5: GPU memory ---------------------------------------------------

void BuildGpuMemorySection(std::span<const Profiling::FrameGraphPoint> windowed,
    const Profiling::FrameSample& latestFrame, std::vector<float>& scratch)
{
    ImGui::SeparatorText("GPU Memory");

    const Profiling::MemorySnapshot& memory = latestFrame.memory;
    if (memory.status == Profiling::GpuSampleStatus::Present) {
        ImGui::Text("Current:  %s", FormatBytes(memory.totalBytes).c_str());
        ImGui::Text("Buffers:  %s", FormatBytes(memory.bufferBytes).c_str());
        ImGui::Text("Textures: %s", FormatBytes(memory.textureBytes).c_str());
    } else {
        // Absent means no snapshot exists this frame (e.g. no live Renderer
        // yet, or a frame recorded before Phase 5 wired this up) - never a
        // fabricated "0 bytes" (see ProfilingTypes.h's own tri-state rule,
        // and Phase7.md's "Present + 0 bytes means a real empty
        // measurement; Absent means no snapshot exists" requirement).
        ImGui::TextDisabled("No memory snapshot this frame.");
    }

    if (windowed.empty()) {
        return;
    }

    // Build the raw float series for the sparkline: a gap frame (memory
    // status != Present) repeats the previous plotted value rather than
    // dropping to zero - a flat segment reads more honestly than a spike
    // toward zero (see PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Step
    // 3.3 item 5). ComputeMemoryBytesRange() below still correctly EXCLUDES
    // those repeated/gap values from the min/max scan, since it branches on
    // status, not on the raw float array built here.
    scratch.clear();
    scratch.reserve(windowed.size());
    float lastKnown = 0.0f;
    for (const Profiling::FrameGraphPoint& point : windowed) {
        if (point.memory.status == Profiling::GpuSampleStatus::Present) {
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

// --- Section 6: GPU timing -------------------------------------------------

void BuildGpuTimingSection(const Profiling::FrameSample& latestFrame)
{
    ImGui::SeparatorText("GPU Timing");

    // All three named passes, unconditionally - never just GameView (see
    // PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Changelog #3). Real,
    // driver-measured GPU milliseconds since Phase 4
    // (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, 4A-4D all landed) -
    // FormatGpuTimingLine() honestly reports "N/A" for a pass that simply
    // didn't run this frame (a hidden Editor panel, a minimized window, a
    // not-yet-warmed-up Present slot, or Capture currently disabled) or for
    // a device/build that can never produce this measurement at all -
    // never a fabricated "0.00 ms" either way.
    for (const Profiling::GpuPass pass :
        { Profiling::GpuPass::GameView, Profiling::GpuPass::SceneView, Profiling::GpuPass::Present }) {
        const Profiling::GpuPassSample& sample = latestFrame.gpuPasses[static_cast<std::size_t>(pass)];
        ImGui::Text("%s: %s", ToString(pass), FormatGpuTimingLine(sample).c_str());
    }
}

// --- Section 7: export stub -------------------------------------------------

void BuildExportSection()
{
    ImGui::SeparatorText("Export");

    ImGui::BeginDisabled();
    ImGui::Button("Export CSV");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Planned for the benchmark/export phase (Phase 6).");
    }
}

} // namespace

void ProfilerPanel::Build(EditorContext& /*ctx*/)
{
    ImGui::Begin("Profiler");

    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();

    BuildCaptureAndPauseControls(profiler, m_paused, m_frozenPoints, m_frozenLatestFrame);

    // Every section below reads either the frozen snapshot (Pause is on) or
    // fresh live data (Pause is off) - `livePoints` only exists to give the
    // "live" branch somewhere to own its own BuildFrameGraphPoints() result
    // for the rest of this call; m_frozenPoints already owns the paused
    // branch's equivalent as a member.
    std::vector<Profiling::FrameGraphPoint> livePoints;
    const std::vector<Profiling::FrameGraphPoint>* points = nullptr;
    const Profiling::FrameSample* latestFrame = nullptr;

    if (m_paused) {
        points = &m_frozenPoints;
        latestFrame = &m_frozenLatestFrame;
    } else {
        livePoints = Profiling::BuildFrameGraphPoints(profiler);
        points = &livePoints;
        latestFrame = &profiler.LastCompletedFrame();
    }

    const std::span<const Profiling::FrameGraphPoint> windowed = WindowedTail(*points);

    BuildCpuFrameTimeGraph(windowed, *latestFrame, m_cpuGraphScratch);
    BuildCpuScopeTable(*latestFrame);
    BuildDrawStatsSection(*latestFrame);
    BuildGpuMemorySection(windowed, *latestFrame, m_memoryGraphScratch);
    BuildGpuTimingSection(*latestFrame);
    BuildExportSection();

    ImGui::End();
}

} // namespace gte
