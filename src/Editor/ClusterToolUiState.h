#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace gte {

// Cached "may this mesh asset be clustered" answer; the file identity guards against stale data.
struct ClusterEligibilityCache {
    std::string path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type lastWrite{};
    std::chrono::steady_clock::time_point lastCheck{};
    std::string refusalReason; // Empty = eligible.
    bool valid = false;
};

// Persistent Inspector state of the cluster tool and the normals tool.
struct ClusterToolUiState {
    int weldIndex = 0; // 0 = Exact, 1 = PositionOnly.
    int maxVertices = 64;
    int maxTriangles = 124;
    float coneWeight = 0.5f;
    ClusterEligibilityCache eligibility;
    bool startRefusedBusy = false; // The last "Build Clusters" click found another build running.
    bool lastNormalsSucceeded = false;
    std::string lastNormalsError;
};

inline constexpr std::chrono::milliseconds kClusterEligibilityRecheckInterval{ 500 };

// Limits of the cluster options.
int ClampClusterMaxVertices(int value) noexcept;      // 3..255
int ClampClusterMaxTriangles(int value) noexcept;     // 4..512, rounded to a multiple of 4
float ClampClusterConeWeight(float value) noexcept;   // 0..1, non-finite -> default

// "N clusters, avg T tris, vertices A -> B, X ms".
std::string FormatClusterStatusLine(std::size_t clusterCount, float avgTrianglesPerCluster, std::size_t verticesBefore,
    std::size_t verticesAfter, double milliseconds);

// Re-reads the metadata of `utf8Path` when the cache is for another path, or when the recheck interval
// has passed and the file identity (size + last write) changed. Never reads the file every frame.
const ClusterEligibilityCache& RefreshClusterEligibility(
    ClusterToolUiState& state, const std::string& utf8Path, std::chrono::steady_clock::time_point now);

} // namespace gte
