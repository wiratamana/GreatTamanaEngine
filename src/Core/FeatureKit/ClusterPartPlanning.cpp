#include "ClusterPartPlanning.h"

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>

namespace gte::fx {

std::vector<ClusterMeshGroup> GroupInstances(std::span<const ClusterInstance> candidates, bool splitByMaterial)
{
    std::vector<ClusterMeshGroup> groups;
    if (candidates.empty()) {
        return groups;
    }
    // Common case: every candidate shares one mesh and material. One group, one exact allocation.
    const ClusterInstance& first = candidates.front();
    const MaterialToken firstMaterial = splitByMaterial ? first.material : MaterialToken{};
    const bool single = std::all_of(candidates.begin(), candidates.end(), [&](const ClusterInstance& candidate) {
        return candidate.mesh == first.mesh && (splitByMaterial ? candidate.material : MaterialToken{}) == firstMaterial;
    });
    if (single) {
        ClusterMeshGroup& group = groups.emplace_back(ClusterMeshGroup{ first.mesh, firstMaterial, first.kind, {} });
        group.instances.resize(candidates.size());
        std::iota(group.instances.begin(), group.instances.end(), std::uint32_t{ 0 });
        return groups;
    }

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

std::vector<std::uint32_t> ClusterSlotsOf(const ClusterSlotAssignment& assignment, std::size_t partCount)
{
    std::vector<std::uint32_t> slots(partCount, kNoClusterSlot);
    for (std::size_t slot = 0; slot < assignment.assigned.size(); ++slot) {
        if (assignment.assigned[slot] < partCount) {
            slots[assignment.assigned[slot]] = static_cast<std::uint32_t>(slot);
        }
    }
    return slots;
}

ClusterSlotAssignment ClusterAssignmentFromSlots(std::span<const std::uint32_t> slots)
{
    ClusterSlotAssignment result;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bySlot; // (slot, part index)
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i] == kNoClusterSlot) {
            result.dropped.push_back(static_cast<std::uint32_t>(i));
        } else {
            bySlot.emplace_back(slots[i], static_cast<std::uint32_t>(i));
        }
    }
    std::sort(bySlot.begin(), bySlot.end());
    for (const auto& entry : bySlot) {
        result.assigned.push_back(entry.second);
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

namespace {

// True when the usable box lies fully behind `plane`.
bool PlaneRejectsBox(const Plane& plane, const AABB& box) noexcept
{
    const float x = plane.normal.x >= 0.0f ? box.max.x : box.min.x;
    const float y = plane.normal.y >= 0.0f ? box.max.y : box.min.y;
    const float z = plane.normal.z >= 0.0f ? box.max.z : box.min.z;
    return plane.normal.x * x + plane.normal.y * y + plane.normal.z * z + plane.distance < 0.0f;
}

} // namespace

std::array<Plane, 6> SelectCullPlanes(std::span<const Plane> pool, std::span<const AABB> sampleBoxes)
{
    std::array<Plane, 6> camera{};
    for (std::size_t i = 0; i < camera.size() && i < pool.size(); ++i) {
        camera[i] = pool[i];
    }
    if (pool.size() <= camera.size()) {
        return camera;
    }

    std::vector<AABB> samples;
    samples.reserve(sampleBoxes.size());
    for (const AABB& box : sampleBoxes) {
        if (IsUsableInstanceBox(box)) {
            samples.push_back(box);
        }
    }
    if (samples.empty()) {
        return camera;
    }

    std::vector<std::uint8_t> rejected(samples.size(), 0);
    std::vector<std::uint8_t> used(pool.size(), 0);
    std::array<Plane, 6> chosen{};
    std::size_t rejectedCount = 0;
    for (std::size_t pick = 0; pick < chosen.size(); ++pick) {
        std::size_t best = 0;
        std::size_t bestGain = 0;
        bool found = false;
        for (std::size_t p = 0; p < pool.size(); ++p) {
            if (used[p] != 0) {
                continue;
            }
            std::size_t gain = 0;
            for (std::size_t i = 0; i < samples.size(); ++i) {
                gain += (rejected[i] == 0 && PlaneRejectsBox(pool[p], samples[i])) ? 1 : 0;
            }
            if (!found || gain > bestGain) {
                best = p;
                bestGain = gain;
                found = true;
            }
        }
        used[best] = 1;
        chosen[pick] = pool[best];
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (rejected[i] == 0 && PlaneRejectsBox(pool[best], samples[i])) {
                rejected[i] = 1;
                ++rejectedCount;
            }
        }
    }

    std::size_t cameraCount = 0;
    for (const AABB& box : samples) {
        bool out = false;
        for (const Plane& plane : camera) {
            out = out || PlaneRejectsBox(plane, box);
        }
        cameraCount += out ? 1 : 0;
    }
    return rejectedCount > cameraCount ? chosen : camera;
}

std::array<Plane, 6> ChooseCullPlanes(
    const std::array<Plane, 6>& camera, const ExtraCullPlanes& extra, std::span<const AABB> candidateBounds)
{
    std::vector<Plane> pool(camera.begin(), camera.end());
    const std::size_t extraCount = std::min<std::size_t>(extra.count, extra.planes.size());
    pool.insert(pool.end(), extra.planes.begin(), extra.planes.begin() + extraCount);

    const std::size_t stride = std::max<std::size_t>(1, candidateBounds.size() / kCullPlaneSampleCount);
    std::vector<AABB> samples;
    samples.reserve(kCullPlaneSampleCount + 1);
    for (std::size_t i = 0; i < candidateBounds.size() && samples.size() < kCullPlaneSampleCount; i += stride) {
        samples.push_back(candidateBounds[i]);
    }
    return SelectCullPlanes(pool, samples);
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
