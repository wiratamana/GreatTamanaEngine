#include "ClusterPartPlanning.h"

#include <algorithm>
#include <cmath>
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

ClusterPartBox UnionInstanceBoxes(std::span<const AABB> instanceBoxes, std::span<const std::uint32_t> indices) noexcept
{
    ClusterPartBox part;
    bool first = true;
    for (const std::uint32_t index : indices) {
        if (index >= instanceBoxes.size()) {
            return ClusterPartBox{};
        }
        const AABB& box = instanceBoxes[index];
        const bool usable = std::isfinite(box.min.x) && std::isfinite(box.min.y) && std::isfinite(box.min.z)
            && std::isfinite(box.max.x) && std::isfinite(box.max.y) && std::isfinite(box.max.z)
            && box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z;
        if (!usable) {
            return ClusterPartBox{};
        }
        if (first) {
            part.box = box;
            first = false;
            continue;
        }
        part.box.min = Vec3(std::min(part.box.min.x, box.min.x), std::min(part.box.min.y, box.min.y),
            std::min(part.box.min.z, box.min.z));
        part.box.max = Vec3(std::max(part.box.max.x, box.max.x), std::max(part.box.max.y, box.max.y),
            std::max(part.box.max.z, box.max.z));
    }
    part.bounded = !first;
    return part;
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
    ClusterCullFlags flags, std::span<const AABB> instanceBoxes, std::span<const ClusterMeshGroup> groups)
{
    std::vector<std::uint8_t> skips(groups.size(), 0);
    if (!planes.has_value() || !HasClusterCullFlag(flags, ClusterCullFlags::Frustum)) {
        return skips;
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        skips[i] = ShouldSkipClusterPart(planes, flags, UnionInstanceBoxes(instanceBoxes, groups[i].instances)) ? 1 : 0;
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
