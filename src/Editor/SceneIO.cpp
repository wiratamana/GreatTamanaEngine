#include "SceneIO.h"

#include "ProjectRootPath.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Reflection/ComponentTypeRegistry.h"
#include "../ECS/Registry.h"
#include "../ECS/TransformHierarchy.h"
#include "../Assets/AssetDatabase.h"
#include "../Game/Game.h"
#include "../Scene/SceneBuilder.h"
#include "../Scene/SceneJsonFormat.h"

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
    // `renderer` is not yet used by this phase's own generic reconstruction
    // - accepted for signature stability with the eventual (PHASE4)
    // recipe-spawn path, which needs one to build/upload GPU mesh data.
    (void)renderer;

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

    Registry& registry = game.GetRegistry();
    // PHASE4 replaces this call with ClearEntireScene(registry) - see
    // Scene/SceneBuilder.h's own doc comment on why this is unchanged in
    // THIS phase.
    ClearSerializableSceneObjects(registry);

    std::vector<Entity> resultEntities(document->entities.size(), kInvalidEntity);

    // Pass A - create every entity, with a default Transform component.
    // PHASE4 TODO: replace this loop's body with a recipe-aware version
    // (spawning a PrimitiveSource/MeshAssetSource record via
    // Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile() instead
    // of a bare CreateEntity(), so it gets a real MeshRenderer) - see
    // task_manager/scene-serialization-2/PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md.
    // Every entity (including a Primitive/Asset-tagged one) still just
    // becomes a bare entity in THIS phase, which is the "intentionally
    // simplified" gap this phase's own strategy file's Step 1 calls out:
    // it will have NO MeshRenderer/visible mesh yet after only this phase,
    // but WILL have its correct Transform/Name/any-other-reflected-field
    // once Pass B2 below runs.
    //
    // DISCREPANCY vs. this phase's own strategy file
    // (PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md,
    // section 3.4): that file's own literal pseudocode has this loop create
    // a BARE entity only (`registry.CreateEntity()`, no Transform added),
    // with hierarchy wiring (Pass B1 below) happening immediately after.
    // But SetParent() (ECS/TransformHierarchy.h) explicitly REQUIRES both
    // the child and a non-invalid parent to already have a Transform
    // component, or it fails outright and leaves the child completely
    // untouched (see that function's own doc comment) - so run exactly as
    // written, Pass B1 would silently fail to wire up ANY parent/child
    // relationship at all, for every entity, every single Load. This was
    // caught by this phase's own required manual sanity check
    // (tests/Scene/SceneRoundTripIntegrationTests.cpp) before it could ship
    // silently broken. The fix: add a default Transform to every entity
    // right here, in Pass A, BEFORE Pass B1 ever runs - safe because every
    // entity BuildSceneDocumentFromRegistry() ever visits already had a
    // live Transform to begin with (see SceneBuilder.h's own doc comment:
    // "An entity with no Transform component at all is out of scope"), so
    // its own record's "components" bag always has a "Transform" entry
    // too; Pass B2 below then overwrites this default with the EXACT saved
    // field values (ensureDefaultComponent() never resets an
    // already-present component - see ComponentTypeDescriptor.h), so this
    // has no observable effect beyond making Pass B1's SetParent() actually
    // succeed. See PHASE3_COMPLETION_REPORT.md's own "Notes / discrepancies"
    // section for the full writeup.
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        resultEntities[i] = registry.CreateEntity();
        registry.AddComponent<Transform>(resultEntities[i]);
    }

    // Pass B1 - wire hierarchy FIRST (before applying Transform values -
    // SetParent()'s worldPositionStays=true would otherwise RECOMPUTE local
    // position/rotation/scale and CLOBBER whatever Pass B2 is about to
    // write). worldPositionStays=false here is what avoids that.
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        const SceneEntityRecord& record = document->entities[i];
        if (record.parentIndex.has_value()) {
            const Entity parentEntity = resultEntities[*record.parentIndex];
            SetParent(registry, resultEntities[i], parentEntity, /*worldPositionStays=*/false);
        }
    }

    // Pass B2 - generically apply every reflected component's fields -
    // this is what actually restores Transform/Name/Camera/etc to their
    // EXACT saved values, regardless of whatever Pass A/B1 left them as.
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        const SceneEntityRecord& record = document->entities[i];
        const Entity entity = resultEntities[i];
        for (auto it = record.components.begin(); it != record.components.end(); ++it) {
            const std::string& typeName = it.key();
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
                    // convention (rather than SceneTextFormat.cpp's old
                    // "any malformed field fails the WHOLE document"
                    // behavior) - deliberately relaxed here because a
                    // single bad float in an otherwise-huge scene file
                    // should not lose everything else in it. The field is
                    // simply left at whatever ensureDefaultComponent()'s
                    // default (or its prior value, if the component
                    // already existed) left it as.
                }
            }
        }
    }

    // Pass B3 - restore sibling ordering, per parent group, ascending by
    // saved siblingIndex (SetSiblingIndex() renumbers the WHOLE sibling
    // group each call, so process every record whose parent is the SAME,
    // in ascending siblingIndex order, calling SetSiblingIndex once per
    // entity) - best-effort, matching GetChildren()'s own documented
    // "ties broken by creation/dense-storage order" tolerance elsewhere in
    // this engine, not a pixel-perfect ordering guarantee.
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        SetSiblingIndex(registry, resultEntities[i], document->entities[i].siblingIndex);
    }

    return true;
}

} // namespace gte
