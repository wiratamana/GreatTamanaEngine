#include "SponzaSceneOpener.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Assets/AssetImporter.h"
#include "../Assets/GltfLoader.h"
#include "../Assets/GtaFile.h"
#include "../Assets/ImportStamp.h"
#include "../Assets/MeshFile.h"
#include "../Encoding/Utf8Path.h"
#include "../Core/Logging.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/DirectionalLight.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

namespace {

#ifdef GTE_SPONZA_DIR
constexpr std::string_view kSponzaDirectory = GTE_SPONZA_DIR;
#else
constexpr std::string_view kSponzaDirectory;
#endif

constexpr const char* kLogCategory = "SponzaScene";
constexpr const char* kRootName = "Sponza";
constexpr float kEyeHeight = 1.7f;

// Camera start: bounds center on X/Z, eye height above the lowest point.
// Read back from the *.gta so it works for fresh and cached imports.
Vec3 ReadCameraStartPosition(const std::filesystem::path& gtaPath)
{
    Vec3 fallback{ 0.0f, kEyeHeight, 0.0f };
    const std::optional<GtaFileData> gta = ReadGtaFile(gtaPath);
    if (!gta.has_value()) {
        return fallback;
    }
    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty()) {
        return fallback;
    }
    Vec3 lo = mesh->positions[0];
    Vec3 hi = lo;
    for (const Vec3& p : mesh->positions) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    return Vec3{ 0.5f * (lo.x + hi.x), lo.y + kEyeHeight, 0.5f * (lo.z + hi.z) };
}

OpenSponzaSceneResult Fail(std::string message)
{
    GTE_LOG_ERROR(kLogCategory, message);
    OpenSponzaSceneResult result;
    result.message = std::move(message);
    return result;
}

} // namespace

OpenSponzaSceneResult OpenSponzaScene(
    Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
try
{
    const std::filesystem::path sponzaGltf
        = std::filesystem::path(std::string(kSponzaDirectory)) / "glTF" / "Sponza.gltf";
    std::error_code ec;
    if (kSponzaDirectory.empty() || !std::filesystem::is_regular_file(sponzaGltf, ec)) {
        return Fail("Sponza.gltf not found. Configure with GTE_FETCH_SPONZA=ON (see cmake/FetchSponza.cmake).");
    }

    const std::filesystem::path sponzaDirectory = ResolveProjectRootDirectory() / kRootName;
    const std::filesystem::path gtaPath = sponzaDirectory / "Sponza.gta";
    const std::filesystem::path texturesDirectory = sponzaDirectory / "Sponza_Textures";
    const std::filesystem::path stampPath = sponzaDirectory / "Sponza.import.stamp";

    OpenSponzaSceneResult result;
    result.gtaPath = PathToUtf8(gtaPath);

    // The import is reused only when the mesh exists and its stamp (written last) matches this importer and source.
    const std::uintmax_t sourceBytes = std::filesystem::file_size(sponzaGltf, ec);
    if (ec) {
        return Fail("Cannot read the size of Sponza.gltf: " + ec.message());
    }
    const bool complete = std::filesystem::is_regular_file(gtaPath, ec)
        && IsImportStampCurrent(ReadImportStamp(stampPath), kGltfImporterVersion, sourceBytes);

    if (!complete) {
        GTE_LOG_INFO(kLogCategory, "Importing Sponza (blocks the main thread)...");
        // Only the importer's own outputs are removed, and only when they lie inside the Sponza folder.
        const bool hadOldImport = std::filesystem::exists(gtaPath, ec);
        for (const std::filesystem::path& stale : { stampPath, gtaPath, texturesDirectory }) {
            if (!IsPathStrictlyInsideDirectory(sponzaDirectory, stale)) {
                return Fail("Refusing to remove \"" + PathToUtf8(stale) + "\": outside the Sponza folder.");
            }
            std::filesystem::remove_all(stale, ec);
        }

        AssetDatabase database;
        const AssetImportResult imported = ImportAssetFile(database, sponzaGltf, sponzaDirectory / "Sponza.gltf");
        if (!imported.success || !imported.convertedToMeshAsset) {
            return Fail("Sponza import failed: " + imported.message);
        }
        if (imported.missingTextureCount > 0) {
            return Fail("Sponza import incomplete: missing textures: " + std::to_string(imported.missingTextureCount)
                + " of " + std::to_string(imported.textureCount) + " (broken download?).");
        }
        const ImportStamp stamp{ .importerVersion = kGltfImporterVersion,
            .sourceBytes = sourceBytes,
            .materialCount = static_cast<std::uint32_t>(imported.materialCount),
            .textureCount = static_cast<std::uint32_t>(imported.textureCount) };
        if (!WriteImportStamp(stampPath, stamp)) {
            GTE_LOG_WARNING(kLogCategory, "Could not write the import stamp; Sponza is imported again on the next open.");
        }
        GTE_LOG_INFO(kLogCategory, imported.message);
        if (hadOldImport) {
            GTE_LOG_INFO(kLogCategory, "Re-imported a stale Sponza; GPU textures of the old import stay until exit.");
        }
        result.importedThisCall = true;
        game.GetMeshInstantiationSystem().InvalidateCachedMeshAsset(result.gtaPath);
    }

    // Spawn first and delete the old scene only on success.
    Registry& registry = game.GetRegistry();
    const std::vector<Entity> oldRoots = GetChildren(registry, kInvalidEntity);
    const Entity root = game.CreateMeshEntityFromGtaFile(renderer, result.gtaPath, sceneServicesSetLayout);
    if (root == kInvalidEntity) {
        return Fail("Sponza spawn failed: could not load \"" + result.gtaPath + "\".");
    }
    for (const Entity oldRoot : oldRoots) {
        DestroyEntityAndDescendants(registry, oldRoot);
    }
    // The spawn may have de-duplicated the name against the old scene; restore it.
    if (Name* name = registry.TryGetComponent<Name>(root)) {
        name->value = kRootName;
    }

    // The sun and the cascaded shadows follow the active DirectionalLight entity.
    if (registry.Storage<DirectionalLight>().Size() == 0) {
        game.CreateDirectionalLightEntity();
    }

    // Named camera so an AI can move it with POST /set_entity_trs; identity rotation looks +Z.
    if (registry.Storage<Camera>().Size() == 0) {
        const Entity camera = registry.CreateEntity();
        registry.AddComponent<Transform>(camera).position = ReadCameraStartPosition(gtaPath);
        registry.AddComponent<Camera>(camera);
        registry.AddComponent<Name>(camera, Name{ "Main Camera" });
    }

    result.success = true;
    result.message = result.importedThisCall ? "Imported and opened the Sponza scene." : "Opened the Sponza scene.";
    GTE_LOG_INFO(kLogCategory, "OpenSponzaScene: done");
    return result;
}
catch (const std::exception& e)
{
    // GPU upload and file errors surface as exceptions; the menu and the HTTP route must get a failure instead.
    return Fail(std::string("Sponza open failed: ") + e.what());
}

} // namespace gte
