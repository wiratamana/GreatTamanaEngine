#pragma once

// Display text for the Render Graph panels: GPU timing text and the per-name pass grouping.

#include "../Renderer/GpuStampPlan.h"
#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// GPU timing text of one pass record: measured value, or its status word.
std::string FormatGpuTiming(const GpuPassRecord& record);

// Same text from already-built pass metadata.
std::string FormatGpuTiming(const rg::RenderGraphPassMetadata& pass);

// All passes sharing one `name` (e.g. a per-view pass declared once per view), shown as one row.
struct RenderGraphGroupedPass {
    std::string name;
    // Views with a non-culled instance ("Game+Scene", "Game only", "Shared"); every instance's view when all were culled.
    std::string viewLabel;
    bool isCulled = false; // True only if every instance was culled.
    std::uint32_t drawCallCount = 0; // Summed over the instances.
    std::uint32_t triangleCount = 0;
    std::string gpuTimingText; // Per-view text, e.g. "Game: 0.12 ms, Scene: 0.08 ms"; one instance shows its own.
    std::vector<std::string> reads;  // Resource names over all instances, first-seen order, no duplicates.
    std::vector<std::string> writes;
    std::vector<rg::RenderGraphPassMetadata> instances; // Original order.
};

// Groups `ungrouped` by name in sorted order, so row order never depends on input order.
std::vector<RenderGraphGroupedPass> GroupPassMetadataByName(const std::vector<rg::RenderGraphPassMetadata>& ungrouped);

} // namespace gte
