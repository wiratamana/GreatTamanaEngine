#pragma once

// Cross-thread bridge for network-driven ECS-mutating/read requests:
// instantiate/delete entities, set transforms, instantiate lights/mesh
// assets, save/load scenes, snapshot the scene, and the hot-reload marker
// test probe. Single global slot - only one engine command, of any kind,
// may be in flight at a time. See SyncCommandBridge.h for the handoff
// mechanism.

#include "../ECS/Entity.h"
#include "../Game/EngineCommandResults.h"
#include "../Math/Vec3.h"

#include "SyncCommandBridge.h"

#include <string>

namespace gte {

enum class EngineCommandKind {
    InstantiatePrimitive,
    DeleteEntity,
    SetEntityTrs,
    InstantiateLight,
    InstantiateMeshAsset,
    SaveScene,
    LoadScene,
    GetSceneSnapshot,
    SetProbeHotReloadMarkerValueForTesting,
};

// Plain request payload for one InstantiatePrimitive command.
struct InstantiatePrimitiveCommand {
    std::string shape;
    std::string requestedName;
    Vec3 worldPosition;
    bool hasParent = false;
    std::string parentName;
};

// Plain request payload for one DeleteEntity command.
struct DeleteEntityCommand {
    std::string name;
};

// Plain request payload for one InstantiateMeshAsset command.
struct InstantiateMeshAssetCommand {
    std::string absoluteGtaPath;
};

// "" means "use Editor::SceneIO.h's own DefaultScenePath()".
struct SaveSceneCommand {
    std::string path;
};
struct LoadSceneCommand {
    std::string path;
};

// No payload needed - the snapshot always covers the whole live scene.
struct GetSceneSnapshotCommand {};

// Payload for the one narrow, hardcoded, testing-only ProbeHotReloadMarker
// mutation command.
struct SetProbeHotReloadMarkerValueForTestingCommand {
    int value = 0;
};

// One pending engine command, tagged by `kind` - only the field matching
// `kind` is meaningful.
struct EngineCommandRequest {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveCommand instantiatePrimitive;
    DeleteEntityCommand deleteEntity;
    SetEntityTrsParams setEntityTrs;
    InstantiateLightParams instantiateLight;
    InstantiateMeshAssetCommand instantiateMeshAsset;
    SaveSceneCommand saveScene;
    LoadSceneCommand loadScene;
    GetSceneSnapshotCommand getSceneSnapshot;
    SetProbeHotReloadMarkerValueForTestingCommand setProbeHotReloadMarkerValueForTesting;
};

// The completed result of one EngineCommandRequest - `kind` mirrors the
// request's own `kind`.
struct EngineCommandResult {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveOutcome instantiatePrimitive;
    DeleteEntityOutcome deleteEntity;
    SetEntityTrsOutcome setEntityTrs;
    InstantiateLightOutcome instantiateLight;
    InstantiateMeshAssetOutcome instantiateMeshAsset;
    SaveSceneOutcome saveScene;
    LoadSceneOutcome loadScene;
    GetSceneSnapshotOutcome getSceneSnapshot;
    SetProbeHotReloadMarkerValueForTestingOutcome setProbeHotReloadMarkerValueForTesting;
};

class EngineCommandBridge : public SyncCommandBridge<EngineCommandRequest, EngineCommandResult> {
};

} // namespace gte
