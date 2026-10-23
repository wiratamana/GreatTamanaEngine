#include "MeshClusterValidation.h"

#include <cmath>
#include <cstdint>

namespace gte {

namespace {

bool Fail(std::string* why, const char* text)
{
    if (why != nullptr) {
        *why = text;
    }
    return false;
}

template <std::size_t N>
bool AllFinite(const float (&values)[N])
{
    for (const float v : values) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ValidateClusterTable(std::span<const MeshCluster> clusters, std::size_t indexCount, std::string* why)
{
    if (clusters.empty()) {
        return indexCount == 0 ? true : Fail(why, "empty cluster table");
    }
    if (clusters.size() > kMaxMeshClusters) {
        return Fail(why, "too many clusters");
    }
    if (clusters.size() > indexCount / 3) {
        return Fail(why, "more clusters than triangles");
    }

    std::uint64_t expectedFirst = 0;
    for (const MeshCluster& c : clusters) {
        if (c.indexCount == 0 || c.indexCount % 3 != 0) {
            return Fail(why, "cluster index count is not a positive multiple of 3");
        }
        if (static_cast<std::uint64_t>(c.firstIndex) != expectedFirst) {
            return Fail(why, "cluster ranges are unsorted, overlapping or have a gap");
        }
        expectedFirst += c.indexCount;

        if (!AllFinite(c.sphere) || !AllFinite(c.cone) || !AllFinite(c.aabbMin) || !AllFinite(c.aabbMax)) {
            return Fail(why, "cluster has a non-finite value");
        }
        if (c.sphere[3] < 0.0f) {
            return Fail(why, "cluster radius is negative");
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (c.aabbMin[axis] > c.aabbMax[axis]) {
                return Fail(why, "cluster bounds are inverted");
            }
        }
        const float cutoff = c.cone[3];
        if (cutoff < 0.0f || cutoff > 1.0f) {
            return Fail(why, "cluster cone cutoff is outside [0, 1]");
        }
        if (cutoff < 1.0f) {
            const double length = std::sqrt(static_cast<double>(c.cone[0]) * c.cone[0]
                + static_cast<double>(c.cone[1]) * c.cone[1] + static_cast<double>(c.cone[2]) * c.cone[2]);
            if (std::fabs(length - 1.0) > 1e-3) {
                return Fail(why, "cluster cone axis is not a unit vector");
            }
        }
    }

    if (expectedFirst != indexCount) {
        return Fail(why, "clusters do not cover the index buffer");
    }
    return true;
}

bool ValidateMeshIndices(std::size_t vertexCount, std::span<const std::uint32_t> indices, std::string* why)
{
    if (indices.empty()) {
        return Fail(why, "no indices");
    }
    if (indices.size() % 3 != 0) {
        return Fail(why, "index count is not a multiple of 3");
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertexCount) {
            return Fail(why, "an index points outside the vertex array");
        }
    }
    return true;
}

bool ValidateMeshClusters(const MeshData& mesh, std::string* why)
{
    if (mesh.clusters.empty()) {
        return true;
    }
    return ValidateClusterTable(mesh.clusters, mesh.indices.size(), why);
}

} // namespace gte
