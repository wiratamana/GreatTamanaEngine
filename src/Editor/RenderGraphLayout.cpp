#include "RenderGraphLayout.h"

#include <algorithm>
#include <unordered_map>

namespace gte {

GraphLayout ComputeGraphLayout(const rg::RenderGraphRegimeMetadata& regime)
{
    GraphLayout layout;
    layout.nodes.reserve(regime.passes.size());

    // Distinct ordinals, sorted ascending - real pipeline-stage order,
    // independent of which pass (survivor or culled) happens to carry each
    // ordinal first in `regime.passes` this particular frame.
    std::vector<std::uint32_t> distinctOrdinals;
    distinctOrdinals.reserve(regime.passes.size());
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        distinctOrdinals.push_back(pass.renderPassEventOrder);
    }
    std::sort(distinctOrdinals.begin(), distinctOrdinals.end());
    distinctOrdinals.erase(std::unique(distinctOrdinals.begin(), distinctOrdinals.end()), distinctOrdinals.end());

    std::unordered_map<std::uint32_t, std::uint32_t> columnByOrdinal;
    columnByOrdinal.reserve(distinctOrdinals.size());
    for (std::size_t i = 0; i < distinctOrdinals.size(); ++i) {
        columnByOrdinal[distinctOrdinals[i]] = static_cast<std::uint32_t>(i);
    }

    std::unordered_map<std::uint32_t, std::uint32_t> rowCursorByColumn;
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        const std::uint32_t column = columnByOrdinal[pass.renderPassEventOrder];
        const std::uint32_t row = rowCursorByColumn[column]++;
        layout.nodes.push_back(GraphNodeLayout{ pass.name, column, row });
    }

    // One edge per resource whose first/last use span two distinct passes.
    for (const rg::RenderGraphResourceMetadata& resource : regime.resources) {
        if (!resource.firstUsePassName.has_value() || !resource.lastUsePassName.has_value()) {
            continue;
        }
        if (*resource.firstUsePassName == *resource.lastUsePassName) {
            continue; // Read and written by the same pass - nothing to draw.
        }
        layout.edges.push_back(GraphEdgeLayout{ *resource.firstUsePassName, *resource.lastUsePassName, resource.name });
    }

    return layout;
}

} // namespace gte
