#include "AssetImporter.h"

#include "../Encoding/Utf8Path.h"
#include "Ktx2Encoder.h"
#include "MeshFile.h"
#include "MotionFile.h"
#include "GltfLoader.h"
#include "PmxLoader.h"
#include "RigFile.h"
#include "StlLoader.h"
#include "VmdLoader.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace gte {

namespace {

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Imports every resolvable texture a model's materials reference as its own Texture *.gta,
// in a "<meshStem>_Textures" folder next to the Mesh *.gta, and stores each Guid back into
// `materials`. A missing or undecodable source keeps Guid::Invalid(); it never fails the import.
void ImportMaterialTextures(
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

        const std::filesystem::path sourcePath = PathFromUtf8(textureRef.sourcePath);

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

// Shared tail of a successful mesh parse: keeps the destination *.gta's saved joint
// physics overrides across a re-import, imports the material textures, writes the
// Mesh *.gta and builds the result. `mesh` and `rig` are consumed.
AssetImportResult FinalizeMeshAssetImport(AssetDatabase& database, MeshData mesh, RigFileData rig,
    const std::filesystem::path& sourcePath, const std::filesystem::path& preferredDestinationPath,
    MeshSourceFormat sourceFormat, const std::string& sourceFormatLabel)
{
    std::filesystem::path gtaPath = preferredDestinationPath;
    gtaPath.replace_extension(".gta");

    // Read the existing *.gta before ImportAsset() overwrites it.
    std::vector<JointPhysicsOverride> preservedJointPhysicsOverrides;
    if (const std::optional<GtaFileData> existingGta = ReadGtaFile(gtaPath);
        existingGta.has_value() && existingGta->header.Type() == AssetType::Mesh) {
        if (const std::optional<RigFileData> existingRig = DecodeRigDataFromBytes(existingGta->metadata);
            existingRig.has_value()) {
            preservedJointPhysicsOverrides = existingRig->jointPhysicsOverrides;
        }
    }
    rig.jointPhysicsOverrides = std::move(preservedJointPhysicsOverrides);

    // Must run before the rig is encoded so the metadata carries Guids, not source paths.
    ImportMaterialTextures(database, rig.materials, gtaPath);

    const std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(mesh);
    const std::vector<std::uint8_t> metadata = EncodeRigDataToBytes(rig);

    const std::optional<Guid> guid = database.ImportAsset(gtaPath, AssetType::Mesh, metadata, payload);

    AssetImportResult result;
    if (guid.has_value()) {
        result.success = true;
        result.convertedToMeshAsset = true;
        result.meshSourceFormat = sourceFormat;
        result.finalPath = gtaPath;
        result.guid = *guid;
        result.meshVertexCount = mesh.positions.size();
        result.meshTriangleCount = mesh.indices.size() / 3;
        result.skinnedVertexCount = rig.skinWeights.size();
        result.boneCount = rig.skeleton.bones.size();
        result.morphCount = rig.morphs.morphs.size();
        result.rigidBodyCount = rig.physics.rigidBodies.size();
        result.jointCount = rig.physics.joints.size();
        result.materialCount = rig.materials.materials.size();
        result.textureCount = rig.materials.textures.size();
        result.message = "Imported \"" + PathToUtf8(sourcePath.filename()) + "\" as a " + sourceFormatLabel + " mesh ("
            + std::to_string(result.meshVertexCount) + " vertices, " + std::to_string(result.meshTriangleCount)
            + " triangles, " + std::to_string(result.boneCount) + " bones, " + std::to_string(result.morphCount)
            + " morphs, " + std::to_string(result.rigidBodyCount) + " rigid bodies, "
            + std::to_string(result.jointCount) + " joints, " + std::to_string(result.materialCount)
            + " materials, " + std::to_string(result.textureCount) + " textures) -> \""
            + PathToUtf8(gtaPath.filename()) + "\".";
    } else {
        result.success = false;
        result.message = "Parsed \"" + PathToUtf8(sourcePath.filename()) + "\" but failed to write its *.gta wrapper.";
    }
    return result;
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
    return extensionLowercaseWithDot == ".pmx" || extensionLowercaseWithDot == ".stl" || extensionLowercaseWithDot == ".gltf";
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
        if (extension == ".pmx") {
            PmxLoadResult loaded = LoadPmxModel(PathToUtf8(sourcePath));
            if (loaded.success) {
                RigFileData rig;
                rig.skinWeights = loaded.mesh.skinWeights;
                rig.skeleton = loaded.skeleton;
                rig.morphs = loaded.morphs;
                rig.physics = loaded.physics;
                rig.materials = loaded.materials;
                return FinalizeMeshAssetImport(database, std::move(loaded.mesh), std::move(rig), sourcePath,
                    preferredDestinationPath, MeshSourceFormat::Pmx, "PMX");
            }
            return ImportAsPlainCopy(
                sourcePath, preferredDestinationPath, "Could not parse as a mesh, imported as-is instead. ");
        }

        if (extension == ".stl") {
            StlLoadResult loaded = LoadStlModel(PathToUtf8(sourcePath));
            if (loaded.success) {
                RigFileData rig; // An STL has no skeleton, morphs, physics or materials.
                return FinalizeMeshAssetImport(database, std::move(loaded.mesh), std::move(rig), sourcePath,
                    preferredDestinationPath, MeshSourceFormat::Stl, "STL");
            }
            return ImportAsPlainCopy(
                sourcePath, preferredDestinationPath, "Could not parse as a mesh, imported as-is instead. ");
        }

        if (extension == ".gltf") {
            GltfLoadResult loaded = LoadGltfModel(PathToUtf8(sourcePath));
            if (loaded.success) {
                RigFileData rig; // No skeleton, morphs or physics.
                rig.materials = std::move(loaded.materials);
                AssetImportResult imported = FinalizeMeshAssetImport(database, std::move(loaded.mesh), std::move(rig),
                    sourcePath, preferredDestinationPath, MeshSourceFormat::Gltf, "glTF");
                if (imported.success) {
                    imported.message += " " + loaded.message; // Adds the UV range and skipped-primitive count.
                }
                return imported;
            }
            // A lone .gltf copied as-is is useless (no .bin, no images): fail loudly.
            AssetImportResult failed;
            failed.message = "Could not import \"" + PathToUtf8(sourcePath.filename()) + "\": " + loaded.message;
            return failed;
        }
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
