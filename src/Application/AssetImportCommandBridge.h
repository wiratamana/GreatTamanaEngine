#pragma once

// Cross-thread bridge for a network-driven external-asset import into the
// Editor's "Project" folder, via the same AssetDatabase instance the
// Project panel owns. See SyncCommandBridge.h for the handoff mechanism.

#include "SyncCommandBridge.h"

#include <cstdint>
#include <string>

namespace gte {

enum class AssetImportCommandKind {
    ImportExternalFile,
};

// Plain request payload for one ImportExternalFile command.
struct ImportExternalFileCommand {
    std::string sourceAbsolutePath;
    // "" means "import directly into the Project root".
    std::string destinationRelativeFolder;
};

struct AssetImportCommandRequest {
    AssetImportCommandKind kind = AssetImportCommandKind::ImportExternalFile;
    ImportExternalFileCommand importExternalFile;
};

// Outcome of one ImportExternalFile command - plain scalars only, no
// std::filesystem::path/Guid type, so this header stays free of any
// src/Assets/ dependency.
//
// - projectAvailable == false: the Editor's Project panel does not exist in
//   this build - every other field is meaningless.
// - projectAvailable == true, success == false: the import itself failed -
//   `message` explains why.
// - success == true: every field below is meaningful.
struct ImportExternalFileOutcome {
    bool projectAvailable = true;
    bool success = false;
    std::string message;

    std::string finalRelativePath; // relative to the Project root, forward slashes
    std::string finalAbsolutePath;
    std::string guid; // Guid::ToString() format, or "" if not applicable

    bool convertedToMeshAsset = false;
    std::string meshSourceFormat; // "stl" / "pmx" / "" (meaningful only when convertedToMeshAsset)
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;

    std::uint64_t meshVertexCount = 0;   // meaningful only when convertedToMeshAsset
    std::uint64_t meshTriangleCount = 0; // meaningful only when convertedToMeshAsset
};

struct AssetImportCommandResult {
    AssetImportCommandKind kind = AssetImportCommandKind::ImportExternalFile;
    ImportExternalFileOutcome importExternalFile;
};

// A real import can legitimately take a long time (large mesh parse) - this
// bridge needs headroom the shared template's 3000ms default does not give
// it. Thin forwarding override restores the correct default; the mechanism
// itself is fully inherited.
class AssetImportCommandBridge : public SyncCommandBridge<AssetImportCommandRequest, AssetImportCommandResult> {
public:
    SubmitResult SubmitAndWait(AssetImportCommandRequest request, int timeoutMilliseconds = 120000)
    {
        return SyncCommandBridge::SubmitAndWait(std::move(request), timeoutMilliseconds);
    }
};

} // namespace gte
