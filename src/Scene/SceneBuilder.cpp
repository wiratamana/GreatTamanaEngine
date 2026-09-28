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

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE3 - see this function's own doc comment in
// SceneBuilder.h for the full extraction history. This body is a faithful,
// unmodified copy of Editor/SceneIO.cpp's own prior LoadScene() recipe-aware
// reconstruction logic (Pass A/B1/B2/B3 + the markSubtreeUnresolved lambda +
// the final EnsureDefaultCameraExists() call) - minus the file-I/O/
// AssetDatabase-scan/ClearEntireScene() responsibilities, which now stay
// with each of this function's two real callers (Editor::LoadScene(),
// Core/Plugins/ProjectAssemblyHotReload.cpp's own
// RestoreProjectAssemblyHotReloadState()). Do NOT "improve" this algorithm
// here - this is a pure code-motion refactor; fix bugs, if any are ever
// found, only as their own, separately-justified change.
void ReconstructSceneFromDocument(Game& game, Renderer& renderer, const SceneDocument& document, const AssetDatabase& assetDatabase)
{
    Registry& registry = game.GetRegistry();

    const std::size_t entityCount = document.entities.size();
    std::vector<Entity> resultEntities(entityCount, kInvalidEntity);

    // consumedByRecipe[i] means: "this record's own identity/creation was
    // already fully decided by Pass A - either spawned via a recipe
    // (PrimitiveSource/MeshAssetSource), OR reconciled (matched or
    // explicitly left unmatched) as an asset root's saved child, OR marked
    // unreachable because an ancestor's asset failed to resolve." Consulted
    // by Pass A's OWN dedup guard (below) and by Pass B2 (to skip
    // re-applying the "PrimitiveSource" key onto an already-recipe-spawned
    // entity).
    std::vector<bool> consumedByRecipe(entityCount, false);

    // alreadyParentedByRecipe[i] means: "this record's LIVE entity is
    // ALREADY correctly attached under its live parent, because
    // CreateMeshEntityFromGtaFile() parented it there itself." Set ONLY on a
    // saved child SUCCESSFULLY matched by-Name inside the reconciliation
    // loop below - NEVER on the recipe root record itself (see Pass B1 for
    // exactly why this distinction matters - conflating it with
    // consumedByRecipe would silently revert a manually-reparented recipe
    // root to being a top-level scene root on every Load).
    std::vector<bool> alreadyParentedByRecipe(entityCount, false);

    // childrenOf[i] = every j where document.entities[j].parentIndex == i,
    // in ascending j (== ascending saved-sibling) order. Built ONCE, up
    // front, by a single linear scan over document.entities before Pass A
    // starts - reused by the reconciliation loop AND by
    // markSubtreeUnresolved() below.
    std::vector<std::vector<std::size_t>> childrenOf(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (document.entities[i].parentIndex.has_value()) {
            childrenOf[*document.entities[i].parentIndex].push_back(i);
        }
    }

    // Recursively marks i's ENTIRE saved subtree (i itself, plus every
    // descendant, at any depth) as "already decided by Pass A - do not
    // create a bare entity for it, ever." resultEntities stays
    // kInvalidEntity for every one of them, forever. WITHOUT this, an
    // unresolvable asset root's saved children (and grandchildren, if any)
    // would each be reached later by Pass A's own i=0..N-1 loop with
    // resultEntities[] still kInvalidEntity AND consumedByRecipe[] still
    // false - indistinguishable from "never visited yet" - and would fall
    // through to the "plain entity" branch, spawning a stray, UNPARENTED
    // bare entity for each one (Pass B1 cannot parent it under a parent that
    // itself never resolved).
    std::function<void(std::size_t)> markSubtreeUnresolved = [&](std::size_t i) {
        consumedByRecipe[i] = true;
        for (const std::size_t childIndex : childrenOf[i]) {
            markSubtreeUnresolved(childIndex);
        }
    };

    // PASS A (recipe-aware entity creation).
    for (std::size_t i = 0; i < entityCount; ++i) {
        // FIX (bug #1 from PHASE4's own revision note): checking ONLY
        // "resultEntities[i] != kInvalidEntity" here is NOT sufficient - an
        // unmatched reconciled child (see the reconciliation loop below: a
        // saved child with no live counterpart gets resultEntities[j] left
        // at kInvalidEntity ON PURPOSE) looks EXACTLY like "not yet
        // visited" to that check alone, so this same loop would later
        // reprocess it from scratch and spawn a brand-new bare entity for
        // it - silently resurrecting a record this algorithm had already,
        // deliberately, decided to drop. consumedByRecipe[i] is the flag
        // that actually remembers "already decided" regardless of whether
        // that decision happened to leave resultEntities[i] valid or
        // invalid.
        if (resultEntities[i] != kInvalidEntity || consumedByRecipe[i]) {
            continue;
        }

        const SceneEntityRecord& record = document.entities[i];

        if (record.components.contains("PrimitiveSource")) {
            const nlohmann::json& primitiveSourceJson = record.components.at("PrimitiveSource");
            PrimitiveType parsedType{};
            if (primitiveSourceJson.contains("type") && primitiveSourceJson.at("type").is_string()
                && TryParsePrimitiveTypeName(primitiveSourceJson.at("type").get<std::string>(), parsedType)) {
                resultEntities[i] = game.CreatePrimitiveEntity(renderer, parsedType);
                consumedByRecipe[i] = true;
            }
            // else: malformed/unrecognized primitive type string - leave
            // resultEntities[i] == kInvalidEntity AND consumedByRecipe[i]
            // == false (degrade gracefully, matches this codebase's
            // existing "malformed input for THIS one item never aborts the
            // whole load" convention). No markSubtreeUnresolved() call
            // needed here - PrimitiveSource.h documents "a primitive spawn
            // is always a single node with no children", so there is no
            // subtree to worry about.
            continue;
        }

        if (!record.assetGuid.empty()) {
            const Guid guid = Guid::Parse(record.assetGuid); // Never throws - returns Guid::Invalid() for anything malformed.
            const AssetRecord* asset = assetDatabase.FindByGuid(guid);
            if (asset == nullptr) {
                // FIX (bug #2): moved/deleted/malformed asset guid - this
                // WHOLE root AND every one of its saved children (and
                // grandchildren, if the source format ever produces any) is
                // skipped entirely, matching scene-serialization-1's own
                // original per-object skip behavior.
                markSubtreeUnresolved(i);
                continue;
            }

            const Entity rootEntity = game.CreateMeshEntityFromGtaFile(renderer, asset->gtaPath);
            resultEntities[i] = rootEntity;
            consumedByRecipe[i] = true;
            // alreadyParentedByRecipe[i] is DELIBERATELY NOT set here - see
            // Pass B1 below for why the root record itself must still be
            // free to run through the normal SetParent() step.

            // --- Reconciliation: match this record's OWN saved children
            // (childrenOf[i]) against the LIVE children
            // CreateMeshEntityFromGtaFile() JUST created, by Name -
            // first-unused-match, walking saved children in ascending
            // saved-sibling order against live children in ascending
            // GetChildren() order (ECS/TransformHierarchy.h documents
            // GetChildren() as sorted by siblingIndex, ties broken by
            // stable creation order) - a deterministic, reproducible
            // pairing, not an arbitrary one. ---
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
                // "" both when the saved child record has NO Name block at
                // all, and when it has one whose value is itself an empty
                // string - both are treated identically by this loop. An
                // earlier draft required "savedChildName != ''" here, i.e.
                // it NEVER matched an unnamed saved child to anything - that
                // directly contradicted this phase's own promise that a
                // hand-edited Transform on an UNNAMED submesh part survives
                // a round trip too; empty string is now just another valid
                // (if weak) matching key, paired positionally like any
                // repeated name.

                Entity match = kInvalidEntity;
                for (std::size_t k = 0; k < liveChildren.size(); ++k) {
                    if (usedLiveChild[k]) {
                        continue;
                    }
                    // A live part entity is NOT guaranteed to have a Name
                    // component at all (whenever the source .pmx left that
                    // material unnamed) - TryGetComponent (never a bare
                    // GetComponent) is mandatory here for exactly that
                    // reason.
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

        // Neither PrimitiveSource nor a resolvable assetGuid - a plain
        // entity (Camera/Light/empty node/an unresolvable-asset's now-
        // orphaned saved child, etc) - PHASE3's original bare-entity
        // behavior, unchanged: a default Transform is added right here, up
        // front, so Pass B1's SetParent() below (which requires one on both
        // sides) can actually succeed - see PHASE3_COMPLETION_REPORT.md's
        // own "Discrepancy found" note for why the literal "bare
        // CreateEntity() only" pseudocode does not, by itself, work.
        resultEntities[i] = registry.CreateEntity();
        registry.AddComponent<Transform>(resultEntities[i]);
    }

    // PASS B1 (hierarchy wiring) - unchanged from PHASE3, with guards. Wire
    // hierarchy FIRST (before applying Transform values - SetParent()'s
    // worldPositionStays=true would otherwise RECOMPUTE local
    // position/rotation/scale and CLOBBER whatever Pass B2 is about to
    // write). worldPositionStays=false here is what avoids that.
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (resultEntities[i] == kInvalidEntity) {
            continue; // Skip anything left unresolved (an unmatched reconciled child, or anything markSubtreeUnresolved() touched).
        }
        if (alreadyParentedByRecipe[i]) {
            // FIX (bug #3): ONLY true for a matched asset-root CHILD -
            // CreateMeshEntityFromGtaFile() already attached it under the
            // SAME live root entity Pass A just resolved; re-parenting it
            // again would be harmless but pointless.
            //
            // Deliberately NOT gated on consumedByRecipe[i] the way an
            // earlier draft of this algorithm did. A PrimitiveSource/asset-
            // root RECORD ITSELF must still run through the normal
            // SetParent() step below whenever it has its OWN saved
            // parentIndex, because CreatePrimitiveEntity()/
            // CreateMeshEntityFromGtaFile() ALWAYS create their entity as a
            // brand-new top-level scene root with no parent at all - they
            // know nothing about whatever OTHER entity that root might have
            // been manually dragged under in the Hierarchy panel before it
            // was saved. Skipping SetParent() for every consumedByRecipe[i]
            // == true record (the earlier draft's mistake) would mean an
            // imported mesh (or a primitive) that had been reparented under
            // some organizational "Group" node would silently revert to
            // being a top-level scene root on every Load, a real, visible
            // regression.
            continue;
        }
        const SceneEntityRecord& record = document.entities[i];
        if (record.parentIndex.has_value()) {
            const Entity parentEntity = resultEntities[*record.parentIndex];
            if (parentEntity != kInvalidEntity) {
                SetParent(registry, resultEntities[i], parentEntity, /*worldPositionStays=*/false);
            }
            // else: this record's OWN saved parent itself never resolved.
            // In practice this is already unreachable for a
            // markSubtreeUnresolved()-affected record (its own
            // resultEntities[i] is kInvalidEntity too, so this whole
            // iteration is already skipped by the guard above) - this else
            // branch is purely defensive.
        }
    }

    // PASS B2 (generic field application) - unchanged from PHASE3, with a
    // guard. This is what actually restores Transform/Name/Camera/etc to
    // their EXACT saved values, INCLUDING for consumedByRecipe[i] == true
    // entities - this is what restores a hand-edited child part's Transform
    // (superseding scene-serialization-1's old Design Decision #3), and
    // what restores the asset ROOT's own Name/Transform too.
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
                    // A single malformed FIELD does not fail the whole
                    // Load - this engine's general "degrade gracefully"
                    // convention - deliberately relaxed here because a
                    // single bad float in an otherwise-huge scene file
                    // should not lose everything else in it. The field is
                    // simply left at whatever ensureDefaultComponent()'s
                    // default (or its prior value, if the component
                    // already existed) left it as.
                }
            }
        }
    }

    // PASS B3 (sibling ordering) - unchanged from PHASE3, with the same
    // resultEntities[i] == kInvalidEntity guard added. No
    // alreadyParentedByRecipe check here - re-applying a matched child's
    // already-correct saved sibling index is harmless/idempotent, and a
    // reparented recipe root (see the Pass B1 fix above) DOES need its
    // saved sibling index re-applied like any other record.
    for (std::size_t i = 0; i < entityCount; ++i) {
        if (resultEntities[i] == kInvalidEntity) {
            continue;
        }
        SetSiblingIndex(registry, resultEntities[i], document.entities[i].siblingIndex);
    }

    // PHASE4 (section 3.4) small hardening: guarantee a Camera exists
    // immediately after this function returns, rather than only on the
    // NEXT Game::Render() call (which is what actually invokes
    // EnsureDefaultCameraExists() every frame). Harmless/idempotent when a
    // Camera record WAS present in the loaded document (that method's own
    // live-count guard makes this a no-op in that case).
    game.EnsureDefaultCameraExists();
}

} // namespace gte
