#include "SceneBuilder.h"

#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Reflection/ComponentTypeRegistry.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"
#include "../Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "../Renderer/Renderer.h"

#include <functional>
#include <utility>

namespace gte {

namespace {

// Pre-order walk: appends `entity`'s record, then recurses into its children, so a parent's index is
// known when each child captures it. `outEntityOrder` is index-aligned with `outDocument.entities`.
void WalkEntityRecursive(Registry& registry, Entity entity, std::optional<std::size_t> parentIndex,
    std::vector<Entity>& outEntityOrder, SceneDocument& outDocument)
{
    const std::size_t myIndex = outDocument.entities.size();

    SceneEntityRecord record;
    record.parentIndex = parentIndex;
    if (const Transform* transform = registry.TryGetComponent<Transform>(entity); transform != nullptr) {
        record.siblingIndex = transform->siblingIndex;
    }

    // Generic capture: serialize every registered component type the entity has, so new components
    // become part of a saved scene by registering themselves.
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

    // Resolve asset guids after the walk, when entityOrder and document are fully built and index-aligned.
    for (std::size_t i = 0; i < entityOrder.size(); ++i) {
        if (const MeshAssetSource* meshAssetSource = registry.TryGetComponent<MeshAssetSource>(entityOrder[i]);
            meshAssetSource != nullptr) {
            if (const AssetRecord* asset = assetDatabase.FindByPath(meshAssetSource->gtaPath); asset != nullptr) {
                document.entities[i].assetGuid = asset->guid.ToString();
            }
            // else: untracked asset - assetGuid stays empty; the record is still saved and reloads as a
            // bare entity with its Transform/Name but no rebuilt mesh.
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

// Spawns the document's entities in recipe-aware passes (A: create, B1: parent, B2: fields, B3: sibling order).
// File I/O, asset scanning and ClearEntireScene() stay with the caller.
void ReconstructSceneFromDocument(Game& game, Renderer& renderer, const SceneDocument& document,
    const AssetDatabase& assetDatabase, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    Registry& registry = game.GetRegistry();

    const std::size_t entityCount = document.entities.size();
    std::vector<Entity> resultEntities(entityCount, kInvalidEntity);

    // consumedByRecipe[i]: Pass A already decided this record (recipe-spawned, reconciled or dropped,
    // or unreachable under an unresolved asset root). Pass A skips it; Pass B2 skips re-applying "PrimitiveSource".
    std::vector<bool> consumedByRecipe(entityCount, false);

    // alreadyParentedByRecipe[i]: the live entity was already attached to its live parent by
    // CreateMeshEntityFromGtaFile(). Set only on matched saved children, never on the recipe root itself.
    std::vector<bool> alreadyParentedByRecipe(entityCount, false);

    // childrenOf[i] = indices j with parentIndex == i, in ascending (saved-sibling) order.
    std::vector<std::vector<std::size_t>> childrenOf(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (document.entities[i].parentIndex.has_value()) {
            childrenOf[*document.entities[i].parentIndex].push_back(i);
        }
    }

    // Marks i and every saved descendant as decided, so Pass A never spawns stray unparented bare
    // entities for the children of an unresolvable asset root.
    std::function<void(std::size_t)> markSubtreeUnresolved = [&](std::size_t i) {
        consumedByRecipe[i] = true;
        for (const std::size_t childIndex : childrenOf[i]) {
            markSubtreeUnresolved(childIndex);
        }
    };

    // PASS A (recipe-aware entity creation).
    for (std::size_t i = 0; i < entityCount; ++i) {
        // resultEntities[i] alone cannot tell "dropped on purpose" (unmatched reconciled child) from
        // "not visited yet"; consumedByRecipe[i] remembers that the record was already decided.
        if (resultEntities[i] != kInvalidEntity || consumedByRecipe[i]) {
            continue;
        }

        const SceneEntityRecord& record = document.entities[i];

        if (record.components.contains("PrimitiveSource")) {
            const nlohmann::json& primitiveSourceJson = record.components.at("PrimitiveSource");
            PrimitiveType parsedType{};
            if (primitiveSourceJson.contains("type") && primitiveSourceJson.at("type").is_string()
                && TryParsePrimitiveTypeName(primitiveSourceJson.at("type").get<std::string>(), parsedType)) {
                resultEntities[i] = game.CreatePrimitiveEntity(renderer, parsedType, sceneServicesSetLayout);
                consumedByRecipe[i] = true;
            }
            // else: unrecognized primitive type - leave unresolved (a bad item never aborts the load).
            // A primitive is a single node, so there is no subtree to mark.
            continue;
        }

        if (!record.assetGuid.empty()) {
            const Guid guid = Guid::Parse(record.assetGuid); // Never throws - returns Guid::Invalid() for anything malformed.
            const AssetRecord* asset = assetDatabase.FindByGuid(guid);
            if (asset == nullptr) {
                // Moved/deleted/malformed asset guid: skip this root and its whole saved subtree.
                markSubtreeUnresolved(i);
                continue;
            }

            const Entity rootEntity = game.CreateMeshEntityFromGtaFile(renderer, asset->gtaPath, sceneServicesSetLayout);
            resultEntities[i] = rootEntity;
            consumedByRecipe[i] = true;
            // alreadyParentedByRecipe[i] is DELIBERATELY NOT set here - see
            // Pass B1 below for why the root record itself must still be
            // free to run through the normal SetParent() step.

            // Reconciliation: match this record's saved children to the live children just created,
            // by Name, first-unused-match, in ascending sibling order (deterministic pairing).
            const std::vector<Entity> liveChildren = GetChildren(registry, rootEntity);
            std::vector<bool> usedLiveChild(liveChildren.size(), false);

            for (const std::size_t j : childrenOf[i]) {
                std::string savedChildName;
                if (const SceneEntityRecord& childRecord = document.entities[j]; childRecord.components.contains("Name")) {
                    const nlohmann::json& nameJson = childRecord.components.at("Name");
                    if (nameJson.contains("value") && nameJson.at("value").is_string()) {
                        savedChildName = nameJson.at("value").get<std::string>();
                    }
                }
                // An empty name (no Name block, or an empty value) is a valid, weak matching key:
                // unnamed saved children are paired positionally like any repeated name.

                Entity match = kInvalidEntity;
                for (std::size_t k = 0; k < liveChildren.size(); ++k) {
                    if (usedLiveChild[k]) {
                        continue;
                    }
                    // A live part may have no Name (unnamed material), so TryGetComponent is required.
                    std::string liveName;
                    if (const Name* namePtr = registry.TryGetComponent<Name>(liveChildren[k]); namePtr != nullptr) {
                        liveName = namePtr->value;
                    }
                    if (liveName == savedChildName) {
                        match = liveChildren[k];
                        usedLiveChild[k] = true;
                        break;
                    }
                }

                resultEntities[j] = match; // kInvalidEntity if no unused live child of that exact name (possibly "") is left.
                consumedByRecipe[j] = true; // Either way - a saved child of an asset root is NEVER independently re-created as a bare entity.
                if (match != kInvalidEntity) {
                    alreadyParentedByRecipe[j] = true; // ONLY on an actual match - see Pass B1.
                }
            }
            continue;
        }

        // Plain entity (Camera, Light, empty node, orphaned child). Gets a default Transform up front
        // so SetParent() in Pass B1 can succeed.
        resultEntities[i] = registry.CreateEntity();
        registry.AddComponent<Transform>(resultEntities[i]);
    }

    // PASS B1 (hierarchy wiring) runs before Transform values are applied: SetParent() with
    // worldPositionStays=true would recompute and clobber the local values Pass B2 writes.
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (resultEntities[i] == kInvalidEntity) {
            continue; // Skip anything left unresolved (an unmatched reconciled child, or anything markSubtreeUnresolved() touched).
        }
        if (alreadyParentedByRecipe[i]) {
            // A matched asset-root child is already attached by CreateMeshEntityFromGtaFile().
            // Not gated on consumedByRecipe: recipe roots always spawn top-level, so a saved parentIndex
            // must still go through SetParent() below.
            continue;
        }
        const SceneEntityRecord& record = document.entities[i];
        if (record.parentIndex.has_value()) {
            const Entity parentEntity = resultEntities[*record.parentIndex];
            if (parentEntity != kInvalidEntity) {
                SetParent(registry, resultEntities[i], parentEntity, /*worldPositionStays=*/false);
            }
            // Defensive: the saved parent never resolved (already skipped by the guard above for unresolved subtrees).
        }
    }

    // PASS B2 (generic field application): restores exact saved values (Transform, Name, Camera, ...),
    // including on recipe-spawned entities and asset roots.
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (resultEntities[i] == kInvalidEntity) {
            continue;
        }
        const SceneEntityRecord& record = document.entities[i];
        const Entity entity = resultEntities[i];
        for (auto it = record.components.begin(); it != record.components.end(); ++it) {
            const std::string& typeName = it.key();
            if (typeName == "PrimitiveSource" && consumedByRecipe[i]) {
                // Already correctly set by CreatePrimitiveEntity() itself -
                // reapplying is harmless/idempotent but unnecessary; skip it
                // for clarity. Every OTHER key still applies normally below.
                continue;
            }
            const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find(typeName);
            if (descriptor == nullptr) {
                continue; // Unknown component type - forward-compat, silently skipped.
            }
            descriptor->ensureDefaultComponent(registry, entity);
            void* component = descriptor->tryGetMutableComponent(registry, entity);
            for (const FieldDescriptor& field : descriptor->fields) {
                std::string errorMessage;
                if (!field.readJson(component, it.value(), errorMessage)) {
                    // A malformed field is not fatal: it keeps the default (or prior) value.
                }
            }
        }
    }

    // PASS B3 (sibling ordering). Re-applying a matched child's saved index is idempotent; a reparented
    // recipe root needs its saved index like any other record.
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (resultEntities[i] == kInvalidEntity) {
            continue;
        }
        SetSiblingIndex(registry, resultEntities[i], document.entities[i].siblingIndex);
    }

    // Guarantee a Camera exists right after load, not only on the next Game::Render().
    // Idempotent when the document already had one.
    game.EnsureDefaultCameraExists();
}

} // namespace gte
