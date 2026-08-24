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

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
        const PmxLoadResult loaded = LoadPmxModel(PathToUtf8(sourcePath));
        if (loaded.success) {
            std::filesystem::path gtaPath = preferredDestinationPath;
            gtaPath.replace_extension(".gta");

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
