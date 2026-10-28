#include "ClusterPartPlanning.h"

#include <algorithm>
#include <map>

namespace gte::fx {

std::vector<ClusterMeshGroup> GroupInstances(std::span<const ClusterInstance> candidates, bool splitByMaterial)
{
    std::vector<ClusterMeshGroup> groups;
    std::map<std::pair<MeshToken, MaterialToken>, std::size_t> groupOf;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const ClusterInstance& candidate = candidates[i];
        const MaterialToken material = splitByMaterial ? candidate.material : MaterialToken{};
        auto [it, inserted] = groupOf.try_emplace(std::pair{ candidate.mesh, material }, groups.size());
        if (inserted) {
            groups.push_back(ClusterMeshGroup{ candidate.mesh, material, candidate.kind, {} });
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
        if (parts[a].mesh != parts[b].mesh) {
            return parts[a].mesh < parts[b].mesh;
        }
        return parts[a].material < parts[b].material;
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
