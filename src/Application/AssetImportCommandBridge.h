#pragma once

// Cross-thread bridge for asset commands served by the Editor on the main thread:
// importing an external file into the Project folder, rebuilding a mesh's cluster table and
// cloning a mesh into a clustered copy.
// See SyncCommandBridge.h for the handoff mechanism. Plain scalars and strings only.

#include "SyncCommandBridge.h"
#include "../Assets/MeshClusterOptions.h"

#include <cstdint>
#include <string>

namespace gte {

enum class AssetImportCommandKind {
    ImportExternalFile,
    BuildMeshClusters,
    CloneMeshClusters,
};

// Plain request payload for one ImportExternalFile command.
struct ImportExternalFileCommand {
    std::string sourceAbsolutePath;
    // "" means "import directly into the Project root".
    std::string destinationRelativeFolder;
};

// Request payload for one BuildMeshClusters command. `weld` is "exact" or "position_only".
struct BuildMeshClustersCommand {
    std::string gtaPath;
    std::string weld = "exact";
    std::uint32_t maxVertices = MeshClusterOptions{}.maxVerticesPerCluster;
    std::uint32_t maxTriangles = MeshClusterOptions{}.maxTrianglesPerCluster;
    float coneWeight = MeshClusterOptions{}.coneWeight;
};

// Request payload for one CloneMeshClusters command. `weld` is "auto", "exact" or "position_only".
// The destination is always chosen by the editor, never by the caller.
struct CloneMeshClustersCommand {
    std::string gtaPath;
    std::string weld = "auto";
    bool openViewer = false;
    std::uint32_t maxVertices = MeshClusterOptions{}.maxVerticesPerCluster;
    std::uint32_t maxTriangles = MeshClusterOptions{}.maxTrianglesPerCluster;
    float coneWeight = MeshClusterOptions{}.coneWeight;
};

struct AssetImportCommandRequest {
    AssetImportCommandKind kind = AssetImportCommandKind::ImportExternalFile;
    ImportExternalFileCommand importExternalFile;
    BuildMeshClustersCommand buildMeshClusters;
    CloneMeshClustersCommand cloneMeshClusters;
};

// Outcome of one ImportExternalFile command.
// - projectAvailable == false: the Editor's Project panel does not exist in this build.
// - projectAvailable == true, success == false: the import failed, `message` says why.
// - success == true: every field below is meaningful.
struct ImportExternalFileOutcome {
    bool projectAvailable = true;
    bool success = false;
    std::string message;

    std::string finalRelativePath; // relative to the Project root, forward slashes
    std::string finalAbsolutePath;
    std::string guid; // Guid::ToString() format, or "" if not applicable

    bool convertedToMeshAsset = false;
    std::string meshSourceFormat; // "stl" / "pmx" / "gltf" / "" (meaningful only when convertedToMeshAsset)
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;

    std::uint64_t meshVertexCount = 0;   // meaningful only when convertedToMeshAsset
    std::uint64_t meshTriangleCount = 0; // meaningful only when convertedToMeshAsset
};

// Maps to the HTTP status of the route: Ok 200, BadRequest 400, Busy 503 (another build or import
// runs), Unavailable 503 (no editor runner in this build).
enum class BuildMeshClustersStatus : std::uint8_t { Ok, BadRequest, Busy, Unavailable };

// Outcome of one BuildMeshClusters command. The report fields are meaningful when success is true.
struct BuildMeshClustersOutcome {
    BuildMeshClustersStatus status = BuildMeshClustersStatus::Unavailable;
    bool success = false;
    std::string message;

    std::string weld;
    std::uint64_t vertexCountBefore = 0;
    std::uint64_t vertexCountAfter = 0;
    std::uint64_t triangleCount = 0;
    std::uint64_t clusterCount = 0;
    double avgTrianglesPerCluster = 0.0;
    double avgVerticesPerCluster = 0.0;
    bool poorFill = false;
    double milliseconds = 0.0;
};

// Outcome of one CloneMeshClusters command; status uses the same HTTP mapping as BuildMeshClustersStatus.
// The other fields are meaningful when success is true.
struct CloneMeshClustersOutcome {
    BuildMeshClustersStatus status = BuildMeshClustersStatus::Unavailable;
    bool success = false;
    std::string message;

    std::string createdPath;
    std::string guid; // Guid::ToString() format
    std::string weldUsed; // "exact" or "position_only"
    bool weldFellBack = false;
    std::uint64_t vertexCountBefore = 0;
    std::uint64_t vertexCountAfter = 0;
    std::uint64_t triangleCount = 0;
    std::uint64_t clusterCount = 0;
    double avgTrianglesPerCluster = 0.0;
    double milliseconds = 0.0;
};

struct AssetImportCommandResult {
    AssetImportCommandKind kind = AssetImportCommandKind::ImportExternalFile;
    ImportExternalFileOutcome importExternalFile;
    BuildMeshClustersOutcome buildMeshClusters;
    CloneMeshClustersOutcome cloneMeshClusters;
};

// A real import or cluster build can take a long time, so this bridge waits longer than the
// shared template's 3000ms default.
class AssetImportCommandBridge : public SyncCommandBridge<AssetImportCommandRequest, AssetImportCommandResult> {
public:
    SubmitResult SubmitAndWait(AssetImportCommandRequest request, int timeoutMilliseconds = 120000)
    {
        return SyncCommandBridge::SubmitAndWait(std::move(request), timeoutMilliseconds);
    }
};

} // namespace gte
