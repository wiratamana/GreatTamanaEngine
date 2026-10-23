#pragma once

// Creates a clustered copy of a mesh asset. The source file is never touched.

#include "AssetTypes.h"
#include "MeshClusterPersistence.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace gte {

struct MeshClusterCloneOptions {
    MeshClusterOptions cluster; // limits and cone weight; the weld is used only when autoWeld is false
    bool autoWeld = true;       // Exact first, PositionOnly when the Exact fill is poor
};

struct MeshClusterCloneOutcome {
    bool success = false;
    std::string message;
    std::filesystem::path createdPath;
    Guid guid;
    MeshClusterReport report; // report of the build that produced the file
    ClusterWeldMode weldUsed = ClusterWeldMode::Exact;
    bool weldFellBack = false; // Exact was poor and PositionOnly was used
};

// Reads `source`, builds clusters and writes a NEW file at `destination` with a new GUID.
// Never replaces an existing file. Never throws. Safe to call from a job.
MeshClusterCloneOutcome CloneMeshAssetWithClusters(const std::filesystem::path& source,
    const std::filesystem::path& destination, const MeshClusterCloneOptions& options,
    const std::atomic<bool>* cancel);

// "<stem>_clustered.gta", then "_clustered_2", ... next to `source`: the first name that does not exist.
// Empty when no free name is found within 1000 tries.
std::filesystem::path ChooseClusteredCloneDestination(const std::filesystem::path& source);

} // namespace gte
