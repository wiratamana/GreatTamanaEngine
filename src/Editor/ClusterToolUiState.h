#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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

// What the Inspector drew for the selected mesh asset on its last frame. Cleared at the start of each draw.
struct InspectorShownState {
    std::string assetPath;
    std::string clustersText;  // The "Clusters: N" metadata line.
    std::string clusterStatus; // Status line of the cluster tool.
    bool buildEnabled = false;
    std::string buildDisabledReason;
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
    InspectorShownState shown;
};

// Outcome of asking the cluster tool to start a build.
enum class ClusterBuildStart {
    Started,
    Busy,
    Unavailable,
    Refused,
};

struct ClusterBuildStartResult {
    ClusterBuildStart status = ClusterBuildStart::Unavailable;
    std::string message;
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

// Checks that `utf8Path` is a readable mesh *.gta strictly inside `projectRoot`.
// Fills `outPath` and returns nullopt when it is; otherwise returns the reason.
std::optional<std::string> ValidateMeshAssetPath(
    const std::string& utf8Path, const std::filesystem::path& projectRoot, std::filesystem::path& outPath);

// Project-root relative form of `absolute` with '/' separators; empty when it is not inside `projectRoot`.
std::string ProjectRelativeSlashPath(const std::filesystem::path& absolute, const std::filesystem::path& projectRoot);

} // namespace gte
