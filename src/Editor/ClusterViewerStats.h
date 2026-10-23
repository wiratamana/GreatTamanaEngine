#pragma once

// Per-reason numbers of one GPU cull report, with the nesting check. Pure: no ImGui, no GPU.

#include "../Renderer/ClusterViewRenderer.h"

#include <cstdint>
#include <string>

namespace gte {

struct ClusterViewerStats {
    bool valid = false; // false when the counts are not nested
    std::string error;  // first broken link, empty when valid
    std::uint32_t total = 0;
    std::uint32_t visible = 0;
    std::uint32_t byFrustum = 0;
    std::uint32_t byCone = 0;
    std::uint32_t byOcclusion = 0;
    std::uint64_t visibleTriangles = 0;
    double visiblePercent = 0.0; // visible / total * 100, 0 when total is 0
};

// Needs visible <= passedCone <= passedFrustum <= total. The check runs before any subtraction.
// A violation gives valid = false, the error text and no per-reason numbers.
ClusterViewerStats BuildClusterViewerStats(const ClusterViewCounts& counts);

} // namespace gte
