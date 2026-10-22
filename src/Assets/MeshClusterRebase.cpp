#include "MeshClusterRebase.h"

#include <cstddef>
#include <limits>

namespace gte {

namespace {

bool Fail(std::string* why, const char* text)
{
    if (why != nullptr) {
        *why = text;
    }
    return false;
}

std::uint64_t SliceEnd(const ClusterSliceRange& slice)
{
    return static_cast<std::uint64_t>(slice.firstIndex) + slice.indexCount;
}

} // namespace

bool RebaseClustersForRanges(std::span<const MeshCluster> clusters, std::span<const ClusterSliceRange> slices,
    std::vector<MeshCluster>& out, std::string* why)
{
    out.clear();
    if (clusters.size() > kMaxMeshClusters) {
        return Fail(why, "too many clusters");
    }

    std::uint64_t previousEnd = 0;
    for (const ClusterSliceRange& slice : slices) {
        if (slice.firstIndex < previousEnd) {
            return Fail(why, "slices are unsorted or overlapping");
        }
        previousEnd = SliceEnd(slice);
    }

    out.reserve(clusters.size());
    std::size_t sliceIndex = 0;
    std::uint64_t keptOffset = 0; // Start of slices[sliceIndex] inside the concatenation of kept slices.
    std::uint64_t previousFirst = 0;
    for (const MeshCluster& cluster : clusters) {
        const std::uint64_t first = cluster.firstIndex;
        const std::uint64_t end = first + cluster.indexCount;
        if (first < previousFirst) {
            out.clear();
            return Fail(why, "clusters are unsorted");
        }
        previousFirst = first;

        while (sliceIndex < slices.size() && SliceEnd(slices[sliceIndex]) <= first) {
            if (slices[sliceIndex].keep) {
                keptOffset += slices[sliceIndex].indexCount;
            }
            ++sliceIndex;
        }
        if (sliceIndex == slices.size() || first < slices[sliceIndex].firstIndex) {
            out.clear();
            return Fail(why, "cluster lies outside every slice");
        }
        const ClusterSliceRange& slice = slices[sliceIndex];
        if (end > SliceEnd(slice)) {
            out.clear();
            return Fail(why, "cluster straddles a slice boundary");
        }
        if (!slice.keep) {
            continue;
        }

        const std::uint64_t rebased = keptOffset + (first - slice.firstIndex);
        if (rebased > std::numeric_limits<std::uint32_t>::max()) {
            out.clear();
            return Fail(why, "rebased index does not fit 32 bits");
        }
        MeshCluster copy = cluster;
        copy.firstIndex = static_cast<std::uint32_t>(rebased);
        out.push_back(copy);
    }
    return true;
}

} // namespace gte
