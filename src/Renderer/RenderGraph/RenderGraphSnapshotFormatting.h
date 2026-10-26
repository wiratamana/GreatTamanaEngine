#pragma once

// Pure, ImGui-free ToString() helpers shared by the metadata builder and the panels.
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

// Name of the pass at `index`; out of range -> "?", empty name -> "(unnamed)".
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index);

// Plain switches without a default, so a new enumerator fails to compile until handled.
const char* ToString(ResourceKind kind) noexcept;

// "Shared" / "Primary" / "Secondary".
const char* ToString(ViewScope scope) noexcept;

} // namespace gte::rg
