#pragma once

#include "../Assets/MeshClusterOptions.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace gte {

struct MeshClusterCloneOptions;

// Cached "may this mesh asset be clustered" answer; the file identity guards against stale data.
struct ClusterEligibilityCache {
    std::string path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type lastWrite{};
    std::chrono::steady_clock::time_point lastCheck{};
    std::string refusalReason; // Empty = eligible.
    bool valid = false;
};

// Cached name of the next clustered copy; the name search touches the disk, so it is not redone every frame.
struct CloneDestinationCache {
    std::string source;
    std::filesystem::path destination; // Empty = no free name.
    std::uint64_t finishedCount = 0;   // Runner FinishedCount() at the time of the search.
    std::chrono::steady_clock::time_point lastCheck{};
    bool valid = false;
};

// What the Inspector drew for the selected mesh asset on its last frame. Cleared at the start of each draw.
struct InspectorShownState {
    std::string assetPath;
    std::string clustersText;  // The "Clusters: N" metadata line.
    std::string clusterStatus; // Status line of the cluster tool.
    bool buildEnabled = false;
    std::string buildDisabledReason;
    std::string cloneButtonDisabledReason; // Empty = the clone button is enabled.
    std::string cloneDestination;          // File name only; empty when none.
    std::string cloneWeld;                 // "auto" | "exact" | "position_only"; empty when not drawn.
    bool openViewerAfterClone = false;
};

// Persistent Inspector state of the cluster tool and the normals tool.
struct ClusterToolUiState {
    int weldIndex = 0; // 0 = Exact, 1 = PositionOnly.
    int maxVertices = static_cast<int>(MeshClusterOptions{}.maxVerticesPerCluster);
    int maxTriangles = static_cast<int>(MeshClusterOptions{}.maxTrianglesPerCluster);
    float coneWeight = MeshClusterOptions{}.coneWeight;
    int cloneWeldIndex = 2;              // 0 = Exact, 1 = PositionOnly, 2 = Auto.
    bool openViewerAfterClone = true;
    std::string lastCloneStatus;
    ClusterEligibilityCache eligibility;
    CloneDestinationCache cloneDestination;
    bool startRefusedBusy = false; // The last "Build Clusters" click found another build running.
    bool lastNormalsSucceeded = false;
    std::string lastNormalsError;
    InspectorShownState shown;
};

// Outcome of asking the cluster tool to start a build. Ready is only the gate result, never a start outcome.
enum class ClusterBuildStart {
    Ready,
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
int ClampCloneWeldIndex(int value) noexcept;          // 0..2

// Clamped options of the "Build Clusters" tool; the weld comes from `weldIndex`.
MeshClusterOptions MakeClusterOptions(const ClusterToolUiState& ui);

// Clamped options of "Create Clustered Copy"; weld index 2 means auto. Used by every clone entry point.
MeshClusterCloneOptions MakeCloneOptions(const ClusterToolUiState& ui);

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

// Project-root relative form of `absolute` with '/' separators; empty on failure.
// `absolute` must already be inside `projectRoot` (see ValidateMeshAssetPath()).
std::string ProjectRelativeSlashPath(const std::filesystem::path& absolute, const std::filesystem::path& projectRoot);

} // namespace gte
