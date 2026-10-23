#pragma once

// Options of the mesh cluster build. Header only, so request structs can take their defaults from it.

#include <cstdint>

namespace gte {

enum class ClusterWeldMode : std::uint8_t {
    Exact,        // Merge vertices with identical position, normal, uv and tangent. Lossless.
    PositionOnly, // Merge by position, then recompute smooth normals. Refused for uv-mapped meshes.
};

struct MeshClusterOptions {
    ClusterWeldMode weld = ClusterWeldMode::Exact;
    std::uint32_t maxVerticesPerCluster = 64;   // 3..255
    std::uint32_t maxTrianglesPerCluster = 124; // 4..512, multiple of 4
    float coneWeight = 0.5f;                    // 0..1, finite
};

} // namespace gte
