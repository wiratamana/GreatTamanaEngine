#pragma once

#include "MaterialData.h"
#include "MeshData.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

struct MeshClusterReport {
    bool success = false;
    std::string message;
    ClusterWeldMode weld = ClusterWeldMode::Exact;
    std::size_t vertexCountBefore = 0;
    std::size_t vertexCountAfter = 0;
    std::size_t triangleCount = 0;
    std::size_t clusterCount = 0;
    float avgTrianglesPerCluster = 0.0f;
    float avgVerticesPerCluster = 0.0f;
    bool poorFill = false; // Average triangles per cluster below half of the maximum.
    double milliseconds = 0.0;
    const char* lastStep = "none"; // Literal naming the last step that ran.
};

// Range check of the options (limits above). `why` gets the first problem.
bool ValidateMeshClusterOptions(const MeshClusterOptions& options, std::string* why);

// True when both counts fit the 32-bit index space of the clustering library.
bool ClusterBuildCountsFit(std::size_t vertexCount, std::size_t indexCount) noexcept;

// Welds, reorders and splits `mesh` into clusters per material slice. Fills mesh.clusters.
// Cone cutoff = sine of the half angle: normals n of a cone satisfy dot(n, axis) >= sqrt(1 - cutoff^2).
// Strong guarantee: `mesh` is unchanged on failure. Never throws.

// With `earlyOutOnPoorFill` the run stops right after the weld when the expected fill is poor
// (success false, poorFill true, lastStep "weld"). `cancel` is checked between steps.
MeshClusterReport BuildMeshClusters(MeshData& mesh, const std::vector<Material>& materials,
    const MeshClusterOptions& options, const std::atomic<bool>* cancel = nullptr,
    bool earlyOutOnPoorFill = false);

} // namespace gte
