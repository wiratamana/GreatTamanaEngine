#include "SceneIO.h"

#include "ProjectRootPath.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Registry.h"
#include "../Assets/AssetDatabase.h"
#include "../Game/Game.h"
#include "../Scene/SceneBuilder.h"
#include "../Scene/SceneTextFormat.h"

#include <fstream>
#include <sstream>

namespace gte {

std::filesystem::path DefaultScenePath()
{
    return ResolveProjectRootDirectory() / "TestScene.gtscene";
}

bool SaveScene(Game& game)
{
    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();

    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot); // Safe even if projectRoot doesn't exist yet - returns 0.

    const SceneDocument document = BuildSceneDocumentFromRegistry(game.GetRegistry(), assetDatabase);
    const std::string text = SerializeSceneDocument(document);

    const std::filesystem::path scenePath = projectRoot / "TestScene.gtscene";

    std::error_code ec;
    std::filesystem::create_directories(scenePath.parent_path(), ec);
    // Deliberately not checked/aborted-on: if scenePath.parent_path() already
    // exists, create_directories() reports an ec that std::ofstream below
    // will simply succeed past anyway - matching WriteGtaFile()'s own
    // tolerant convention.

    std::ofstream file(scenePath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

bool LoadScene(Game& game, Renderer& renderer)
{
    const std::filesystem::path scenePath = DefaultScenePath();

    std::ifstream file(scenePath, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    const std::optional<SceneDocument> document = DeserializeSceneDocument(buffer.str());
    if (!document.has_value()) {
        return false; // Malformed file - do NOT touch the current scene at all.
    }

    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();
    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot);

    ClearSerializableSceneObjects(game.GetRegistry());

    for (const SceneObjectRecord& record : document->objects) {
        Entity spawned = kInvalidEntity;
        if (record.kind == SceneObjectKind::Primitive) {
            spawned = game.CreatePrimitiveEntity(renderer, record.primitiveType);
        } else {
            const AssetRecord* asset = assetDatabase.FindByGuid(record.assetGuid);
            if (asset == nullptr) {
                continue; // Asset moved/deleted since last Save - skip this one object gracefully.
            }
            spawned = game.CreateMeshEntityFromGtaFile(renderer, asset->gtaPath);
        }
        if (spawned == kInvalidEntity) {
            continue;
        }

        if (Transform* transform = game.GetRegistry().TryGetComponent<Transform>(spawned); transform != nullptr) {
            transform->position = record.position;
            transform->rotation = record.rotation;
            transform->scale = record.scale;
        }
        if (!record.name.empty()) {
            game.GetRegistry().AddComponent<Name>(spawned, Name{ record.name });
        }
    }

    return true;
}

} // namespace gte
