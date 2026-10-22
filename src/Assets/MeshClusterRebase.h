#pragma once

#include "MeshCluster.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

// A contiguous index range of a mesh and whether its clusters are kept.
struct ClusterSliceRange {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    bool keep = false;
};

// Keeps the clusters inside kept slices and rebases firstIndex to the concatenation of the kept slices.
// Clusters of dropped slices are removed. Fails on unsorted or overlapping slices, a cluster that
// straddles a slice edge, or a table above kMaxMeshClusters. The caller validates the result.
bool RebaseClustersForRanges(std::span<const MeshCluster> clusters, std::span<const ClusterSliceRange> slices,
    std::vector<MeshCluster>& out, std::string* why);

} // namespace gte
