#pragma once

#include "MeshCluster.h"
#include "MeshData.h"

#include <cstddef>
#include <span>
#include <string>

namespace gte {

// True when `clusters` tiles [0, indexCount) exactly and every record is sane. An empty table is valid only
// when indexCount is 0. On failure `why` (when non-null) names the first problem.
bool ValidateClusterTable(std::span<const MeshCluster> clusters, std::size_t indexCount, std::string* why);

// Validates mesh.clusters against mesh.indices. An empty table means "not clustered" and is valid.
bool ValidateMeshClusters(const MeshData& mesh, std::string* why);

} // namespace gte
