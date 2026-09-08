#include "SceneBuilder.h"

#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/PrimitiveSource.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"

namespace gte {

SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase)
{
    SceneDocument document;

    for (const Entity entity : GetChildren(registry, kInvalidEntity)) {
        const Transform* transform = registry.TryGetComponent<Transform>(entity);
        if (transform == nullptr) {
            continue; // Should never happen in practice - every Instantiate()'d entity gets one.
        }

        SceneObjectRecord record;
        record.position = transform->position;
        record.rotation = transform->rotation;
        record.scale = transform->scale;
        if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr) {
            record.name = name->value;
        }

        if (const PrimitiveSource* primitiveSource = registry.TryGetComponent<PrimitiveSource>(entity);
            primitiveSource != nullptr) {
            record.kind = SceneObjectKind::Primitive;
            record.primitiveType = primitiveSource->type;
            document.objects.push_back(record);
        } else if (const MeshAssetSource* meshAssetSource = registry.TryGetComponent<MeshAssetSource>(entity);
            meshAssetSource != nullptr) {
            const AssetRecord* asset = assetDatabase.FindByPath(meshAssetSource->gtaPath);
            if (asset == nullptr) {
                continue; // Not (or no longer) a tracked asset - no stable Guid to serialize by.
            }
            record.kind = SceneObjectKind::Asset;
            record.assetGuid = asset->guid;
            document.objects.push_back(record);
        }
        // else: neither tag present (e.g. the default Camera entity) - not
        // part of this feature's serialization scope, skipped.
    }

    return document;
}

void ClearSerializableSceneObjects(Registry& registry)
{
    // Snapshot roots BEFORE destroying anything - GetChildren(kInvalidEntity)
    // reads live Transform data that DestroyEntityAndDescendants() below
    // mutates as it goes.
    const std::vector<Entity> roots = GetChildren(registry, kInvalidEntity);
    for (const Entity root : roots) {
        const bool isPrimitive = registry.HasComponent<PrimitiveSource>(root);
        const bool isAsset = registry.HasComponent<MeshAssetSource>(root);
        if (isPrimitive || isAsset) {
            DestroyEntityAndDescendants(registry, root);
        }
    }
}

} // namespace gte
