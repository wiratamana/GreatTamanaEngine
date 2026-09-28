#include "EngineCommandDispatch.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"

#include <filesystem>

namespace gte {

EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, ISceneIOCapability* sceneIOCapability,
    IHotReloadDebugCapability* hotReloadDebugCapability, const EngineCommandRequest& request)
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
    // to Editor/SceneIO.h's SaveScene()/LoadScene(), now reached ONLY
    // through the nullable ISceneIOCapability* hook (editor-core-separation-1
    // campaign, PHASE6 - PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md)
    // instead of a compile-time `#if GTE_ENABLE_EDITOR`. `sceneIOCapability
    // == nullptr` (a future Player host, or today's GTE_ENABLE_EDITOR=OFF
    // configuration - see Application.cpp's own PHASE6 wiring comment) hits
    // the EXACT SAME fallback message this function's old `#else` branch
    // used to set - a behavior-preserving refactor, not a chance to change
    // what a capability-less build reports.
    case EngineCommandKind::SaveScene: {
        if (sceneIOCapability != nullptr) {
            const std::filesystem::path path = request.saveScene.path.empty()
                ? sceneIOCapability->DefaultScenePath()
                : std::filesystem::path(request.saveScene.path);
            result.saveScene.success = sceneIOCapability->SaveScene(game, path, result.saveScene.errorMessage);
            result.saveScene.resolvedPath = path.string();
        } else {
            result.saveScene.editorAvailable = false;
            result.saveScene.errorMessage =
                "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
        }
        break;
    }
    case EngineCommandKind::LoadScene: {
        if (sceneIOCapability != nullptr) {
            const std::filesystem::path path = request.loadScene.path.empty()
                ? sceneIOCapability->DefaultScenePath()
                : std::filesystem::path(request.loadScene.path);
            result.loadScene.success = sceneIOCapability->LoadScene(game, renderer, path, result.loadScene.errorMessage);
            result.loadScene.resolvedPath = path.string();
        } else {
            result.loadScene.editorAvailable = false;
            result.loadScene.errorMessage =
                "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
        }
        break;
    }
    // editor-core-separation-12 campaign, PHASE2 (Project Assembly Hot
    // Reload plan, BIG-STEP 1) - reuses this SAME single-global-slot bridge
    // for a READ-ONLY ECS world/scene snapshot request (see
    // Core/EditorCapabilities.h's IHotReloadDebugCapability doc comment for
    // why this must run through the main-thread-only bridge rather than a
    // direct network-thread read of the live Registry).
    case EngineCommandKind::GetSceneSnapshot: {
        if (hotReloadDebugCapability != nullptr) {
            result.getSceneSnapshot.sceneJson = hotReloadDebugCapability->BuildSceneSnapshotJson(game);
            result.getSceneSnapshot.success = true;
        } else {
            result.getSceneSnapshot.editorAvailable = false;
            result.getSceneSnapshot.errorMessage =
                "scene snapshot requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
        }
        break;
    }
    }
    return result;
}

} // namespace gte
