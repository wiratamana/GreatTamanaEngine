#include "ClusterClaim.h"

namespace gte {

std::vector<std::span<const Entity>> ClaimedSpansOf(
    std::span<const ClusterPartClaim> parts, const ClusterClaimContext& context)
{
    std::vector<std::span<const Entity>> claimed;
    if (!ClusterClaimAllowed(context)) {
        return claimed;
    }
    claimed.reserve(parts.size());
    for (const ClusterPartClaim& part : parts) {
        if (ClusterPartClaimable(part) && !part.entities.empty()) {
            claimed.push_back(part.entities);
        }
    }
    return claimed;
}

} // namespace gte
