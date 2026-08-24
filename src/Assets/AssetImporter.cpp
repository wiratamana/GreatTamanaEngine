#include "AssetImporter.h"

#include "Ktx2Encoder.h"
#include "MeshFile.h"
#include "MotionFile.h"
#include "PmxLoader.h"
#include "RigFile.h"
#include "VmdLoader.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace gte {

namespace {

// Same small helpers AssetDatabase.cpp already keeps locally (see its own
// comment) - duplicated here rather than shared, for the exact same
// "src/Assets/ never depends on src/Editor/" reasoning as
// IsImportableAsKtx2Texture()'s own doc comment.
std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// The exact inverse of PathToUtf8() above - same std::u8string round-trip
// PmxLoader.cpp's own Utf8ToPath() uses, duplicated here for the same
// "src/Assets/ never depends on src/Editor/" reasoning IsImportableAsKtx2Texture()'s
// own doc comment gives. Needed to turn a MaterialTextureRef::sourcePath
// (always UTF-8 - see PmxLoader.cpp's ResolveTexturePath()) back into a
// real std::filesystem::path for ImportPmxMaterialTextures() below.
std::filesystem::path Utf8ToPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Imports every resolvable texture a just-parsed .pmx model's materials
// reference (see MaterialData::textures/MaterialTextureRef, MaterialData.h)
// as its OWN standalone *.gta AssetType::Texture asset, sitting right next
// to the model's own destination Mesh *.gta (in a
// "<meshFileStem>_Textures" sibling folder) - this is what makes the
// engine's runtime mesh/texture loading (Game::EnsureMeshAsset(),
// src/Game/Game.cpp) resolve every material's texture purely by Guid
// through an AssetDatabase, instead of ever reaching back out to wherever
// the original source .pmx's own texture folder happened to live on THIS
// machine (which may not even exist on whatever machine later loads the
// resulting Mesh *.gta - the exact "referencing a texture from outside the
// Project" problem this function exists to close).
//
// Mutates `materials` in place, filling in each MaterialTextureRef::guid
// that was successfully imported - a slot whose sourcePath is empty, whose
// source file no longer exists on disk, or that fails to decode as a
// supported image is simply left at Guid::Invalid() (the same "missing
// texture" degrade-gracefully convention MaterialData::textures' own doc
// comment already documents), never treated as a hard failure of the
// overall .pmx import. Never throws.
void ImportPmxMaterialTextures(
    AssetDatabase& database, MaterialData& materials, const std::filesystem::path& meshGtaPath)
{
    if (materials.textures.empty()) {
        return;
    }

    const std::filesystem::path texturesDirectory
        = meshGtaPath.parent_path() / (PathToUtf8(meshGtaPath.stem()) + "_Textures");

    for (std::size_t i = 0; i < materials.textures.size(); ++i) {
        MaterialTextureRef& textureRef = materials.textures[i];
        if (textureRef.sourcePath.empty()) {
            continue; // No texture in this slot at all - nothing to import.
        }

        const std::filesystem::path sourcePath = Utf8ToPath(textureRef.sourcePath);

        std::error_code existsEc;
        if (!std::filesystem::is_regular_file(sourcePath, existsEc) || existsEc) {
            continue; // Referenced texture file is missing on disk - leave guid invalid.
        }

        const std::optional<Ktx2EncodeResult> encoded = EncodeImageFileToKtx2(sourcePath);
        if (!encoded.has_value()) {
            continue; // Not actually decodable as a supported image - leave guid invalid.
        }

        // "<index>_<originalStem>.gta" - the index prefix keeps two
        // different source folders' same-named textures (e.g. two
        // materials both referencing "diffuse.png") from colliding once
        // they all land flattened in the one texturesDirectory.
        const std::filesystem::path textureGtaPath
            = texturesDirectory / (std::to_string(i) + "_" + PathToUtf8(sourcePath.stem()) + ".gta");

        const std::optional<Guid> guid
            = database.ImportAsset(textureGtaPath, AssetType::Texture, std::vector<std::uint8_t>{}, encoded->ktx2Bytes);
        if (guid.has_value()) {
            textureRef.guid = *guid;
        }
    }
}

AssetImportResult ImportAsPlainCopy(const std::filesystem::path& sourcePath,
    const std::filesystem::path& preferredDestinationPath, const std::string& fallbackPrefix)
{
    AssetImportResult result;

    std::error_code dirEc;
    const std::filesystem::path parent = preferredDestinationPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, dirEc); // Best-effort; copy_file() below is the real check.
    }

    std::error_code copyEc;
    std::filesystem::copy_file(sourcePath, preferredDestinationPath, std::filesystem::copy_options::none, copyEc);
    if (copyEc) {
        result.success = false;
        result.message = fallbackPrefix + "Failed to import \"" + PathToUtf8(sourcePath.filename())
            + "\": " + copyEc.message();
        return result;
    }

    result.success = true;
    result.convertedToKtx2 = false;
    result.finalPath = preferredDestinationPath;
    result.message = fallbackPrefix + "Imported \"" + PathToUtf8(preferredDestinationPath.filename()) + "\".";
    return result;
}

} // namespace

