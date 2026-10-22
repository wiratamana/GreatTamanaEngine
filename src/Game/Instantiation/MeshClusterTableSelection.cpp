#include "MeshClusterTableSelection.h"

#include "../../Assets/MeshClusterValidation.h"

namespace gte {

namespace {

std::nullopt_t Refuse(std::string* why, const char* text)
{
    if (why != nullptr) {
        *why = text;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::vector<MeshCluster>> SelectClusterTableForKeptSlices(std::span<const MeshCluster> clusters,
    std::span<const ClusterSliceRange> slices, std::size_t keptIndexCount, std::uint32_t maxClusters,
    std::string* why)
{
    if (clusters.empty()) {
        return Refuse(why, "no clusters");
    }
    if (keptIndexCount == 0) {
        return Refuse(why, "no kept index range");
    }

    std::vector<MeshCluster> rebased;
    if (!RebaseClustersForRanges(clusters, slices, rebased, why)) {
        return std::nullopt;
    }
    if (rebased.empty()) {
        return Refuse(why, "no cluster lies in a kept slice");
    }
    if (rebased.size() > maxClusters) {
        return Refuse(why, "cluster table above the binding limit");
    }
    if (!ValidateClusterTable(rebased, keptIndexCount, why)) {
        return std::nullopt;
    }
    return rebased;
}

} // namespace gte
