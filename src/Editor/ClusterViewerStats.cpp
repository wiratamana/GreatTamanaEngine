#include "ClusterViewerStats.h"

namespace gte {

ClusterViewerStats BuildClusterViewerStats(const ClusterViewCounts& counts)
{
    ClusterViewerStats stats;

    if (counts.passedFrustum > counts.total) {
        stats.error = "passedFrustum " + std::to_string(counts.passedFrustum) + " is above total "
            + std::to_string(counts.total);
        return stats;
    }
    if (counts.passedCone > counts.passedFrustum) {
        stats.error = "passedCone " + std::to_string(counts.passedCone) + " is above passedFrustum "
            + std::to_string(counts.passedFrustum);
        return stats;
    }
    if (counts.visible > counts.passedCone) {
        stats.error = "visible " + std::to_string(counts.visible) + " is above passedCone "
            + std::to_string(counts.passedCone);
        return stats;
    }

    stats.valid = true;
    stats.total = counts.total;
    stats.visible = counts.visible;
    stats.byFrustum = counts.total - counts.passedFrustum;
    stats.byCone = counts.passedFrustum - counts.passedCone;
    stats.byOcclusion = counts.passedCone - counts.visible;
    stats.visibleTriangles = counts.visibleTriangles;
    stats.visiblePercent = counts.total == 0
        ? 0.0
        : static_cast<double>(counts.visible) * 100.0 / static_cast<double>(counts.total);
    return stats;
}

} // namespace gte
