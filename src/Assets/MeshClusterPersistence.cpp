#include "MeshClusterPersistence.h"

#include "AtomicFile.h"
#include "MeshFile.h"
#include "RigFile.h"

#include <array>
#include <exception>
#include <span>
#include <utility>
#include <vector>

namespace gte {

namespace {

// Decodes the rig metadata. Empty metadata is a rig-free mesh with no materials.
std::optional<std::string> DecodeRigForClustering(const std::vector<std::uint8_t>& metadata, RigFileData& rig)
{
    if (metadata.empty()) {
        return std::nullopt;
    }
    std::optional<RigFileData> decoded = DecodeRigDataFromBytes(metadata);
    if (!decoded.has_value()) {
        return std::string("rig metadata could not be decoded");
    }
    rig = std::move(*decoded);

    if (!rig.skinWeights.empty()) {
        return std::string("mesh has skin weights");
    }
    if (!rig.skeleton.bones.empty()) {
        return std::string("mesh has a skeleton");
    }
    if (!rig.morphs.morphs.empty()) {
        return std::string("mesh has morphs");
    }
    if (!rig.physics.rigidBodies.empty() || !rig.physics.joints.empty()) {
        return std::string("mesh has physics data");
    }
    return std::nullopt;
}

MeshClusterBuildOutcome Failure(std::string message)
{
    MeshClusterBuildOutcome outcome;
    outcome.success = false;
    outcome.message = std::move(message);
    return outcome;
}

bool IsCancelled(const std::atomic<bool>* cancel) noexcept
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

void Release(std::vector<std::uint8_t>& bytes)
{
    std::vector<std::uint8_t>().swap(bytes);
}

MeshClusterBuildOutcome BuildImpl(
    const std::filesystem::path& gtaPath, const MeshClusterOptions& options, const std::atomic<bool>* cancel)
{
    const std::optional<FileIdentity> identity = ReadFileIdentity(gtaPath);
    if (!identity.has_value()) {
        return Failure("file not found");
    }

    std::optional<GtaFileData> gta = ReadGtaFile(gtaPath);
    if (!gta.has_value()) {
        return Failure("could not read the *.gta file");
    }
    if (gta->header.Type() != AssetType::Mesh) {
        return Failure("not a mesh asset");
    }

    RigFileData rig;
    if (const std::optional<std::string> refusal = DecodeRigForClustering(gta->metadata, rig); refusal.has_value()) {
        return Failure("refused: " + *refusal);
    }

    std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    Release(gta->payload); // Decoded arrays replace the raw bytes before the build starts.
    if (!mesh.has_value()) {
        return Failure("the mesh payload could not be decoded");
    }
    mesh->clusters.clear();

    if (IsCancelled(cancel)) {
        return Failure("cancelled");
    }

    MeshClusterBuildOutcome outcome;
    outcome.report = BuildMeshClusters(*mesh, rig.materials.materials, options, cancel);
    if (!outcome.report.success) {
        outcome.message = outcome.report.message;
        return outcome;
    }
    if (IsCancelled(cancel)) {
        return Failure("cancelled");
    }

    std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(*mesh);
    mesh.reset();
    if (payload.empty()) {
        return Failure("the clustered mesh could not be encoded");
    }
    if (IsCancelled(cancel)) {
        return Failure("cancelled");
    }

    // Header and metadata are written back unchanged.
    const std::array<ConstByteSpan, 3> parts = {
        std::as_bytes(std::span<const GtaHeader, 1>(&gta->header, 1)),
        std::as_bytes(std::span<const std::uint8_t>(gta->metadata)),
        std::as_bytes(std::span<const std::uint8_t>(payload)),
    };
    std::string error;
    if (!WriteFileAtomically(gtaPath, parts, &*identity, &error)) {
        outcome.report.success = false;
        outcome.message = "write failed: " + error;
        return outcome;
    }

    outcome.success = true;
    outcome.message = outcome.report.message;
    return outcome;
}

} // namespace

std::optional<std::string> ClusterBuildRefusalReason(const GtaMetadataData& gta)
{
    if (gta.header.Type() != AssetType::Mesh) {
        return std::string("not a mesh asset");
    }
    RigFileData rig;
    return DecodeRigForClustering(gta.metadata, rig);
}

MeshClusterBuildOutcome BuildClustersInGtaFile(
    const std::filesystem::path& gtaPath, const MeshClusterOptions& options, const std::atomic<bool>* cancel)
{
    try {
        return BuildImpl(gtaPath, options, cancel);
    } catch (const std::exception& e) {
        return Failure(std::string("unexpected error: ") + e.what());
    } catch (...) {
        return Failure("unexpected error");
    }
}

} // namespace gte
