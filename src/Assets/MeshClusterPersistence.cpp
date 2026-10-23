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

ClusteredPayload PayloadFailure(std::string message)
{
    ClusteredPayload result;
    result.message = std::move(message);
    return result;
}

bool IsCancelled(const std::atomic<bool>* cancel) noexcept
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

void Release(std::vector<std::uint8_t>& bytes)
{
    std::vector<std::uint8_t>().swap(bytes);
}

MeshClusterBuildOutcome Failure(std::string message)
{
    MeshClusterBuildOutcome outcome;
    outcome.message = std::move(message);
    return outcome;
}

// Builds with Exact first and falls back to PositionOnly when the Exact fill is poor.
// On failure `result` carries the report and the message.
bool BuildWithAutoWeld(MeshData& mesh, const std::vector<Material>& materials, MeshClusterOptions options,
    const std::atomic<bool>* cancel, ClusteredPayload& result)
{
    options.weld = ClusterWeldMode::Exact;
    result.report = BuildMeshClusters(mesh, materials, options, cancel, true);
    result.weldUsed = ClusterWeldMode::Exact;
    if (result.report.success) {
        return true;
    }
    if (!result.report.poorFill) {
        result.message = result.report.message;
        return false;
    }

    const std::string exactMessage = result.report.message;
    options.weld = ClusterWeldMode::PositionOnly;
    result.report = BuildMeshClusters(mesh, materials, options, cancel);
    result.weldUsed = ClusterWeldMode::PositionOnly;
    result.weldFellBack = true;
    if (!result.report.success) {
        result.message = "Exact weld: " + exactMessage + "; Position only weld: " + result.report.message;
        return false;
    }
    return true;
}

} // namespace

ClusteredPayload BuildClusteredPayload(
    GtaFileData& gta, const MeshClusterOptions& options, bool autoWeld, const std::atomic<bool>* cancel)
{
    if (gta.header.Type() != AssetType::Mesh) {
        return PayloadFailure("not a mesh asset");
    }

    RigFileData rig;
    if (const std::optional<std::string> refusal = DecodeRigForClustering(gta.metadata, rig); refusal.has_value()) {
        return PayloadFailure("refused: " + *refusal);
    }

    std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta.payload);
    Release(gta.payload); // Decoded arrays replace the raw bytes before the build starts.
    if (!mesh.has_value()) {
        return PayloadFailure("the mesh payload could not be decoded");
    }
    mesh->clusters.clear();

    if (IsCancelled(cancel)) {
        return PayloadFailure("cancelled");
    }

    ClusteredPayload result;
    if (autoWeld) {
        if (!BuildWithAutoWeld(*mesh, rig.materials.materials, options, cancel, result)) {
            return result;
        }
    } else {
        result.report = BuildMeshClusters(*mesh, rig.materials.materials, options, cancel);
        result.weldUsed = options.weld;
        if (!result.report.success) {
            result.message = result.report.message;
            return result;
        }
    }
    if (IsCancelled(cancel)) {
        return PayloadFailure("cancelled");
    }

    result.payload = EncodeMeshDataToBytes(*mesh);
    mesh.reset();
    if (result.payload.empty()) {
        return PayloadFailure("the clustered mesh could not be encoded");
    }
    if (IsCancelled(cancel)) {
        return PayloadFailure("cancelled");
    }

    result.success = true;
    result.message = result.report.message;
    return result;
}

std::optional<std::string> ClusterBuildRefusalReason(const GtaMetadataData& gta)
{
    if (gta.header.Type() != AssetType::Mesh) {
        return std::string("not a mesh asset");
    }
    RigFileData rig;
    return DecodeRigForClustering(gta.metadata, rig);
}

namespace {

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

    ClusteredPayload built = BuildClusteredPayload(*gta, options, false, cancel);
    MeshClusterBuildOutcome outcome;
    outcome.report = std::move(built.report);
    outcome.message = std::move(built.message);
    if (!built.success) {
        return outcome;
    }

    // Header and metadata are written back unchanged.
    const std::array<ConstByteSpan, 3> parts = {
        std::as_bytes(std::span<const GtaHeader, 1>(&gta->header, 1)),
        std::as_bytes(std::span<const std::uint8_t>(gta->metadata)),
        std::as_bytes(std::span<const std::uint8_t>(built.payload)),
    };
    std::string error;
    if (!WriteFileAtomically(gtaPath, parts, &*identity, &error)) {
        outcome.report.success = false;
        outcome.message = "write failed: " + error;
        return outcome;
    }

    outcome.success = true;
    return outcome;
}

} // namespace

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
