#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

namespace gte {

// One cluster of a mesh: a contiguous index range plus culling bounds, in mesh local space.
// The same 64 bytes are the file record and the GPU record.
struct MeshCluster {
    // xyz = center, w = radius (>= 0).
    float sphere[4];

    // xyz = unit axis, w = cutoff in [0, 1]. Cutoff 1 means "no cone".
    float cone[4];

    float aabbMin[3];
    std::uint32_t firstIndex; // Into the mesh index array.
    float aabbMax[3];
    std::uint32_t indexCount; // triangleCount * 3.
};
static_assert(sizeof(MeshCluster) == 64);
static_assert(std::is_trivially_copyable_v<MeshCluster>);
static_assert(std::endian::native == std::endian::little);

// Largest table one 128 MiB storage binding can hold.
inline constexpr std::uint32_t kMaxMeshClusters = (128u * 1024u * 1024u) / 64u;

// Multiplies the cone axis to match the pipeline's triangle winding.
inline constexpr float kClusterConeWindingSign = 1.0f;

} // namespace gte
