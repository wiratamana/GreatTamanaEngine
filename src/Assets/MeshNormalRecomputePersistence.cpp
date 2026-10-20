#include "MeshNormalRecomputePersistence.h"

#include "../Encoding/Utf8Path.h"
#include "AssetTypes.h"
#include "GtaFile.h"
#include "MeshFile.h"
#include "MeshNormalRecompute.h"
#include "MeshTangents.h"

#include <filesystem>
#include <optional>

namespace gte {

namespace {

void SetError(std::string* outErrorMessage, const char* message)
{
    if (outErrorMessage != nullptr) {
        *outErrorMessage = message;
    }
}

} // namespace

bool RecomputeAndSaveMeshNormalsToGtaFile(const std::string& absoluteGtaPath, std::string* outErrorMessage)
{
    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absoluteGtaPath));
    if (!gta.has_value()) {
        SetError(outErrorMessage, "Could not read the *.gta file (missing, unreadable, or bad magic).");
        return false;
    }
    if (gta->header.Type() != AssetType::Mesh) {
        SetError(outErrorMessage, "This *.gta file is not an AssetType::Mesh asset.");
        return false;
    }

    std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value()) {
        SetError(outErrorMessage, "This *.gta file's existing mesh payload could not be decoded - refusing to overwrite it.");
        return false;
    }

    RecomputeMeshNormalsFromGeometry(*mesh);
    if (!mesh->tangents.empty()) {
        OrthonormalizeMeshTangents(*mesh); // Tangents must stay perpendicular to the new normals.
    }

    const std::vector<std::uint8_t> newPayload = EncodeMeshDataToBytes(*mesh);
    const bool wrote = WriteGtaFile(PathFromUtf8(absoluteGtaPath), gta->header.Type(), gta->header.Id(),
        gta->header.Flags(), gta->metadata, newPayload, gta->header.version);
    if (!wrote) {
        SetError(outErrorMessage, "Failed to write the *.gta file back to disk.");
        return false;
    }
    return true;
}

} // namespace gte
