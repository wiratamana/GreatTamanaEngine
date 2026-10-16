#pragma once

// Pure, ImGui-free formatting helpers shared by the Render Graph panel and the JSON metadata.
// Kept out of RenderGraphSnapshot so the snapshot holds no opinion on how a string should look.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../GpuTiming.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte::rg {

// Present -> "%.2f ms", Unsupported -> "Unsupported", Absent -> "N/A" (never a fabricated "0.00 ms").
std::string FormatGpuTiming(const GpuTimingSample& timing);

// Name of the pass at `index`; out of range -> "?", empty name -> "(unnamed)".
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index);

// Plain switches without a default, so a new enumerator fails to compile until handled.
const char* ToString(ResourceKind kind) noexcept;

// "Shared" / "GameView" / "SceneView".
const char* ToString(ViewScope scope) noexcept;

} // namespace gte::rg
