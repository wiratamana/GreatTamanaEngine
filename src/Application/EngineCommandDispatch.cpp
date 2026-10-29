#include "EngineCommandDispatch.h"

#include "../ECS/Reflection/ComponentTypeRegistry.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"
#include "../Renderer/Renderer.h"

#include <filesystem>

namespace gte {

EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, ISceneIOCapability* sceneIOCapability,
    IHotReloadDebugCapability* hotReloadDebugCapability, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const EngineCommandRequest& request)
{
    EngineCommandResult result;
    result.kind = request.kind;
    switch (request.kind) {
    case EngineCommandKind::InstantiatePrimitive: {
        const InstantiatePrimitiveCommand& cmd = request.instantiatePrimitive;
        result.instantiatePrimitive = game.InstantiatePrimitive(renderer, cmd.shape, cmd.requestedName,
            cmd.worldPosition, cmd.hasParent, cmd.parentName, sceneServicesSetLayout);
        break;
    }
    case EngineCommandKind::DeleteEntity: {
        result.deleteEntity = game.DeleteEntityByName(request.deleteEntity.name);
        break;
    }
    // These two cases do not touch `renderer`.
    case EngineCommandKind::SetEntityTrs: {
        result.setEntityTrs = game.SetEntityTrs(request.setEntityTrs);
        break;
    }
    case EngineCommandKind::InstantiateLight: {
        result.instantiateLight = game.InstantiateLight(request.instantiateLight);
        break;
    }
    // Spawns an imported mesh asset; uploads its GPU mesh through `renderer`.
    case EngineCommandKind::InstantiateMeshAsset: {
        result.instantiateMeshAsset = game.InstantiateMeshAssetFromGtaFile(
            renderer, request.instantiateMeshAsset.absoluteGtaPath, sceneServicesSetLayout);
        break;
    }
    // Scene save/load goes through the scene IO capability; a null capability answers "not available".
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
            result.loadScene.success =
                sceneIOCapability->LoadScene(game, renderer, path, sceneServicesSetLayout, result.loadScene.errorMessage);
            result.loadScene.resolvedPath = path.string();
        } else {
            result.loadScene.editorAvailable = false;
            result.loadScene.errorMessage =
                "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
        }
        break;
    }
    // Read-only scene snapshot; runs here because the live registry is main-thread only.
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
    // Test-only route: sets "value" on the first entity carrying the "ProbeHotReloadMarker" component.
    case EngineCommandKind::SetProbeHotReloadMarkerValueForTesting: {
        bool success = false;
        if (const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("ProbeHotReloadMarker");
            descriptor != nullptr) {
            for (const Entity root : GetChildren(game.GetRegistry(), kInvalidEntity)) {
                if (void* component = descriptor->tryGetMutableComponent(game.GetRegistry(), root); component != nullptr) {
                    // readJson() expects an object keyed by the field name, not a bare scalar.
                    nlohmann::json fields;
                    fields["value"] = request.setProbeHotReloadMarkerValueForTesting.value;
                    std::string errorMessage;
                    for (const FieldDescriptor& field : descriptor->fields) {
                        if (field.name == "value") {
                            success = field.readJson(component, fields, errorMessage);
                        }
                    }
                    break; // The first entity carrying the component wins.
                }
            }
        }
        result.setProbeHotReloadMarkerValueForTesting.success = success;
        break;
    }
    // Opens a preset scene; a first-time import can take a long time.
    case EngineCommandKind::OpenPresetScene: {
        const OpenPresetSceneCommand& cmd = request.openPresetScene;
        OpenPresetSceneOutcome& out = result.openPresetScene;
        out.preset = std::string(PresetSceneName(cmd.preset));
        if (sceneIOCapability != nullptr) {
            ISceneIOCapability::OpenSceneOutcome outcome = sceneIOCapability->OpenPresetScene(
                game, renderer, sceneServicesSetLayout, cmd.preset, cmd.parameters);
            out.success = outcome.success;
            out.importedThisCall = outcome.importedThisCall;
            out.message = std::move(outcome.message);
            out.gtaPath = std::move(outcome.gtaPath);
            out.detailsJson = std::move(outcome.detailsJson);
        } else {
            out.editorAvailable = false;
            out.message = "opening a preset scene requires the Editor module";
        }
        break;
    }
    }
    return result;
}

} // namespace gte
