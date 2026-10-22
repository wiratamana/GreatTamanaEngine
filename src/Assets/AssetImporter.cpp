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
#include <cstdio>

namespace gte {

namespace {

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Imports each texture a model's materials reference as its own Texture *.gta in "<meshStem>_Textures"; stores the Guids in `materials`.
// Returns the number of textures that could not be imported (they keep Guid::Invalid()); never fails the import.
std::size_t ImportMaterialTextures(
    AssetDatabase& database, MaterialData& materials, const std::filesystem::path& meshGtaPath)
{
    const std::filesystem::path texturesDirectory
        = meshGtaPath.parent_path() / (PathToUtf8(meshGtaPath.stem()) + "_Textures");

    std::size_t missingCount = 0;
    for (std::size_t i = 0; i < materials.textures.size(); ++i) {
        MaterialTextureRef& textureRef = materials.textures[i];
        if (textureRef.sourcePath.empty()) {
            continue; // No texture in this slot at all - nothing to import.
        }

        const std::filesystem::path sourcePath = PathFromUtf8(textureRef.sourcePath);

        std::error_code existsEc;
        if (!std::filesystem::is_regular_file(sourcePath, existsEc) || existsEc) {
            ++missingCount;
            continue;
        }

        const std::optional<Ktx2EncodeResult> encoded = EncodeImageFileToKtx2(sourcePath);
        if (!encoded.has_value()) {
            ++missingCount;
            continue;
        }

        // The index prefix keeps same-named textures from different folders apart.
        const std::filesystem::path textureGtaPath
            = texturesDirectory / (std::to_string(i) + "_" + PathToUtf8(sourcePath.stem()) + ".gta");

        const std::optional<Guid> guid
            = database.ImportAsset(textureGtaPath, AssetType::Texture, std::vector<std::uint8_t>{}, encoded->ktx2Bytes);
        if (guid.has_value()) {
            textureRef.guid = *guid;
        } else {
            ++missingCount;
        }
    }
    return missingCount;
}

struct ClusterImportOutcome {
    std::size_t clusterCount = 0;
    double milliseconds = 0.0;
    std::string message; // Empty when the step did not apply.
};

// Names why a rig blocks clustering (welding reorders vertices that rig data indexes), or nullptr.
const char* RigClusterBlocker(const MeshData& mesh, const RigFileData& rig) noexcept
{
    if (!mesh.skinWeights.empty() || !rig.skinWeights.empty()) {
        return "mesh has skin weights";
    }
    if (!rig.skeleton.bones.empty()) {
        return "mesh has a skeleton";
    }
    if (!rig.morphs.morphs.empty()) {
        return "mesh has morphs";
    }
    if (!rig.physics.rigidBodies.empty() || !rig.physics.joints.empty()) {
        return "mesh has physics data";
    }
    return nullptr;
}

bool AnyMaterialDrawsTextured(const std::vector<Material>& materials) noexcept
{
    return std::any_of(materials.begin(), materials.end(), MaterialDrawsTextured);
}

std::string FormatMilliseconds(double milliseconds)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f ms", milliseconds);
    return buffer;
}

// Runs the optional cluster step. Never fails the import: on any problem `mesh` is left valid and unclustered.
ClusterImportOutcome RunClusterStep(MeshData& mesh, const RigFileData& rig, const MeshImportOptions& options)
{
    ClusterImportOutcome outcome;
    if (options.clusters == ClusterBuildMode::Off) {
        return outcome;
    }
    const bool automatic = options.clusters == ClusterBuildMode::Auto;

    if (const char* blocker = RigClusterBlocker(mesh, rig); blocker != nullptr) {
        if (!automatic) {
            outcome.message = std::string("Clusters skipped: ") + blocker + ".";
        }
        return outcome;
    }
    if (automatic) {
        if (mesh.indices.size() / 3 < options.autoTriangleThreshold) {
            return outcome;
        }
        if (AnyMaterialDrawsTextured(rig.materials.materials)) {
            outcome.message = "Clusters skipped: a material is textured or uses PBR shading (use mode Always).";
            return outcome;
        }
    }

    const MeshClusterReport report
        = BuildMeshClusters(mesh, rig.materials.materials, options.cluster, nullptr, automatic);
    outcome.milliseconds = report.milliseconds;

    if (!report.success) {
        // A poor-fill early out is a normal Auto outcome, not an error worth a warning.
        outcome.message = "Clusters skipped: " + report.message + " (" + FormatMilliseconds(report.milliseconds) + ").";
        return outcome;
    }
    if (automatic && report.poorFill) {
        mesh.clusters.clear(); // Keep the welded mesh, drop the table that is worse than none.
        outcome.message = "Clusters skipped: poor cluster fill (" + FormatMilliseconds(report.milliseconds) + ").";
        return outcome;
    }

    char stats[96];
    std::snprintf(stats, sizeof(stats), "avg %.1f tris, vertices %zu -> %zu", report.avgTrianglesPerCluster,
        report.vertexCountBefore, report.vertexCountAfter);
    outcome.clusterCount = report.clusterCount;
    outcome.message = "Clusters: " + std::to_string(report.clusterCount) + ", " + stats + ", "
        + FormatMilliseconds(report.milliseconds) + ".";
    if (report.milliseconds > 10000.0) {
        outcome.message += " Warning: clustering took more than 10 s.";
    }
    if (report.poorFill) {
        outcome.message += " Warning: poor cluster fill, try the position-only weld.";
    }
    return outcome;
}

// Shared tail of a successful mesh parse: runs the cluster step, keeps the destination *.gta's saved joint
// physics overrides across a re-import, imports the material textures, writes the
// Mesh *.gta and builds the result. `mesh` and `rig` are consumed.
AssetImportResult FinalizeMeshAssetImport(AssetDatabase& database, MeshData mesh, RigFileData rig,
    const std::filesystem::path& sourcePath, const std::filesystem::path& preferredDestinationPath,
    MeshSourceFormat sourceFormat, const std::string& sourceFormatLabel, const MeshImportOptions& options)
{
    std::filesystem::path gtaPath = preferredDestinationPath;
    gtaPath.replace_extension(".gta");

    const ClusterImportOutcome clusterOutcome = RunClusterStep(mesh, rig, options);

    // Keep the saved joint physics overrides of an existing *.gta (metadata only, no payload read).
    std::vector<JointPhysicsOverride> preservedJointPhysicsOverrides;
    if (const std::optional<GtaMetadataData> existingGta = ReadGtaMetadataOnly(gtaPath);
        existingGta.has_value() && existingGta->header.Type() == AssetType::Mesh) {
        if (const std::optional<RigFileData> existingRig = DecodeRigDataFromBytes(existingGta->metadata);
            existingRig.has_value()) {
            preservedJointPhysicsOverrides = existingRig->jointPhysicsOverrides;
        }
    }
    rig.jointPhysicsOverrides = std::move(preservedJointPhysicsOverrides);

    // Encode first so an inconsistent mesh imports nothing, not even textures.
    std::vector<std::uint8_t> payload = EncodeMeshDataToBytes(mesh);
    std::size_t writtenClusterCount = clusterOutcome.clusterCount;
    std::string clusterMessage = clusterOutcome.message;
    if (payload.empty() && !mesh.clusters.empty()) {
        // Clustering must never fail an import: retry without the table.
        mesh.clusters.clear();
        writtenClusterCount = 0;
        clusterMessage = "Clusters skipped: the cluster table could not be encoded.";
        payload = EncodeMeshDataToBytes(mesh);
    }
    if (payload.empty()) {
        AssetImportResult failed;
        failed.success = false;
        failed.message = "Parsed \"" + PathToUtf8(sourcePath.filename()) + "\" but its mesh arrays are inconsistent; nothing was written.";
        return failed;
    }

    // Must run before the rig is encoded so the metadata carries Guids, not source paths.
    const std::size_t missingTextureCount = ImportMaterialTextures(database, rig.materials, gtaPath);

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
        result.missingTextureCount = missingTextureCount;
        result.clusterCount = writtenClusterCount;
        result.clusterMessage = std::move(clusterMessage);
        result.clusterMilliseconds = clusterOutcome.milliseconds;
        result.message = "Imported \"" + PathToUtf8(sourcePath.filename()) + "\" as a " + sourceFormatLabel + " mesh ("
            + std::to_string(result.meshVertexCount) + " vertices, " + std::to_string(result.meshTriangleCount)
            + " triangles, " + std::to_string(result.boneCount) + " bones, " + std::to_string(result.morphCount)
            + " morphs, " + std::to_string(result.rigidBodyCount) + " rigid bodies, "
            + std::to_string(result.jointCount) + " joints, " + std::to_string(result.materialCount)
            + " materials, " + std::to_string(result.textureCount) + " textures, "
            + std::to_string(result.missingTextureCount) + " missing) -> \"" + PathToUtf8(gtaPath.filename()) + "\".";
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

AssetImportResult ImportAssetFile(AssetDatabase& database, const std::filesystem::path& sourcePath,
    const std::filesystem::path& preferredDestinationPath, const MeshImportOptions& options)
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
                    preferredDestinationPath, MeshSourceFormat::Pmx, "PMX", options);
            }
            return ImportAsPlainCopy(
                sourcePath, preferredDestinationPath, "Could not parse as a mesh, imported as-is instead. ");
        }

        if (extension == ".stl") {
            StlLoadResult loaded = LoadStlModel(PathToUtf8(sourcePath));
            if (loaded.success) {
                RigFileData rig; // An STL has no skeleton, morphs, physics or materials.
                return FinalizeMeshAssetImport(database, std::move(loaded.mesh), std::move(rig), sourcePath,
                    preferredDestinationPath, MeshSourceFormat::Stl, "STL", options);
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
                    sourcePath, preferredDestinationPath, MeshSourceFormat::Gltf, "glTF", options);
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
                result.motionModeKeyframeCount = loaded.motion.ModeKeyframeCount();
                result.motionIkKeyframeCount = loaded.motion.ikKeyframes.size();
                result.message = "Imported \"" + PathToUtf8(sourcePath.filename()) + "\" as a motion ("
                    + std::to_string(result.motionBoneKeyframeCount) + " bone keyframes, "
                    + std::to_string(result.motionMorphKeyframeCount) + " morph keyframes, "
                    + std::to_string(result.motionCameraKeyframeCount) + " camera keyframes, "
                    + std::to_string(result.motionLightKeyframeCount) + " light keyframes, "
                    + std::to_string(result.motionModeKeyframeCount) + " mode keyframes, "
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
