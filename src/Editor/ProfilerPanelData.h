#pragma once

#include "../Profiling/FrameChecks.h"
#include "../Profiling/FrameGraphData.h"
#include "../Profiling/ProfilingTypes.h"
#include "../Profiling/ScopeEvent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Pure data shaping and text formatting for the Profiler panel. No ImGui.

// One CPU scope name summed over the frame.
struct CpuScopeRow {
    const char* name = "";
    Profiling::ScopeKind kind = Profiling::ScopeKind::Work;
    double totalMilliseconds = 0.0; // a scope nested in a scope of the same name is counted twice
    double selfMilliseconds = 0.0;  // total minus direct children
    std::uint32_t callCount = 0;
};

// Rows of the frame's scope events, biggest total first. Instant markers are not rows.
std::vector<CpuScopeRow> BuildSortedCpuScopeRows(const Profiling::FrameRecord& frame);

// One scope event in tree order (parents before children).
struct CpuTreeRow {
    const char* name = "";
    Profiling::ScopeKind kind = Profiling::ScopeKind::Work;
    std::uint16_t depth = 0;
    bool untracked = false;          // the frame time outside every top-level scope
    double totalMilliseconds = 0.0;
    double selfMilliseconds = 0.0;   // total minus direct children
    double percentOfFrame = 0.0;
};

// Scope events in tree order, then one untracked row for the frame time no top-level scope covers.
std::vector<CpuTreeRow> BuildCpuTreeRows(const Profiling::FrameRecord& frame, const Profiling::FrameTotals& totals);

// Everything the panel shows for one finished frame.
struct ProfilerFrameView {
    Profiling::FrameSummary summary{};
    Profiling::FrameTotals totals{};
    Profiling::ProfilerChecks checks{};
    std::vector<CpuScopeRow> rows;
    std::vector<CpuTreeRow> tree;
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

// True when GTE_ENABLE_PROFILER was on for this build.
inline constexpr bool kCpuScopeInstrumentationCompiledIn =
#if GTE_ENABLE_PROFILER
    true;
#else
    false;
#endif

// Empty-table text. Says whether scope data can ever appear in this build.
const char* CpuScopeTableEmptyMessage();

} // namespace gte
