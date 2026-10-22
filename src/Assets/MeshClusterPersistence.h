#pragma once

#include "GtaFile.h"
#include "MeshClustering.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

namespace gte {

struct MeshClusterBuildOutcome {
    bool success = false;
    std::string message;
    MeshClusterReport report;
};

// Reason why a mesh asset may not be clustered (rig data indexes vertices), or nullopt when it may.
// Reads the metadata only.
std::optional<std::string> ClusterBuildRefusalReason(const GtaMetadataData& gta);

// Reads a mesh *.gta, rebuilds its cluster table (any old table is discarded) and writes the
// file back atomically. Safe to call from a job: touches no shared state except the file.
// Any failure reports success false and leaves the file byte-identical. Never throws.
MeshClusterBuildOutcome BuildClustersInGtaFile(
    const std::filesystem::path& gtaPath, const MeshClusterOptions& options, const std::atomic<bool>* cancel);

} // namespace gte
