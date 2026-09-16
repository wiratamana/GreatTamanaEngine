#include "MeshNormalRecomputePersistence.h"

#include "AssetTypes.h"
#include "GtaFile.h"
#include "MeshFile.h"
#include "MeshNormalRecompute.h"

#include <filesystem>
#include <optional>

namespace gte {

namespace {

// Mirrors DynamicChainPhysicsPersistence.cpp's own Utf8PathFromGamePath() -
// same std::u8string round-trip, same reasoning (a std::string holding
// UTF-8 game/asset-relative-or-absolute path text must be reinterpreted
// through std::u8string before handing it to std::filesystem::path, or
// non-ASCII characters get mis-decoded via the platform's native narrow
// encoding on Windows). Duplicated here rather than shared/exported from
// that file specifically because it is a 2-call-site helper - not a case to
// introduce a new shared utility header for (see that file's own comment
// on this exact point); if a THIRD call site ever needs this, promote it
// then.
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

void SetError(std::string* outErrorMessage, const char* message)
{
    if (outErrorMessage != nullptr) {
        *outErrorMessage = message;
    }
}

} // namespace

bool RecomputeAndSaveMeshNormalsToGtaFile(const std::string& absoluteGtaPath, std::string* outErrorMessage)
{
    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteGtaPath));
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

    const std::vector<std::uint8_t> newPayload = EncodeMeshDataToBytes(*mesh);
    const bool wrote = WriteGtaFile(Utf8PathFromGamePath(absoluteGtaPath), gta->header.Type(), gta->header.Id(),
        gta->header.Flags(), gta->metadata, newPayload, gta->header.version);
    if (!wrote) {
        SetError(outErrorMessage, "Failed to write the *.gta file back to disk.");
        return false;
    }
    return true;
}

} // namespace gte
