#include "SceneBuilder.h"

#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/HierarchySnapshot.h"
#include "../ECS/Reflection/ComponentTypeRegistry.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "../Renderer/Renderer.h"

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace gte {

namespace {

// One record for `entity`: sibling order plus every registered component the entity has, so new
// components become part of a saved scene by registering themselves.
SceneEntityRecord CaptureRecord(Registry& registry, Entity entity, std::optional<std::size_t> parentIndex,
    const std::vector<ComponentTypeDescriptor>& descriptors)
{
    SceneEntityRecord record;
    record.parentIndex = parentIndex;
    if (const Transform* transform = registry.TryGetComponent<Transform>(entity); transform != nullptr) {
        record.siblingIndex = transform->siblingIndex;
    }

    for (const ComponentTypeDescriptor& descriptor : descriptors) {
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
    return record;
}

} // namespace

SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase)
{
    GTE_PROFILE_SCOPE("Scene.Save");

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    const std::vector<ComponentTypeDescriptor>& descriptors = ComponentTypeRegistry::Instance().AllSortedByTypeName();

    SceneDocument document;
    std::vector<Entity> entityOrder; // index-aligned with document.entities.

    // Iterative pre-order: a parent's record comes first, then its children in sibling order.
    struct Pending {
        Entity entity;
        std::optional<std::size_t> parentIndex;
    };
    std::vector<Pending> stack;
    const auto pushChildren = [&](Entity parent, std::optional<std::size_t> parentIndex) {
        const std::span<const Entity> children = snapshot.Children(parent);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push_back(Pending{ *it, parentIndex });
        }
    };

    pushChildren(kInvalidEntity, std::nullopt);
    while (!stack.empty()) {
        const Pending pending = stack.back();
        stack.pop_back();

        const std::size_t index = document.entities.size();
        entityOrder.push_back(pending.entity);
        document.entities.push_back(CaptureRecord(registry, pending.entity, pending.parentIndex, descriptors));
        pushChildren(pending.entity, index);
    }

    // Resolve asset guids once entityOrder and document are fully built and index-aligned.
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
    GTE_PROFILE_SCOPE("Scene.Destroy");
    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    const std::span<const Entity> roots = snapshot.Children(kInvalidEntity);
    DestroyEntitiesAndDescendants(registry, roots);
}

// Spawns the document's entities in passes: A creates, B1 wires parents and sibling order, B2 applies fields.
// File I/O, asset scanning and ClearEntireScene() stay with the caller.
void ReconstructSceneFromDocument(Game& game, Renderer& renderer, const SceneDocument& document,
    const AssetDatabase& assetDatabase, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    GTE_PROFILE_SCOPE("Scene.Load");
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
        const std::optional<std::size_t>& parentIndex = document.entities[i].parentIndex;
        if (parentIndex.has_value() && *parentIndex < entityCount) {
            childrenOf[*parentIndex].push_back(i);
        }
    }

    // Marks i and every saved descendant as decided, so Pass A never spawns stray bare entities
    // under an unresolvable asset root. Iterative; each record is walked at most once.
    std::vector<bool> subtreeMarked(entityCount, false);
    std::vector<std::size_t> markStack;
    const auto markSubtreeUnresolved = [&](std::size_t root) {
        if (subtreeMarked[root]) {
            consumedByRecipe[root] = true;
            return;
        }
        subtreeMarked[root] = true;
        markStack.push_back(root);
        while (!markStack.empty()) {
            const std::size_t i = markStack.back();
            markStack.pop_back();
            consumedByRecipe[i] = true;
            for (const std::size_t childIndex : childrenOf[i]) {
                if (!subtreeMarked[childIndex]) {
                    subtreeMarked[childIndex] = true;
                    markStack.push_back(childIndex);
                }
            }
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
            // The root record itself is not marked as parented: it still gets its saved parent in Pass B1.

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
                    alreadyParentedByRecipe[j] = true; // Only on an actual match.
                }
            }
            continue;
        }

        // Plain entity (Camera, Light, empty node, orphaned child). Gets a default Transform up front
        // so it can take part in the hierarchy.
        resultEntities[i] = registry.CreateEntity();
        registry.AddComponent<Transform>(resultEntities[i]);
    }

    // PASS B1 (hierarchy): parents and sibling order in one bulk call, before Pass B2 writes field values.
    // Matched asset-root children keep the parent the recipe gave them.
    std::vector<HierarchyAssignment> assignments;
    assignments.reserve(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i) {
        const Entity entity = resultEntities[i];
        if (entity == kInvalidEntity) {
            continue;
        }
        const SceneEntityRecord& record = document.entities[i];
        Entity parent = kInvalidEntity;
        if (alreadyParentedByRecipe[i]) {
            if (const Transform* transform = registry.TryGetComponent<Transform>(entity)) {
                parent = EffectiveParent(registry, *transform);
            }
        } else if (record.parentIndex.has_value() && *record.parentIndex < entityCount) {
            parent = resultEntities[*record.parentIndex];
        }
        assignments.push_back(HierarchyAssignment{ entity, parent, record.siblingIndex });
    }
    AssignHierarchy(registry, assignments);

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

    // Guarantee a Camera exists right after load, not only on the next Game::Render().
    // Idempotent when the document already had one.
    game.EnsureDefaultCameraExists();
}

} // namespace gte
