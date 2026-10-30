#pragma once

#include "../Profiling/FrameChecks.h"
#include "../Profiling/FrameGraphData.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/Overhead.h"
#include "../Profiling/ProfilingTypes.h"
#include "../Profiling/ScopeEvent.h"
#include "../Profiling/SpikeStore.h"
#include "../Profiling/Verdict.h"
#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

// Pure data shaping and text formatting for the Profiler panel. No ImGui.

// Totals and checks of one finished frame.
struct ProfilerFrameView {
    Profiling::FrameSummary summary{};
    Profiling::FrameTotals totals{};
    Profiling::ProfilerChecks checks{};
};

ProfilerFrameView BuildProfilerFrameView(const Profiling::FrameRecord& frame, bool captureEnabled);

// "[WARN] text (12)". The value is omitted when it is zero.
std::string FormatCheckLine(const Profiling::FrameCheck& check);

// "Blocked 3.20 ms (GPU 2.90, workers 0.10, other 0.20)"
std::string FormatBlockedLine(const Profiling::FrameTotals& totals);

// "12.34 ms"
std::string FormatDuration(double milliseconds);

// "16.67 ms / 60 FPS", or "N/A" for a non-positive time.
std::string FormatFrameTimeSummary(double cpuMilliseconds);

// 128400 -> "128,400"
std::string FormatCount(std::uint64_t value);

// GPU timing text. Only a Present status shows a number; every other status shows its own word.
std::string FormatGpuTimingLine(Profiling::SampleStatus status, double milliseconds);

// One GPU number as text: "2.35 ms", "1.20 ms partial (9 passes folded)" or the status word. Every GPU number
// the panel prints goes through this. Never "N/A", and a status other than Present or Partial never shows a number.
std::string FormatGpuMetric(const Profiling::GpuViewMetric& metric);

// "executed 20 of declared 24, recorded 20, disabled by toggle 3"
std::string FormatGpuCoverage(const Profiling::GpuFrameMetrics& gpu);

// GPU time of one render graph regime's executed passes, summed per view. A view that had no executed pass is
// Inactive. A view is Present only when every one of its passes was measured and none was folded.
struct RegimeGpuByView {
    Profiling::GpuViewMetric game;
    Profiling::GpuViewMetric scene;
    Profiling::GpuViewMetric shared;
};
RegimeGpuByView SumRegimeGpuByView(const std::vector<rg::RenderGraphPassMetadata>& passes);

// True when GTE_ENABLE_PROFILER was on for this build.
inline constexpr bool kCpuScopeInstrumentationCompiledIn =
#if GTE_ENABLE_PROFILER
    true;
#else
    false;
#endif

// Empty-table text. Says whether scope data can ever appear in this build.
const char* CpuScopeTableEmptyMessage();

// The verdict line of the top bar. The text carries its own numbers.
struct ProfilerVerdictLine {
    std::string text;
    Profiling::VerdictKind kind = Profiling::VerdictKind::Unknown;
};

// Verdict over the newest frames of the history; `checks` taints it when a self-check fails.
ProfilerVerdictLine BuildVerdictLine(const Profiling::FrameProfiler& profiler, const Profiling::ProfilerChecks& checks);

// "Profiler overhead: estimated 0.04 ms (0.2 %)", with a [WARN] prefix above the warning share, or "not measured".
std::string FormatOverheadLine(const Profiling::OverheadEstimate& overhead);

// One frozen spike as the panel shows it. Every number is already text, made through FormatMetric.
struct SpikeListRow {
    std::uint64_t frameIndex = 0;
    std::string frameTime;      // "50.00 ms"
    std::string overMedian;     // "34.00 ms over the 16.00 ms median"
    std::string classification; // "explained" or "unexplained"
    std::string cause;          // dominant scope, or "marker"; "-" when nothing is known
    std::string gpu;            // GPU frame span with its status
    bool pinned = false;
    bool explained = false;
};

// Newest first, in the order of `spikes`.
std::vector<SpikeListRow> BuildSpikeListRows(std::span<const Profiling::SpikeStore::Row> spikes);

// "Spikes: 3 frozen, 1 of 8 pinned"
std::string FormatSpikeSummary(std::size_t frozen, std::size_t pinned);

} // namespace gte