bool IsImportableAsKtx2Texture(const std::string& extensionLowercaseWithDot)
{
    static constexpr std::array<const char*, 12> kSupported = {
        ".png",
        ".jpg",
        ".jpeg",
        ".bmp",
        ".tga",
        ".gif",
        ".psd",
        ".hdr",
        ".pic",
        ".pnm",
        ".ppm",
        ".pgm",
    };
    return std::any_of(
        kSupported.begin(), kSupported.end(), [&](const char* ext) { return extensionLowercaseWithDot == ext; });
}

bool IsImportableAsMeshAsset(const std::string& extensionLowercaseWithDot)
{
    return extensionLowercaseWithDot == ".pmx";
}

bool IsImportableAsMotionAsset(const std::string& extensionLowercaseWithDot)
{
    return extensionLowercaseWithDot == ".vmd";
}

AssetImportResult ImportAssetFile(
    AssetDatabase& database, const std::filesystem::path& sourcePath, const std::filesystem::path& preferredDestinationPath)
{
    const std::string extension = ToLowerAscii(PathToUtf8(sourcePath.extension()));

    if (IsImportableAsMeshAsset(extension)) {
        PmxLoadResult loaded = LoadPmxModel(PathToUtf8(sourcePath));
        if (loaded.success) {
            std::filesystem::path gtaPath = preferredDestinationPath;
            gtaPath.replace_extension(".gta");

            // Imports every material's referenced texture as its own
            // *.gta AssetType::Texture asset (see
            // ImportPmxMaterialTextures()'s own doc comment above) and
            // fills in loaded.materials.textures[*].guid accordingly -
            // MUST happen before EncodeRigDataToBytes() below, so the
            // RigFileData this mesh's *.gta metadata gets built from
            // already carries the resolved Guids, never the original
            // machine-local absolute source paths.
            ImportPmxMaterialTextures(database, loaded.materials, gtaPath);

            const std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(loaded.mesh);

            RigFileData rig;
            rig.skinWeights = loaded.mesh.skinWeights;
            rig.skeleton = loaded.skeleton;
            rig.morphs = loaded.morphs;
            rig.physics = loaded.physics;
            rig.materials = loaded.materials;
            const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);

            const std::optional<Guid> guid = database.ImportAsset(gtaPath, AssetType::Mesh, metadata, payload);

            AssetImportResult result;
            if (guid.has_value()) {
                result.success = true;
                result.convertedToMeshAsset = true;
                result.finalPath = gtaPath;
                result.guid = *guid;
                result.meshVertexCount = loaded.mesh.positions.size();
                result.meshTriangleCount = loaded.mesh.indices.size() / 3;
                result.skinnedVertexCount = loaded.mesh.skinWeights.size();
                result.boneCount = loaded.skeleton.bones.size();
                result.morphCount = loaded.morphs.morphs.size();
                result.rigidBodyCount = loaded.physics.rigidBodies.size();
                result.jointCount = loaded.physics.joints.size();
                result.materialCount = loaded.materials.materials.size();
                result.textureCount = loaded.materials.textures.size();
                result.message = "Imported \"" + PathToUtf8(sourcePath.filename()) + "\" as a mesh ("
                    + std::to_string(result.meshVertexCount) + " vertices, " + std::to_string(result.meshTriangleCount)
                    + " triangles, " + std::to_string(result.boneCount) + " bones, " + std::to_string(result.morphCount)
                    + " morphs, " + std::to_string(result.rigidBodyCount) + " rigid bodies, "
                    + std::to_string(result.jointCount) + " joints, " + std::to_string(result.materialCount)
                    + " materials, " + std::to_string(result.textureCount) + " textures) -> \""
                    + PathToUtf8(gtaPath.filename()) + "\".";
            } else {
                result.success = false;
                result.message
                    = "Parsed \"" + PathToUtf8(sourcePath.filename()) + "\" but failed to write its *.gta wrapper.";
            }
            return result;
        }

        // The extension claimed this was a supported mesh format, but it
        // failed to actually parse (corrupt/truncated/not really a .pmx
        // despite its extension) - degrade gracefully to a plain copy
        // rather than failing the whole import outright, same fallback
        // shape as the KTX2 branch below.
        return ImportAsPlainCopy(
            sourcePath, preferredDestinationPath, "Could not parse as a mesh, imported as-is instead. ");
    }

    if (IsImportableAsMotionAsset(extension)) {
        const VmdLoadResult loaded = LoadVmdMotion(PathToUtf8(sourcePath));
        if (loaded.success) {
            std::filesystem::path gtaPath = preferredDestinationPath;
            gtaPath.replace_extension(".gta");

            const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(loaded.motion);

            const std::optional<Guid> guid
                = database.ImportAsset(gtaPath, AssetType::Animation, std::vector<std::uint8_t>{}, payload);

            AssetImportResult result;
            if (guid.has_value()) {
                result.success = true;
                result.convertedToMotionAsset = true;
                result.finalPath = gtaPath;
                result.guid = *guid;
                result.motionBoneKeyframeCount = loaded.motion.boneKeyframes.size();
                result.motionMorphKeyframeCount = loaded.motion.morphKeyframes.size();
                result.motionCameraKeyframeCount = loaded.motion.cameraKeyframes.size();
                result.motionLightKeyframeCount = loaded.motion.lightKeyframes.size();
                result.motionShadowKeyframeCount = loaded.motion.shadowKeyframes.size();
                result.motionIkKeyframeCount = loaded.motion.ikKeyframes.size();
                result.message = "Imported \"" + PathToUtf8(sourcePath.filename()) + "\" as a motion ("
                    + std::to_string(result.motionBoneKeyframeCount) + " bone keyframes, "
                    + std::to_string(result.motionMorphKeyframeCount) + " morph keyframes, "
                    + std::to_string(result.motionCameraKeyframeCount) + " camera keyframes, "
                    + std::to_string(result.motionLightKeyframeCount) + " light keyframes, "
                    + std::to_string(result.motionShadowKeyframeCount) + " shadow keyframes, "
                    + std::to_string(result.motionIkKeyframeCount) + " IK keyframes) -> \""
                    + PathToUtf8(gtaPath.filename()) + "\".";
            } else {
                result.success = false;
                result.message
                    = "Parsed \"" + PathToUtf8(sourcePath.filename()) + "\" but failed to write its *.gta wrapper.";
            }
            return result;
        }

        // The extension claimed this was a supported motion format, but it
        // failed to actually parse (corrupt/truncated/not really a .vmd
        // despite its extension) - degrade gracefully to a plain copy
        // rather than failing the whole import outright, same fallback
        // shape as the mesh/KTX2 branches.
        return ImportAsPlainCopy(
            sourcePath, preferredDestinationPath, "Could not parse as a motion, imported as-is instead. ");
    }

    if (IsImportableAsKtx2Texture(extension)) {
        if (const std::optional<Ktx2EncodeResult> encoded = EncodeImageFileToKtx2(sourcePath); encoded.has_value()) {
            std::filesystem::path gtaPath = preferredDestinationPath;
            gtaPath.replace_extension(".gta");

            const std::optional<Guid> guid
                = database.ImportAsset(gtaPath, AssetType::Texture, std::vector<std::uint8_t>{}, encoded->ktx2Bytes);

            AssetImportResult result;
            if (guid.has_value()) {
                result.success = true;
                result.convertedToKtx2 = true;
                result.finalPath = gtaPath;
                result.guid = *guid;
                result.message = "Imported \"" + PathToUtf8(sourcePath.filename()) + "\" as a " + std::to_string(encoded->width)
                    + "x" + std::to_string(encoded->height) + " KTX2 texture -> \"" + PathToUtf8(gtaPath.filename())
                    + "\".";
            } else {
                result.success = false;
                result.message
                    = "Decoded \"" + PathToUtf8(sourcePath.filename()) + "\" but failed to write its *.gta wrapper.";
            }
            return result;
        }

        // The extension claimed this was a supported image format, but it
        // failed to actually decode (corrupt/truncated/not really an
        // image) - degrade gracefully to a plain copy rather than failing
        // the whole import outright.
        return ImportAsPlainCopy(
            sourcePath, preferredDestinationPath, "Could not decode as an image, imported as-is instead. ");
    }

    return ImportAsPlainCopy(sourcePath, preferredDestinationPath, std::string());
}

} // namespace gte
