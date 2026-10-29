#pragma once

#include "../GpuDrivenSceneTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gte {

// Panel text of the culling feature. Pure functions of settings and snapshots; no UI calls.

// One plain sentence describing what `mode` does.
std::string_view CullModeLine(GpuCullMode mode) noexcept;

// "GPU path: N entities, M clusters tested, V visible", or why V is missing.
std::string GpuPathLine(const GpuDrivenSceneViewSnapshot& v);

// "Parts skipped (outside view): N"; empty when no part was skipped.
std::string SkippedPartsLine(const GpuDrivenSceneViewSnapshot& v);

// "CPU path: T tested, C culled, U kept without bounds (last frame)".
std::string CpuPathLine(const GpuDrivenSceneViewSnapshot& v);

// "On the CPU path (no Hi-Z): N <reason>, ..."; empty when every renderable is on the GPU path.
std::string CpuReasonsLine(const GpuDrivenSceneViewSnapshot& v);

// Hi-Z state in plain words. `secondary` selects the wording of a missing shared pyramid.
std::string HizLine(const GpuDrivenSceneViewSnapshot& v, bool secondary);

// Whose camera the view culls with.
std::string CullCameraLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v);

// Why the CPU draw cull is not running; empty while it is active.
std::string CpuCullStateLine(const GpuDrivenSceneViewSnapshot& v);

// Shown when Hi-Z is asked for but only CPU-drawn meshes exist; empty otherwise.
std::string_view NoGpuPathLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v) noexcept;

// How to compare culling modes: timing and pacing hints.
std::string_view CullHelpLine() noexcept;

// Items by outcome from the cull counters, or a hint while the counters are off or not ready.
std::string ReasonsLine(const GpuDrivenSceneViewSnapshot& v);

// Why the cluster path drew nothing although the view rendered; empty when it is active or a refusal text explains it.
std::string InertLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v);

// GPU time of one pass, grouped for the panel. `view` is Primary, Secondary or Other (shared passes).
enum class CullTimeView : std::uint8_t { Primary, Secondary, Other };

struct CullTimeRow {
    CullTimeView view = CullTimeView::Other;
    std::string_view owner;       // Owning feature of the pass.
    bool culled = false;          // A culled pass never executed and is ignored.
    std::optional<double> ms;     // GPU milliseconds; empty when the pass was not measured.
};

// Milliseconds of one view. A number is empty unless every executed pass of the view was measured (never 0).
struct CullTimeSplit {
    std::optional<double> total;
    std::optional<double> cull;  // Passes owned by the culling feature.
    std::optional<double> other; // total - cull.
    std::uint32_t passes = 0;
    std::uint32_t measured = 0;
};

struct CullTimeSplits {
    CullTimeSplit primary;
    CullTimeSplit secondary;
};

CullTimeSplits SplitCullTime(std::span<const CullTimeRow> rows, std::string_view cullFeature);

// "GPU time: total T ms, culling C ms, everything else O ms"; "-" for a number that was not measured.
std::string CullTimeLine(const CullTimeSplit& split);

// Warning shown while back-face cone culling is on.
std::string_view ConeWarningLine() noexcept;

} // namespace gte
