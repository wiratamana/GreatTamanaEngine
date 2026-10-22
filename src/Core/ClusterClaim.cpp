#include "ClusterClaim.h"

namespace gte {

std::vector<Entity> ClaimedEntitiesOf(std::span<const ClusterPartClaim> parts, const ClusterClaimContext& context)
{
    std::vector<Entity> claimed;
    if (!ClusterClaimAllowed(context)) {
        return claimed;
    }
    std::size_t total = 0;
    for (const ClusterPartClaim& part : parts) {
        if (ClusterPartClaimable(part)) {
            total += part.entities.size();
        }
    }
    claimed.reserve(total);
    for (const ClusterPartClaim& part : parts) {
        if (ClusterPartClaimable(part)) {
            claimed.insert(claimed.end(), part.entities.begin(), part.entities.end());
        }
    }
    return claimed;
}

} // namespace gte
