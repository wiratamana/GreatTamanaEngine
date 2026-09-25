#pragma once

// editor-core-separation-7 campaign, PHASE1
// (PHASE1_EXTRACT_FORMATTING_AND_LABEL_HELPERS.md) - pure, ImGui-free,
// Tier-1-testable presentation-formatting helpers shared by BOTH
// RenderGraphPanel.cpp (ImGui) and RenderGraphMetadata.cpp (JSON, PHASE2) -
// see PHASE0_MASTER_STRATEGY.md's Locked Design Decision #12 for why this is
// its OWN new file rather than folded into RenderGraphSnapshot.h/.cpp
// (RenderGraphSnapshot itself stays free of any "how a string should look"
// opinion, exactly as it always has been).
//
// Every function here is a pure function of already-computed plain data -
// no live RenderGraph/VkDevice/Renderer/ImGui - mirrors
// RenderGraphSnapshot.h's own "directly Tier-1-testable" precedent exactly.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../GpuTiming.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte::rg {

// Relocated verbatim from RenderGraphPanel.cpp's own anonymous namespace -
// Present -> "%.2f ms", Unsupported -> "Unsupported", Absent/default ->
// "N/A" (NEVER a fabricated "0.00 ms" - see AGENTS.md, "Profiling").
std::string FormatGpuTiming(const GpuTimingSample& timing);

// Relocated verbatim - empty vector -> "-", each empty-string name ->
// "(unnamed)", joined with ", ".
std::string JoinNames(const std::vector<std::string>& names);

// Relocated and RENAMED from PassNameAtSurvivingIndex() (a clearer name now
// that this lives outside the one panel that used to be its only caller) -
// identical behavior: out-of-range index -> "?", empty name -> "(unnamed)".
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index);

// NEW (this phase) - mirrors PassKind/RenderPassCategory/RenderPassDrawKind/
// RenderPassEvent's own existing ToString() free-function precedent
// (RenderGraphTypes.h/.cpp) exactly: a plain switch, NO default: case, so a
// future new ResourceKind enumerator fails to compile here until updated.
const char* ToString(ResourceKind kind) noexcept;

// NEW (this phase) - same "no default: case" discipline, for ViewScope
// ("Shared" / "GameView" / "SceneView").
const char* ToString(ViewScope scope) noexcept;

} // namespace gte::rg
