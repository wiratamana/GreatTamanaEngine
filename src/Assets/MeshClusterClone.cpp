#include "MeshClusterClone.h"

#include "GtaFile.h"

#include <exception>
#include <optional>
#include <system_error>
#include <utility>

namespace gte {

namespace {

constexpr int kMaxDestinationTries = 1000;

MeshClusterCloneOutcome Failure(std::string message)
{
    MeshClusterCloneOutcome outcome;
    outcome.message = std::move(message);
    return outcome;
}

// True when the path exists or cannot be checked.
bool ExistsOrUnknown(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) || ec;
}

std::string DescribeWeld(const MeshClusterCloneOutcome& outcome)
{
    if (outcome.weldUsed == ClusterWeldMode::Exact) {
        return "weld Exact";
    }
    return outcome.weldFellBack ? "weld Position only (Exact was poor)" : "weld Position only";
}

MeshClusterCloneOutcome CloneImpl(const std::filesystem::path& source, const std::filesystem::path& destination,
    const MeshClusterCloneOptions& options, const std::atomic<bool>* cancel)
{
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
        return Failure("cancelled");
    }
    if (ExistsOrUnknown(destination)) {
        return Failure("destination exists");
    }

    std::optional<GtaFileData> gta = ReadGtaFile(source);
    if (!gta.has_value()) {
        return Failure("could not read the *.gta file");
    }

    ClusteredPayload built = BuildClusteredPayload(*gta, options.cluster, options.autoWeld, cancel);
    MeshClusterCloneOutcome outcome;
    outcome.report = std::move(built.report);
    outcome.weldUsed = built.weldUsed;
    outcome.weldFellBack = built.weldFellBack;
    if (!built.success) {
        outcome.message = std::move(built.message);
        return outcome;
    }

    // Create-only write: a file that appears after the check above is not replaced.
    const Guid guid = Guid::Generate();
    std::string error;
    if (!WriteGtaFile(destination, AssetType::Mesh, guid, gta->header.Flags(), gta->metadata, built.payload,
            gta->header.version, &error, true)) {
        outcome.message = std::move(error);
        return outcome;
    }

    outcome.success = true;
    outcome.createdPath = destination;
    outcome.guid = guid;
    outcome.message = outcome.report.message + "; " + DescribeWeld(outcome);
    return outcome;
}

} // namespace

MeshClusterCloneOutcome CloneMeshAssetWithClusters(const std::filesystem::path& source,
    const std::filesystem::path& destination, const MeshClusterCloneOptions& options,
    const std::atomic<bool>* cancel)
{
    try {
        return CloneImpl(source, destination, options, cancel);
    } catch (const std::exception& e) {
        return Failure(std::string("unexpected error: ") + e.what());
    } catch (...) {
        return Failure("unexpected error");
    }
}

std::filesystem::path ChooseClusteredCloneDestination(const std::filesystem::path& source)
{
    try {
        for (int attempt = 1; attempt <= kMaxDestinationTries; ++attempt) {
            std::filesystem::path name = source.stem();
            name += attempt == 1 ? std::string("_clustered") : "_clustered_" + std::to_string(attempt);
            name += ".gta";
            std::filesystem::path candidate = source.parent_path() / name;
            if (!ExistsOrUnknown(candidate)) {
                return candidate;
            }
        }
    } catch (...) {
        // Falls through to the empty "no name" result.
    }
    return {};
}

} // namespace gte
