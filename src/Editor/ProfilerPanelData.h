#pragma once

#include "../Profiling/FrameGraphData.h"
#include "../Profiling/ProfilingTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Phase 7 (PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md): the Editor
// "Profiler" panel's pure, ImGui-free data-shaping/formatting logic -
// follows MemoryPanelData.h's own template exactly (plain reshape/format
// functions, Tier-1-tested despite living under src/Editor/ - see
// AGENTS.md, "Testability & Regression Safety"). Panels/ProfilerPanel.cpp
// is the thin ImGui-facing wrapper around these.

// Returns a copy of `frame.cpuScopes[0..frame.cpuScopeCount)` sorted by
// `totalMilliseconds` descending (biggest contributor first - the same
// convention MemoryPanelData.h's BuildMemoryRows() already established for
// the "Memory" panel). Deliberately respects `cpuScopeCount`, NOT the fixed
// array's own capacity (kMaxCpuScopesPerFrame) - the array may carry stale
// data past cpuScopeCount from a prior frame's larger sample set (it's a
// plain, uninitialized-past-count std::array slot, never cleared
// individually - see ProfilingTypes.h).
std::vector<Profiling::CpuScopeSample> BuildSortedCpuScopeRows(const Profiling::FrameSample& frame);

// Formats a millisecond duration as "12.34 ms" - pulled out as its own pure
// function so Panels/ProfilerPanel.cpp and its test share the exact same
// formatting, same rationale as MemoryPanelData.h's FormatBytes().
std::string FormatDuration(double milliseconds);

// Formats a frame's CPU time as "<duration> / <fps> FPS" (e.g.
// "16.67 ms / 60 FPS") - fps is derived as 1000.0 / milliseconds. Returns a
// "N/A" fallback for a non-positive `cpuMilliseconds` (division-by-zero
// guard - a genuinely zero/negative frame time is never expected in
// practice, but this keeps the function total rather than producing inf/nan
// text for a pathological input).
std::string FormatFrameTimeSummary(double cpuMilliseconds);

// Formats an integer count with thousands separators (e.g. 128400 ->
// "128,400") - used for the draw-call/triangle-count section.
std::string FormatCount(std::uint64_t value);

// The draw-call/triangle-count display's own collapsed tri-state -> bool
// resolution (see ProfilingTypes.h's GpuPassSample::countStatus). This is
// the ONE place that collapse happens - deliberately NOT inside
// FrameGraphData.h/ProfilingTypes.h themselves, since a future Phase 6 CSV
// exporter may still want the full tri-state rather than this panel's own
// simplified "available or not" view.
struct GpuPassCountDisplay {
    // False when countStatus != GpuSampleStatus::Present (including an
    // out-of-range `pass`) - a caller must show "N/A", never "0 draw
    // calls"/"0 triangles", when this is false (see ProfilingTypes.h's own
    // "never collapse absent into zero" rule).
    bool available = false;
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;
};

// Resolves `frame.gpuPasses[pass]`'s draw-call/triangle-count tri-state into
// a display-ready value. Bounds-checks `pass` unconditionally (active in
// Debug AND Release), mirroring FrameGraphData.h's own
// ComputeGpuMillisecondsRange() convention - an out-of-range `pass` simply
// reports `available == false`, never an out-of-bounds read.
GpuPassCountDisplay ResolveGpuPassCounts(const Profiling::FrameSample& frame, Profiling::GpuPass pass);

// Short, human-readable label for a GpuPass value ("Game View"/"Scene
// View"/"Present") - an out-of-range value falls back to "Unknown Pass"
// rather than reading out of bounds/crashing.
const char* ToString(Profiling::GpuPass pass);

// Formats one GPU pass's TIMING line (see ProfilingTypes.h's
// GpuPassSample::timingStatus/milliseconds) - honestly reports "N/A" for
// Absent/Unsupported (Phase 4, Vulkan GPU timestamp queries, is not
// implemented yet - see PROFILER_STRATEGY_v2.md) rather than ever
// fabricating "0.00 ms". Takes the whole GpuPassSample (not a resolved
// bool+value pair, unlike ResolveGpuPassCounts() above) so a future real
// Present value flows through unchanged with zero call-site changes needed.
std::string FormatGpuTimingLine(const Profiling::GpuPassSample& pass);

// Whether GTE_PROFILE_SCOPE actually records anything anywhere in this
// build, i.e. whether GTE_ENABLE_PROFILER was ON when this translation unit
// itself was compiled (see AGENTS.md, "Profiling" - that switch compiles
// ScopeTimer's body down to a true empty no-op, meaning every CPU scope call
// site in the engine compiles away entirely). A plain compile-time constant,
// not a runtime probe - exactly as permanent/unchanging for the life of the
// process as GTE_ENABLE_EDITOR itself is. GTE_ENABLE_PROFILER is
// PUBLIC-defined on the gte_core target (see the root CMakeLists.txt), the
// same way GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL already are, so it's
// visible here exactly like those two already are.
inline constexpr bool kCpuScopeInstrumentationCompiledIn =
#if GTE_ENABLE_PROFILER
    true;
#else
    false;
#endif

// The CPU Scopes table's own empty-state message - distinguishes "this
// build can never show scope data, by design" (kCpuScopeInstrumentationCompiledIn
// == false) from "no scopes have been recorded yet this particular frame"
// (still true, transiently, even in a build where it's compiled in - e.g.
// the very first frame, or right after Capture was re-enabled). See
// PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Changelog #1.
const char* CpuScopeTableEmptyMessage();

} // namespace gte
