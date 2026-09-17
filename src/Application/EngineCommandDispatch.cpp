#include "EngineCommandDispatch.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"

#include <filesystem>

// task_manager/scene-serialization-2 campaign, PHASE5
// (PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md) - Editor/SceneIO.h is
// compiled ONLY when GTE_ENABLE_EDITOR is ON (it depends on
// Editor/ProjectRootPath.h) - this file itself is a CORE, always-compiled
// file, so this include (and the SaveScene/LoadScene case branches below)
// must be conditionally compiled.
#if GTE_ENABLE_EDITOR
#include "../Editor/SceneIO.h"
#endif

namespace gte {

EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, const EngineCommandRequest& request)
{
    EngineCommandResult result;
    result.kind = request.kind;
    switch (request.kind) {
    case EngineCommandKind::InstantiatePrimitive: {
        const InstantiatePrimitiveCommand& cmd = request.instantiatePrimitive;
        result.instantiatePrimitive = game.InstantiatePrimitive(
            renderer, cmd.shape, cmd.requestedName, cmd.worldPosition, cmd.hasParent, cmd.parentName);
        break;
    }
    case EngineCommandKind::DeleteEntity: {
        result.deleteEntity = game.DeleteEntityByName(request.deleteEntity.name);
        break;
    }
    // network-impl-5 campaign - neither of these two touches `renderer` at
    // all (see PHASE2's own Testability note) - `renderer` stays
    // unreferenced in these two branches, exactly mirroring
    // DeleteEntity's own branch immediately above, which also never
    // touches it.
    case EngineCommandKind::SetEntityTrs: {
        result.setEntityTrs = game.SetEntityTrs(request.setEntityTrs);
        break;
    }
    case EngineCommandKind::InstantiateLight: {
        result.instantiateLight = game.InstantiateLight(request.instantiateLight);
        break;
    }
    // task_manager/stl-parser-2 campaign, PHASE3 - spawns an already-
    // imported Mesh *.gta asset. Touches `renderer` (the freshly-spawned
    // GPU mesh must be uploaded through it), unlike SetEntityTrs/
    // InstantiateLight immediately above.
    case EngineCommandKind::InstantiateMeshAsset: {
        result.instantiateMeshAsset = game.InstantiateMeshAssetFromGtaFile(
            renderer, request.instantiateMeshAsset.absoluteGtaPath);
        break;
    }
    // task_manager/scene-serialization-2 campaign, PHASE5
    // (PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md) - thin network bridge
    // to Editor/SceneIO.h's SaveScene()/LoadScene(). Not compiled at all in
    // a GTE_ENABLE_EDITOR=OFF build - `result.saveScene.editorAvailable`/
    // `result.loadScene.editorAvailable` default to `true` (see
    // EngineCommandResults.h), so the #else branch below must explicitly
    // set them to `false`.
    case EngineCommandKind::SaveScene: {
#if GTE_ENABLE_EDITOR
        const std::filesystem::path path = request.saveScene.path.empty()
            ? DefaultScenePath()
            : std::filesystem::path(request.saveScene.path);
        result.saveScene.success = SaveScene(game, path);
        result.saveScene.resolvedPath = path.string();
        if (!result.saveScene.success) {
            result.saveScene.errorMessage = "failed to write scene file (I/O error) - see engine log";
        }
#else
        result.saveScene.editorAvailable = false;
        result.saveScene.errorMessage =
            "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
#endif
        break;
    }
    case EngineCommandKind::LoadScene: {
#if GTE_ENABLE_EDITOR
        const std::filesystem::path path = request.loadScene.path.empty()
            ? DefaultScenePath()
            : std::filesystem::path(request.loadScene.path);
        result.loadScene.success = LoadScene(game, renderer, path);
        result.loadScene.resolvedPath = path.string();
        if (!result.loadScene.success) {
            result.loadScene.errorMessage =
                "failed to load scene file - it may not exist, or failed to parse (see engine log)";
        }
#else
        result.loadScene.editorAvailable = false;
        result.loadScene.errorMessage =
            "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
#endif
        break;
    }
    }
    return result;
}

} // namespace gte
