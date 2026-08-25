#pragma once

#include "FrameProfiler.h"
#include "ProfilingTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gte::Profiling {

// Phase 2 (PROFILER_STRATEGY_v2.md / PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md):
// the pure "FrameProfiler history -> plottable points" reshape. This whole
// file is always-compiled (no GTE_ENABLE_EDITOR/GTE_ENABLE_PROFILER
// dependency at all - same tier as FrameProfiler.h/.cpp itself, see
// AGENTS.md's "Profiling" section) and deliberately ImGui-free, so a future
// Phase 6 benchmark-mode CSV exporter (built with -DGTE_ENABLE_EDITOR=OFF)
// and a future Phase 7 Editor "Profiler" panel can both consume the exact
// same functions below rather than each re-deriving this reshape logic
// independently.

// One retained frame's plottable data - one of these per frame currently in
// FrameProfiler's ring buffer, oldest-to-newest, matching HistoryAt()'s own
// existing iteration convention exactly. Deliberately carries ALL THREE
// named GPU passes at once (see PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md,
// Step 3.1) rather than requiring one BuildFrameGraphPoints() call per
// GpuPass - this mirrors FrameSample itself, guarantees every line a future
// graph overlays is built from the exact same underlying frame list in one
// pass, and is strictly cheaper than walking the history multiple times.
struct FrameGraphPoint {
    // Copied verbatim from FrameSample::frameIndex - this is the REAL frame
    // number (never a synthetic 0-based index), and is guaranteed gapless/
    // strictly increasing across every point this module ever returns: a
    // window during which FrameProfiler::SetCaptureEnabled(false) was set
    // simply never advances m_frameIndex at all (see FrameProfiler.cpp's
    // BeginFrame()/EndFrame() early-return paths), so it never leaves a gap
    // for a retained frame to inherit.
    std::uint64_t frameIndex = 0;

    // Copied verbatim from FrameSample::cpuFrameMilliseconds. No tri-state
    // needed here - every frame that reaches FrameProfiler's ring buffer
    // already has a real, measured CPU time (there is no "CPU time didn't
    // happen this frame" concept in this engine).
    double cpuMilliseconds = 0.0;

    // Copied verbatim, index-for-index, from FrameSample::gpuPasses - the
    // exact same type FrameSample itself already uses (see
    // ProfilingTypes.h's GpuPass/GpuPassSample/GpuSampleStatus), never
    // reshaped into a new, Phase-2-specific tri-state representation.
    std::array<GpuPassSample, kGpuPassCount> gpuPasses{};
};

// The Y-axis auto-scale helper's result, for a single series (CPU, or one
// specific GpuPass) over a set of FrameGraphPoints.
struct FrameGraphRange {
    // False if zero points in the requested series had real data (e.g.
    // every retained frame's GameView pass is still GpuSampleStatus::Absent,
    // today's pre-Phase-4 reality) - or if an out-of-range GpuPass value was
    // passed to ComputeGpuMillisecondsRange() (see that function's own
    // comment). A caller must check this FIRST and never plot a meaningless
    // 0.0..0.0 range when it's false.
    bool hasData = false;

    // Only meaningful when hasData is true.
    double minMilliseconds = 0.0;
    double maxMilliseconds = 0.0;
};

// Reshapes `profiler`'s entire currently-retained history
// (HistoryAt(0..HistoryCount()-1)) into a plain, ordered array of points,
// oldest-first - a direct, 1:1, order-preserving transcription, with no
// windowing/"last N frames" parameter by design (a caller wanting only the
// most recent N frames can simply take a suffix of the returned
// std::vector - see PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md, Step 3.1).
// Returns an empty vector for an empty history - never a special-cased
// default point. Safe to call at ANY point in the frame lifecycle,
// including strictly between a BeginFrame()/EndFrame() pair: HistoryAt()/
// HistoryCount() read exclusively from the completed-and-pushed ring
// buffer, never the in-progress scratch FrameSample, so a mid-frame call
// simply never observes the not-yet-completed frame.
std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler& profiler);

// Computes the min/max CPU-millisecond range across every point in
// `points`. CPU time is always real data (see FrameGraphPoint::cpuMilliseconds
// above), so hasData is false only when `points` itself is empty.
//
// Accepts std::span (not const std::vector<FrameGraphPoint>&) so a future
// windowed caller can pass a sub-range of BuildFrameGraphPoints()'s output
// (e.g. std::span(points).subspan(points.size() - 120)) with zero copying -
// a plain std::vector argument still converts implicitly, so no existing
// call site needs to change. See PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md,
// Step 3.1/3.2 for the full reasoning behind this one deliberate deviation
// from MemoryPanelData.h's own const-vector-reference convention.
FrameGraphRange ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint> points);

// Computes the min/max GPU-millisecond range for one named `pass` across
// every point in `points`, including ONLY entries whose
// gpuPasses[pass].timingStatus == GpuSampleStatus::Present in the scan - an
// Absent/Unsupported entry is excluded regardless of whatever numeric value
// happens to be sitting in its own milliseconds field (see
// PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md's own "never convert absent GPU
// data into 0 ms" rule). hasData is true only if at least one Present entry
// was found. Never reads/branches on countStatus/drawCallCount/
// triangleCount - see ProfilingTypes.h's own comment on the timingStatus/
// countStatus split (PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md, Step
// 2.4).
//
// Bounds-checks `pass` unconditionally (active in both Debug and Release,
// never a debug-only assert), mirroring FrameProfiler::SetGpuPassTiming()'s
// own already-shipped handling of exactly this same kind of invalid input
// (see FrameProfiler.cpp) - an out-of-range `pass` simply reports
// hasData == false, the same as "a valid pass with zero Present entries",
// rather than inventing a separate error channel for a path that should
// never be reachable from real code.
FrameGraphRange ComputeGpuMillisecondsRange(std::span<const FrameGraphPoint> points, GpuPass pass);

} // namespace gte::Profiling
