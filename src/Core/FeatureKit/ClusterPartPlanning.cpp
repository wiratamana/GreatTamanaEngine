#include "ClusterPartPlanning.h"

#include <algorithm>
#include <unordered_map>

namespace gte::fx {

std::vector<ClusterMeshGroup> GroupInstancesByMesh(std::span<const ClusterInstance> candidates)
{
    std::vector<ClusterMeshGroup> groups;
    std::unordered_map<MeshToken, std::size_t, MeshTokenHash> groupOf;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const MeshToken token = candidates[i].mesh;
        auto [it, inserted] = groupOf.try_emplace(token, groups.size());
        if (inserted) {
            groups.push_back(ClusterMeshGroup{ token, {} });
        }
        groups[it->second].instances.push_back(static_cast<std::uint32_t>(i));
    }
    return groups;
}

ClusterSlotAssignment AssignClusterPartSlots(std::span<const ClusterPartSize> parts, std::uint32_t slotCount)
{
    std::vector<std::uint32_t> order(parts.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<std::uint32_t>(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t workA = parts[a].instanceCount * parts[a].clusterCount;
        const std::uint64_t workB = parts[b].instanceCount * parts[b].clusterCount;
        if (workA != workB) {
            return workA > workB;
        }
        return parts[a].mesh < parts[b].mesh;
    });

    ClusterSlotAssignment result;
    for (const std::uint32_t index : order) {
        (result.assigned.size() < slotCount ? result.assigned : result.dropped).push_back(index);
    }
    return result;
}

ClusterVisibleCount SumClusterCounts(std::span<const ClusterCountSample> samples) noexcept
{
    ClusterVisibleCount total;
    for (const ClusterCountSample& sample : samples) {
        if (!sample.valid) {
            continue;
        }
        total.value += sample.value;
        total.frame = total.valid ? std::min(total.frame, sample.frame) : sample.frame;
        total.valid = true;
    }
    return total;
}

} // namespace gte::fx
