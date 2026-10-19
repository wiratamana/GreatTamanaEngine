#include "SponzaSceneOpener.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Assets/AssetImporter.h"
#include "../Assets/GtaFile.h"
#include "../Assets/MeshFile.h"
#include "../Core/Logging.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/DirectionalLight.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"

#include <algorithm>
#include <filesystem>
#include <optional>
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

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

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
{
    const std::filesystem::path sponzaGltf
        = std::filesystem::path(std::string(kSponzaDirectory)) / "glTF" / "Sponza.gltf";
    std::error_code ec;
    if (kSponzaDirectory.empty() || !std::filesystem::is_regular_file(sponzaGltf, ec)) {
        return Fail("Sponza.gltf not found. Configure with GTE_FETCH_SPONZA=ON (see cmake/FetchSponza.cmake).");
    }

    const std::filesystem::path sponzaDirectory = ResolveProjectRootDirectory() / kRootName;
    const std::filesystem::path gtaPath = sponzaDirectory / "Sponza.gta";

    OpenSponzaSceneResult result;
    result.gtaPath = PathToUtf8(gtaPath);

    // Textures are written before the mesh *.gta, so an existing mesh *.gta is a complete import.
    if (!std::filesystem::is_regular_file(gtaPath, ec)) {
        GTE_LOG_INFO(kLogCategory, "First open: importing Sponza (blocks the main thread)...");
        AssetDatabase database;
        const AssetImportResult imported = ImportAssetFile(database, sponzaGltf, sponzaDirectory / "Sponza.gltf");
        if (!imported.success || !imported.convertedToMeshAsset) {
            return Fail("Sponza import failed: " + imported.message);
        }
        GTE_LOG_INFO(kLogCategory, imported.message);
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

} // namespace gte
