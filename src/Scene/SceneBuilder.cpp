#include "SceneBuilder.h"

#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Components/PrimitiveSource.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Reflection/ComponentTypeRegistry.h"
#include "../ECS/TransformHierarchy.h"

#include <utility>

namespace gte {

namespace {

// Recursive pre-order walk - visits `entity`, appends its own
// SceneEntityRecord to `outDocument.entities`, THEN recurses into its
// children (via GetChildren()) so a parent's own array index is always
// already known (and thus can be captured by each child's own parentIndex)
// by the time any child is visited. `outEntityOrder` is index-aligned with
// `outDocument.entities` - PHASE4 reuses it for its own asset_guid
// resolution pass (see SceneBuilder.h's own doc comment).
void WalkEntityRecursive(Registry& registry, Entity entity, std::optional<std::size_t> parentIndex,
    std::vector<Entity>& outEntityOrder, SceneDocument& outDocument)
{
    const std::size_t myIndex = outDocument.entities.size();

    SceneEntityRecord record;
    record.parentIndex = parentIndex;
    if (const Transform* transform = registry.TryGetComponent<Transform>(entity); transform != nullptr) {
        record.siblingIndex = transform->siblingIndex;
    }

    // Generic component capture - THE key new piece of logic this whole
    // campaign is built around: walk EVERY registered component type, in
    // AllSortedByTypeName() order, and if this entity has it, serialize
    // it. No per-component-type branch anywhere in this function - a
    // future component becomes part of a saved scene automatically the
    // moment it registers itself (see PHASE2).
    for (const ComponentTypeDescriptor& descriptor : ComponentTypeRegistry::Instance().AllSortedByTypeName()) {
        const void* component = descriptor.tryGetConstComponent(registry, entity);
        if (component == nullptr) {
            continue;
        }
        nlohmann::json fields = nlohmann::json::object();
        for (const FieldDescriptor& field : descriptor.fields) {
            field.writeJson(component, fields);
        }
        record.components[descriptor.typeName] = std::move(fields);
    }

    outEntityOrder.push_back(entity);
    outDocument.entities.push_back(std::move(record));

    for (const Entity child : GetChildren(registry, entity)) {
        WalkEntityRecursive(registry, child, myIndex, outEntityOrder, outDocument);
    }
}

} // namespace

SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase)
{
    // Not yet used by this phase - PHASE4 inserts the MeshAssetSource ->
    // asset_guid resolution pass here, using `entityOrder`/`assetDatabase`
    // together with the already-built `document` below (see
    // SceneBuilder.h's own doc comment for why this parameter is still
    // accepted, unused, rather than removed and re-added later).
    (void)assetDatabase;

    SceneDocument document;
    std::vector<Entity> entityOrder; // index-aligned with document.entities - PHASE4 reuses this for asset_guid resolution.
    for (const Entity root : GetChildren(registry, kInvalidEntity)) {
        WalkEntityRecursive(registry, root, std::nullopt, entityOrder, document);
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
