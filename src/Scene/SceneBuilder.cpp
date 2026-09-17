#include "SceneBuilder.h"

#include "../ECS/Components/MeshAssetSource.h"
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
// `outDocument.entities` - PHASE4's own asset_guid resolution pass (below)
// reuses it.
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
    SceneDocument document;
    std::vector<Entity> entityOrder; // index-aligned with document.entities.
    for (const Entity root : GetChildren(registry, kInvalidEntity)) {
        WalkEntityRecursive(registry, root, std::nullopt, entityOrder, document);
    }

    // PHASE4 (task_manager/scene-serialization-2/
    // PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md, section
    // 3.1) - the asset_guid resolution pass PHASE3 deferred. Runs AFTER the
    // recursive walk above finishes, so entityOrder/document are both fully
    // built and index-aligned.
    for (std::size_t i = 0; i < entityOrder.size(); ++i) {
        if (const MeshAssetSource* meshAssetSource = registry.TryGetComponent<MeshAssetSource>(entityOrder[i]);
            meshAssetSource != nullptr) {
            if (const AssetRecord* asset = assetDatabase.FindByPath(meshAssetSource->gtaPath); asset != nullptr) {
                document.entities[i].assetGuid = asset->guid.ToString();
            }
            // else: not (or no longer) a tracked asset - leave assetGuid
            // empty, exactly like scene-serialization-1's own original
            // behavior - except this campaign does NOT skip/omit the
            // entity's own record entirely (the old behavior) - it is
            // still saved, generically, with whatever Transform/Name it
            // has; it will simply come back on Load as a bare entity with
            // no re-derived mesh, since Load has no Guid to resolve. This
            // is a deliberate, small improvement over
            // scene-serialization-1's old "skip the whole entity
            // silently" - now at least ITS TRANSFORM/NAME still round-trips,
            // even if its mesh can't be rebuilt.
        }
    }

    return document;
}

void ClearEntireScene(Registry& registry)
{
    // Snapshot roots BEFORE destroying anything - GetChildren(kInvalidEntity)
    // reads live Transform data that DestroyEntityAndDescendants() below
    // mutates as it goes.
    const std::vector<Entity> roots = GetChildren(registry, kInvalidEntity);
    for (const Entity root : roots) {
        DestroyEntityAndDescendants(registry, root);
    }
}

} // namespace gte
