#pragma once

// Pure node/edge grid layout for the Render Graph panel's canvas. No ImGui,
// no Vulkan, no live RenderGraph& - a function of already-computed
// RenderGraphRegimeMetadata alone, so it is directly unit-testable with a
// hand-fabricated input.

#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// One pass node's grid position in the canvas.
struct GraphNodeLayout {
    std::string passName;
    std::uint32_t column = 0;
    std::uint32_t row = 0;
};

// One writer-pass -> reader-pass wire through a named resource.
struct GraphEdgeLayout {
    std::string fromPass;
    std::string toPass;
    std::string resourceName;
};

struct GraphLayout {
    std::vector<GraphNodeLayout> nodes;
    std::vector<GraphEdgeLayout> edges;
};

// Column is assigned by the pass's REAL pipeline-stage ordinal
// (RenderGraphPassMetadata::renderPassEventOrder), never by first-seen
// position in `regime.passes` - that vector lists every surviving pass
// first, then every culled pass appended after regardless of its own real
// tier (see RenderGraphSnapshot.h), so a first-seen-order layout would
// visibly reshuffle columns the instant a mid-pipeline pass gets disabled.
// Row is a simple per-column append counter, in `regime.passes` order.
GraphLayout ComputeGraphLayout(const rg::RenderGraphRegimeMetadata& regime);

} // namespace gte
