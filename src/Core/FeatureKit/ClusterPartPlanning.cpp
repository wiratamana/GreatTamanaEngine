#include "ClusterPartPlanning.h"

#include <algorithm>
#include <limits>
#include <map>

namespace gte::fx {

std::vector<ClusterMeshGroup> GroupInstances(std::span<const ClusterInstance> candidates, bool splitByMaterial)
{
    std::vector<ClusterMeshGroup> groups;
    std::map<std::pair<MeshToken, MaterialToken>, std::size_t> groupOf;
    // Runs of one mesh and material are common; the last group skips the map lookup.
    std::size_t lastGroup = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const ClusterInstance& candidate = candidates[i];
        const MaterialToken material = splitByMaterial ? candidate.material : MaterialToken{};
        if (i == 0 || candidate.mesh != groups[lastGroup].mesh || material != groups[lastGroup].material) {
            auto [it, inserted] = groupOf.try_emplace(std::pair{ candidate.mesh, material }, groups.size());
            if (inserted) {
                groups.push_back(ClusterMeshGroup{ candidate.mesh, material, candidate.kind, {} });
            }
            lastGroup = it->second;
        }
        groups[lastGroup].instances.push_back(static_cast<std::uint32_t>(i));
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

ClusterPartBox UnionInstanceBoxes(std::span<const AABB> instanceBoxes, std::span<const std::uint32_t> indices) noexcept
{
    if (indices.empty()) {
        return ClusterPartBox{};
    }
    constexpr float kHuge = std::numeric_limits<float>::max();
    Vec3 lo(kHuge, kHuge, kHuge);
    Vec3 hi(-kHuge, -kHuge, -kHuge);
    for (const std::uint32_t index : indices) {
        if (index >= instanceBoxes.size()) {
            return ClusterPartBox{};
        }
        const AABB& box = instanceBoxes[index];
        if (!IsUsableInstanceBox(box)) {
            return ClusterPartBox{};
        }
        lo = Vec3(std::min(lo.x, box.min.x), std::min(lo.y, box.min.y), std::min(lo.z, box.min.z));
        hi = Vec3(std::max(hi.x, box.max.x), std::max(hi.y, box.max.y), std::max(hi.z, box.max.z));
    }
    return ClusterPartBox{ AABB{ lo, hi }, true };
}

bool ShouldSkipClusterPart(const std::optional<std::array<Plane, 6>>& planes, ClusterCullFlags flags,
    const ClusterPartBox& part) noexcept
{
    if (!planes.has_value() || !part.bounded || !HasClusterCullFlag(flags, ClusterCullFlags::Frustum)) {
        return false;
    }
    return IsAABBOutsideFrustum(*planes, PadAABB(part.box, kClusterPartSkipPadding));
}

std::vector<std::uint8_t> PlanClusterPartSkips(const std::optional<std::array<Plane, 6>>& planes,
    ClusterCullFlags flags, std::span<const AABB> instanceBoxes, const ClusterPartBox& allBoxes,
    std::span<const ClusterMeshGroup> groups)
{
    std::vector<std::uint8_t> skips(groups.size(), 0);
    if (!planes.has_value() || !HasClusterCullFlag(flags, ClusterCullFlags::Frustum)) {
        return skips;
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const bool coversAll = !instanceBoxes.empty() && groups[i].instances.size() == instanceBoxes.size();
        const ClusterPartBox part = coversAll ? allBoxes : UnionInstanceBoxes(instanceBoxes, groups[i].instances);
        skips[i] = ShouldSkipClusterPart(planes, flags, part) ? 1 : 0;
    }
    return skips;
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

ClusterReasonCounts SumClusterReasons(std::span<const ClusterReasonSample> samples) noexcept
{
    ClusterReasonCounts total;
    for (const ClusterReasonSample& sample : samples) {
        if (!sample.valid) {
            continue;
        }
        total.frustum += sample.words.reasons[kClusterReasonFrustum];
        total.cone += sample.words.reasons[kClusterReasonCone];
        total.pyramid += sample.words.reasons[kClusterReasonPyramid];
        total.history += sample.words.reasons[kClusterReasonHistory];
        total.kept += sample.words.reasons[kClusterReasonKept];
        total.frame = total.valid ? std::min(total.frame, sample.frame) : sample.frame;
        total.valid = true;
    }
    return total;
}

} // namespace gte::fx
