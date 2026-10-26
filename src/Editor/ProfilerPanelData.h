#pragma once

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
