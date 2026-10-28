#pragma once

#include "../GpuDrivenSceneTypes.h"

#include <string>
#include <string_view>

namespace gte {

// Panel text of the culling feature. Pure functions of settings and snapshots; no UI calls.

// One plain sentence describing what `mode` does.
std::string_view CullModeLine(CullMode mode) noexcept;

// "GPU path: N entities, M clusters tested, V visible", or why V is missing.
std::string GpuPathLine(const GpuDrivenSceneViewSnapshot& v);

// "CPU path: T tested, C culled, U kept without bounds (last frame)".
std::string CpuPathLine(const GpuDrivenSceneViewSnapshot& v);

// Hi-Z state in plain words. `secondary` selects the wording of a missing shared pyramid.
std::string HizLine(const GpuDrivenSceneViewSnapshot& v, bool secondary);

// Whose camera the view culls with.
std::string CullCameraLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v);

// Why the CPU draw cull is not running; empty while it is active.
std::string CpuCullStateLine(const GpuDrivenSceneViewSnapshot& v);

// Shown when Hi-Z is asked for but only CPU-drawn meshes exist; empty otherwise.
std::string_view NoGpuPathLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v) noexcept;

// Warning shown while back-face cone culling is on.
std::string_view ConeWarningLine() noexcept;

} // namespace gte
