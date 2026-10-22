#pragma once

#include "../../Assets/MeshCluster.h"
#include "../../Assets/MeshClusterRebase.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

// Cluster table for the part that concatenates the kept slices. Rebases, caps and validates it.
// Nullopt (with `why`) when nothing is kept, the table is above `maxClusters`, or it does not tile the part.
std::optional<std::vector<MeshCluster>> SelectClusterTableForKeptSlices(std::span<const MeshCluster> clusters,
    std::span<const ClusterSliceRange> slices, std::size_t keptIndexCount, std::uint32_t maxClusters,
    std::string* why);

} // namespace gte
