#include "ProfilerPanel.h"

#include "ProfilerGraphWidget.h"
#include "ProfilerTableWidget.h"
#include "../EditorFrame.h"
#include "../MemoryPanelData.h"
#include "../ProfilerPanelData.h"
#include "../../Application/ProfilerScopeCatalog.h"
#include "../../Profiling/FrameProfiler.h"
#include "../../Profiling/NameTable.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace gte {

namespace {

constexpr ImVec4 kColorBad(1.0f, 0.35f, 0.3f, 1.0f);
constexpr ImVec4 kColorWarn(1.0f, 0.75f, 0.2f, 1.0f);
constexpr ImVec4 kColorGood(0.45f, 0.85f, 0.45f, 1.0f);
constexpr ImVec4 kColorQuiet(0.6f, 0.6f, 0.6f, 1.0f);

constexpr float kGraphHeight = 110.0f;
constexpr float kMinTableHeight = 120.0f;
// Row caps per lane (Main, Workers, Gpu); a busy lane never hides another.
constexpr Profiling::LaneRowCaps kTableLaneCaps{ 384, 64, 128 };
constexpr double kLimitMessageSeconds = 3.0;
constexpr float kNamesWarnShare = 0.9f;

ImVec4 VerdictColor(Profiling::VerdictKind kind)
{
    switch (kind) {
    case Profiling::VerdictKind::Unreliable:
    case Profiling::VerdictKind::Stalled: return kColorBad;
    case Profiling::VerdictKind::PresentLimited: return kColorWarn;
    case Profiling::VerdictKind::Unknown: return kColorQuiet;
    case Profiling::VerdictKind::GpuLimited:
    case Profiling::VerdictKind::CpuLimited:
    case Profiling::VerdictKind::Serial: return kColorGood;
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void BuildCaptureAndPauseControls(Profiling::FrameProfiler& profiler)
{
    bool captureEnabled = profiler.IsCaptureEnabled();
    if (ImGui::Checkbox("Capture", &captureEnabled)) {
        profiler.SetCaptureEnabled(captureEnabled);
    }
    ImGui::SameLine();
    bool paused = profiler.IsDisplayPaused();
    if (ImGui::Checkbox("Pause", &paused)) {
        profiler.SetDisplayPaused(paused);
    }
}

void BuildViewControls(TableOptions& options, bool& showSelf)
{
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("View:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Tree", options.tree)) {
        options.tree = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Flat", !options.tree)) {
        options.tree = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Show:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Inclusive", !showSelf)) {
        showSelf = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Self time", showSelf)) {
        showSelf = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Technical names", &options.showTechnicalNames);
}

void BuildSummary(const std::string& summary, const ProfilerVerdictLine& verdict, const ProfilerFrameView& view,
    Profiling::FrameProfiler& profiler)
{
    ImGui::PushStyleColor(ImGuiCol_Text, VerdictColor(verdict.kind));
    ImGui::TextWrapped("%s", summary.c_str());
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", DescribeVerdict(verdict.kind));

    const Profiling::OverheadEstimate overhead =
        Profiling::EstimateOverhead(profiler.EventCostNs(), view.totals.scopeCount, view.summary.cpuMilliseconds);
    const std::string overheadLine = FormatOverheadLine(overhead);
    if (overhead.warn) {
        ImGui::TextColored(kColorWarn, "%s", overheadLine.c_str());
    } else {
        ImGui::TextDisabled("%s", overheadLine.c_str());
    }
}

void DrawTopThings(const std::vector<TopThing>& things)
{
    ImGui::SeparatorText("What to look at");
    if (things.empty()) {
        ImGui::TextDisabled("Nothing stands out in this frame.");
        return;
    }
    for (const TopThing& thing : things) {
        ImGui::TextWrapped("%s%s: %s (%.1f%%)  %s", thing.gpu ? "[GPU] " : "", thing.text->info.title.c_str(),
            FormatDuration(thing.ms).c_str(), thing.percent, thing.text->info.hint.c_str());
    }
}

void BuildFilterRow(std::array<char, 64>& filter, TableOptions& options, const TableModel& table, std::size_t trackedCount)
{
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##filter", "Filter", filter.data(), filter.size());

    ImGui::SameLine();
    if (ImGui::Button("Categories")) {
        ImGui::OpenPopup("##categories");
    }
    if (ImGui::BeginPopup("##categories")) {
        for (std::size_t i = 0; i < kScopeCategoryCount; ++i) {
            const ScopeCategory category = static_cast<ScopeCategory>(i);
            bool on = (options.categoryMask & CategoryBit(category)) != 0;
            if (ImGui::Checkbox(CategoryLabel(category), &on)) {
                options.categoryMask = on ? options.categoryMask | CategoryBit(category)
                                          : options.categoryMask & ~CategoryBit(category);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Expand all")) {
        options.collapsed.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Collapse all")) {
        options.collapsed.clear();
        for (const TableRowView& row : table.rows) {
            if (row.row.hasChildren) {
                options.collapsed.push_back(row.row.pathKey);
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Graph lines: %zu / %zu", trackedCount, kMaxTrackedRows);
}

void BuildFooter(const Profiling::ScopeHistory& history, std::uint32_t aggregatesDropped)
{
    const std::uint32_t used = Profiling::GlobalNameTable().Count();
    const bool nearFull = static_cast<float>(used) >= static_cast<float>(Profiling::kMaxNames) * kNamesWarnShare;
    ImGui::TextColored(nearFull ? kColorWarn : kColorQuiet, "Names used: %u / %zu", used, Profiling::kMaxNames);
    ImGui::SameLine();
    ImGui::TextDisabled("  History tracks: %zu / %zu", history.TrackCount(), Profiling::kHistoryTracks);
    if (history.DroppedTracks() != 0) {
        ImGui::SameLine();
        ImGui::TextColored(kColorWarn, "  rows without history this frame: %u", history.DroppedTracks());
    }
    if (aggregatesDropped != 0) {
        ImGui::SameLine();
        ImGui::TextColored(kColorWarn, "  Aggregate rows dropped: %u", aggregatesDropped);
    }
}

void BuildChecksSection(const ProfilerFrameView& view, const ProfilerVerdictLine& verdict, bool& hadIssues)
{
    const std::size_t issues = view.checks.IssueCount();
    if (issues != 0 && !hadIssues) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    hadIssues = issues != 0;
    if (!ImGui::CollapsingHeader("Checks")) {
        return;
    }

    ImGui::TextDisabled("%s", verdict.text.c_str());
    if (issues == 0) {
        ImGui::TextDisabled("All %zu checks pass.", view.checks.items.size());
    }
    for (const Profiling::FrameCheck& check : view.checks.items) {
        if (check.status == Profiling::CheckStatus::Ok) {
            continue;
        }
        ImGui::TextColored(check.status == Profiling::CheckStatus::Fail ? kColorBad : kColorWarn, "%s",
            FormatCheckLine(check).c_str());
    }
    ImGui::TextDisabled("%s", FormatBlockedLine(view.totals).c_str());
}

void BuildGpuMemorySection(std::span<const Profiling::FrameGraphPoint> windowed, const Profiling::FrameSummary& latest,
    std::vector<float>& scratch)
{
    if (!ImGui::CollapsingHeader("GPU Memory")) {
        return;
    }

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

    // A frame without a snapshot repeats the last value; the range skips those frames.
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
    if (!ImGui::CollapsingHeader("GPU Timing")) {
        return;
    }

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

void BuildSpikeSection(Profiling::FrameProfiler& profiler, std::string& pinMessage)
{
    if (!ImGui::CollapsingHeader("Spikes")) {
        return;
    }

    Profiling::SpikeStore& store = profiler.Spikes();
    std::array<Profiling::SpikeStore::Row, Profiling::SpikeStore::kSlots> spikes{};
    const std::size_t count = store.Rows(spikes);
    ImGui::TextDisabled("%s", FormatSpikeSummary(count, store.PinnedCount()).c_str());
    if (!pinMessage.empty()) {
        ImGui::TextColored(kColorBad, "%s", pinMessage.c_str());
    }
    if (count == 0) {
        ImGui::TextDisabled("No frame was slower than twice the recent median.");
        return;
    }

    const std::vector<SpikeListRow> rows = BuildSpikeListRows(std::span<const Profiling::SpikeStore::Row>(spikes.data(), count));
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("SpikeTable", 6, tableFlags)) {
        return;
    }
    ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Over median");
    ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Cause");
    ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableHeadersRow();

    for (const SpikeListRow& row : rows) {
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(row.frameIndex & 0x7fffffff));

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%llu", static_cast<unsigned long long>(row.frameIndex));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.frameTime.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(row.overMedian.c_str());
        ImGui::TableSetColumnIndex(3);
        if (row.explained) {
            ImGui::TextUnformatted(row.classification.c_str());
        } else {
            ImGui::TextColored(kColorWarn, "%s", row.classification.c_str());
        }
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(row.cause.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("GPU span: %s", row.gpu.c_str());
        }
        ImGui::TableSetColumnIndex(5);
        if (ImGui::SmallButton(row.pinned ? "Unpin" : "Pin")) {
            pinMessage.clear();
            if (row.pinned) {
                store.Unpin(row.frameIndex);
            } else if (store.Pin(row.frameIndex) == Profiling::PinResult::LimitReached) {
                pinMessage = "Pin refused: the pin limit is reached, unpin a spike first.";
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void ProfilerPanel::SyncPauseEdge()
{
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    const bool paused = profiler.IsDisplayPaused();
    if (paused && !m_wasPaused) {
        m_frozenRecord = std::make_unique<Profiling::FrameRecord>(profiler.LastCompletedFrame());
    } else if (!paused) {
        m_frozenRecord.reset();
    }
    m_wasPaused = paused;
}

void ProfilerPanel::OnFrame(EditorFrame& /*frame*/)
{
    SyncPauseEdge();
}

void ProfilerPanel::OnGui(EditorFrame& /*frame*/)
{
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    BuildCaptureAndPauseControls(profiler);
    SyncPauseEdge();
    BuildViewControls(m_options, m_showSelf);
    if (!profiler.IsCaptureEnabled()) {
        ImGui::TextDisabled("Capture disabled - no new frames are being recorded.");
    }

    if constexpr (!kCpuScopeInstrumentationCompiledIn) {
        ImGui::TextDisabled("%s", CpuScopeTableEmptyMessage());
        return;
    }

    // Paused: keep showing one copy of the newest frame. Recording continues underneath.
    const bool paused = profiler.IsDisplayPaused();
    const Profiling::FrameRecord& record = m_frozenRecord ? *m_frozenRecord : profiler.LastCompletedFrame();

    if (std::strcmp(m_filterBuffer.data(), m_options.filter.c_str()) != 0) {
        m_options.filter = m_filterBuffer.data();
    }
    m_options.sort = m_showSelf ? Profiling::ScopeSort::SelfDesc : Profiling::ScopeSort::TotalDesc;

    if (!paused) {
        Profiling::BuildFrameGraphPoints(profiler, m_points);
        BuildGraphModel(m_points, profiler.Scopes(), m_tracked,
            m_showSelf ? Profiling::ScopeSeries::SelfMs : Profiling::ScopeSeries::TotalMs, m_visibility, m_graph);
    }
    // Legend toggles and unticked rows apply at once, also on a frozen graph.
    for (std::size_t i = 0; i < 3 && i < m_graph.series.size(); ++i) {
        m_graph.series[i].visible = i == 0 ? m_visibility.frame : i == 1 ? m_visibility.cpuBusy : m_visibility.gpu;
    }
    std::erase_if(m_graph.series, [this](const GraphSeries& s) { return !s.isBase && !IsTracked(m_tracked, s.key); });

    BuildTableModel(record, m_options, kTableLaneCaps, m_textCache, m_table);
    BuildTopThings(record, m_textCache, m_topThings);
    const ProfilerFrameView view = BuildProfilerFrameView(record, profiler.IsCaptureEnabled());
    const ProfilerVerdictLine verdict = BuildVerdictLine(profiler, view.checks);

    BuildSummary(BuildPlainSummary(MakePlainSummaryInput(record.summary)), verdict, view, profiler);
    DrawTopThings(m_topThings);

    ImGui::SeparatorText("Frame time");
    DrawProfilerGraph(m_graph, m_visibility, m_tracked, kGraphHeight);

    BuildFilterRow(m_filterBuffer, m_options, m_table, m_tracked.size());
    if (ImGui::GetTime() < m_limitMessageUntil) {
        ImGui::TextColored(kColorBad, "Graph limit reached, untick one");
    }
    if (m_options.tree && m_table.treeTruncated) {
        ImGui::TextColored(kColorWarn, "Tree detail is incomplete (%u scopes dropped). The Flat view is still exact.",
            record.scopes.Dropped());
    }

    if (m_table.rows.empty()) {
        ImGui::TextDisabled("%s", m_options.filter.empty() && m_options.categoryMask == kAllCategoriesMask
                ? CpuScopeTableEmptyMessage()
                : "No rows match the filter.");
    } else {
        const float height = std::max(kMinTableHeight, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.5f);
        TableDrawState state{ m_tracked, m_options.collapsed, m_options.tree, m_options.showTechnicalNames };
        DrawProfilerTable(m_table, state, height);
        if (state.limitRefused) {
            m_limitMessageUntil = ImGui::GetTime() + kLimitMessageSeconds;
        }
    }
    if (m_table.rowsDropped != 0) {
        ImGui::TextColored(kColorWarn, "%u rows hidden (too many to list). Use the filter to narrow the table.",
            m_table.rowsDropped);
    }

    BuildFooter(profiler.Scopes(), record.aggregates.Dropped());

    BuildChecksSection(view, verdict, m_hadIssues);
    BuildSpikeSection(profiler, m_pinMessage);
    const std::span<const Profiling::FrameGraphPoint> all(m_points);
    BuildGpuMemorySection(all.size() <= kGraphWindowFrames ? all : all.subspan(all.size() - kGraphWindowFrames),
        view.summary, m_memoryScratch);
    BuildGpuTimingSection(view.summary.gpu);
}

} // namespace gte
