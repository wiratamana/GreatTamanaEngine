#pragma once

#include "AssetDatabase.h"

#include <filesystem>
#include <string>

namespace gte {

// Which loader produced a mesh import (see AssetImportResult::meshSourceFormat).
// Values are never renumbered; append only.
enum class MeshSourceFormat {
    Unknown = 0, // Not a mesh import.
    Pmx = 1,
    Stl = 2,
    Gltf = 3,
};

// Outcome of one ImportAssetFile() call. Always fully populated; `message` is always set.
// The convertedTo* flags are mutually exclusive; none set means a plain file copy.
struct AssetImportResult {
    bool success = false;

    bool convertedToKtx2 = false;       // Image -> KTX2 Texture *.gta.
    bool convertedToMeshAsset = false;  // Model -> Mesh *.gta.
    bool convertedToMotionAsset = false; // Motion -> Animation *.gta.

    // Only meaningful when convertedToMeshAsset is true.
    MeshSourceFormat meshSourceFormat = MeshSourceFormat::Unknown;

    // The *.gta path when any convertedTo* flag is set, otherwise the plain-copy destination.
    std::filesystem::path finalPath;

    // The asset's Guid (reused on re-import) when any convertedTo* flag is set, otherwise invalid.
    Guid guid;

    // Mesh imports only: geometry counts.
    std::size_t meshVertexCount = 0;
    std::size_t meshTriangleCount = 0;

    // Mesh imports only: rig counts (all zero for a static mesh).
    std::size_t skinnedVertexCount = 0;
    std::size_t boneCount = 0;
    std::size_t morphCount = 0;
    std::size_t rigidBodyCount = 0;
    std::size_t jointCount = 0;

    // Mesh imports only: material and texture counts. `missingTextureCount` is how many referenced
    // textures could not be imported (the import still succeeds).
    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::size_t missingTextureCount = 0;

    // Motion imports only: keyframe counts per track (zero for an unused track is normal).
    std::size_t motionBoneKeyframeCount = 0;
    std::size_t motionMorphKeyframeCount = 0;
    std::size_t motionCameraKeyframeCount = 0;
    std::size_t motionLightKeyframeCount = 0;
    std::size_t motionShadowKeyframeCount = 0;
    std::size_t motionIkKeyframeCount = 0;

    std::string message; // Human-readable status, success or failure.
};

// True if the extension (lowercase, with dot) is an image format that is decoded and re-encoded as KTX2.
// Same list as src/Editor/AssetInspectorData.h; duplicated so src/Assets never depends on src/Editor.
bool IsImportableAsKtx2Texture(const std::string& extensionLowercaseWithDot);

// True if the extension (lowercase, with dot) is a model format parsed into a MeshData: ".pmx", ".stl"
// or ".gltf". Textures a model references become sibling Texture *.gta files.
bool IsImportableAsMeshAsset(const std::string& extensionLowercaseWithDot);

// True if the extension (lowercase, with dot) is a motion format parsed into a MotionData (".vmd").
bool IsImportableAsMotionAsset(const std::string& extensionLowercaseWithDot);

// Imports one source FILE (never a directory) into the Project. Checked in order:
//   1. Mesh formats: parsed into a MeshData, written as a Mesh *.gta at `preferredDestinationPath`
//      (extension replaced by ".gta"); rig data and materials go into the *.gta metadata section.
//      A mesh that fails to parse falls back to a plain copy, except ".gltf" which fails (a lone
//      .gltf copy is useless).
//   2. Motion formats: written as an Animation *.gta.
//   3. Image formats: re-encoded as KTX2 and written as a Texture *.gta.
// Every other extension is copied unchanged. The result is queryable through `database` on return.
// Never throws.
AssetImportResult ImportAssetFile(AssetDatabase& database, const std::filesystem::path& sourcePath,
    const std::filesystem::path& preferredDestinationPath);

} // namespace gte
