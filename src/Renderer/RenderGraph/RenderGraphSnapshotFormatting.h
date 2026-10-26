#pragma once

// Pure, ImGui-free formatting helpers shared by the Render Graph panel and the JSON metadata.
// Kept out of RenderGraphSnapshot so the snapshot holds no opinion on how a string should look.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../GpuStampPlan.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte::rg {

// Status word of a GPU read: "Present", "Partial", "Absent", "Error", "Unsupported" or "Inactive".
const char* ToString(GpuReadState state) noexcept;

// Present -> "%.2f ms" (barriers + work), Partial -> "~%.2f ms (partial)", any other state -> its status word.
// Never a fabricated "0.00 ms" for a pass that was not measured.
std::string FormatGpuTiming(const GpuPassRecord& record);

// Name of the pass at `index`; out of range -> "?", empty name -> "(unnamed)".
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index);

// Plain switches without a default, so a new enumerator fails to compile until handled.
const char* ToString(ResourceKind kind) noexcept;

// "Shared" / "GameView" / "SceneView".
const char* ToString(ViewScope scope) noexcept;

} // namespace gte::rg
