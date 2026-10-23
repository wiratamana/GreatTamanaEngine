#pragma once

#include "GtaFile.h"
#include "MeshClustering.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gte {

struct MeshClusterBuildOutcome {
    bool success = false;
    std::string message;
    MeshClusterReport report;
};

// Result of the shared cluster build: the encoded mesh with its cluster table, ready to be written.
struct ClusteredPayload {
    bool success = false;
    std::string message;
    std::vector<std::uint8_t> payload;
    MeshClusterReport report;
    ClusterWeldMode weldUsed = ClusterWeldMode::Exact;
    bool weldFellBack = false; // Exact fill was poor and PositionOnly was used
};

// Reason why a mesh asset may not be clustered (rig data indexes vertices), or nullopt when it may.
// Reads the metadata only.
std::optional<std::string> ClusterBuildRefusalReason(const GtaMetadataData& gta);

// Builds the cluster table of a mesh asset (autoWeld: Exact first, PositionOnly when the fill is poor).
// Releases gta.payload, leaves header and metadata alone, writes no file.
ClusteredPayload BuildClusteredPayload(
    GtaFileData& gta, const MeshClusterOptions& options, bool autoWeld, const std::atomic<bool>* cancel);

// Reads a mesh *.gta, rebuilds its cluster table (any old table is discarded) and writes the
// file back atomically. Safe to call from a job: touches no shared state except the file.
// Any failure reports success false and leaves the file byte-identical. Never throws.
MeshClusterBuildOutcome BuildClustersInGtaFile(
    const std::filesystem::path& gtaPath, const MeshClusterOptions& options, const std::atomic<bool>* cancel);

} // namespace gte
