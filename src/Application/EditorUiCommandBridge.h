#pragma once

// Cross-thread bridge for network-driven Editor UI commands: tab focus, instance grid spawn,
// Inspector asset selection, Inspector state, Inspector cluster build and the Game/Scene split.
// See SyncCommandBridge.h for the handoff mechanism.

#include "SyncCommandBridge.h"
#include "../Core/InstanceGridLimits.h"

#include <cstdint>
#include <string>

namespace gte {

enum class EditorUiCommandKind {
    ActivateTab,
    SpawnInstanceGrid,
    SelectAsset,
    InspectorState,
    InspectorBuildClusters,
    SplitViews,
};

// Plain request payload for one ActivateTab command.
struct ActivateTabCommand {
    std::string tabName;
};

// Plain request payload for one SpawnInstanceGrid command.
struct SpawnInstanceGridCommand {
    std::uint32_t instanceCount = kDefaultSpawnGridInstances; // 1..kMaxSpawnGridInstances.
    float spacing = kDefaultSpawnGridSpacing;                 // Centre distance between instances.
};

// Plain request payload for one SelectAsset command.
struct SelectAssetCommand {
    std::string absolutePath;
};

// Cluster options of one InspectorBuildClusters command; same fields as the Inspector's own controls.
struct InspectorBuildClustersCommand {
    std::string weld = "exact"; // "exact" or "position_only".
    std::uint32_t maxVertices = 64;
    std::uint32_t maxTriangles = 124;
    float coneWeight = 0.5f;
};

// Plain request payload for one SplitViews command.
struct SplitViewsCommand {
    bool enabled = false; // true = Game beside Scene, false = Game as a tab of Scene.
};

// One pending Editor UI command, tagged by `kind` - only the field matching
// `kind` is meaningful. InspectorState carries no payload.
struct EditorUiCommandRequest {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabCommand activateTab;
    SpawnInstanceGridCommand spawnInstanceGrid;
    SelectAssetCommand selectAsset;
    InspectorBuildClustersCommand inspectorBuildClusters;
    SplitViewsCommand splitViews;
};

// Outcome of one ActivateTab command.
// - success == true                        -> tab found and focused
// - success == false && tabExists == false  -> no live ImGui window with
//                                              that exact name existed this
//                                              frame
// A name not in the known tab list is rejected before ever reaching this
// bridge - this outcome type has no field for that case.
struct ActivateTabOutcome {
    bool success = false;
    bool tabExists = false;
};

// Outcome of one SpawnInstanceGrid command. `success == false` covers a refused grid
// and an Editor module that is unavailable in this build; `errorMessage` explains which,
// `editorAvailable` distinguishes the two without sniffing error text.
struct SpawnInstanceGridOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::uint32_t instanceCount = 0;
};

// Outcome of one SelectAsset command. `editorAvailable` is false when the build has no Project panel.
struct SelectAssetOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string message;
};

// What the Inspector showed on its last draw. Strings are empty when nothing of that kind was shown.
struct InspectorStateOutcome {
    bool editorAvailable = true;
    std::string selectionKind; // "none", "entity", "asset" or "model_part".
    std::string selectedPath;  // Absolute path of the selected asset.
    std::string shownPath;     // Asset the fields below were drawn for.
    std::string clustersText;  // The "Clusters: N" line of the mesh metadata.
    std::string clusterStatus; // Status line of the cluster tool.
    bool buildEnabled = false;
    std::string buildDisabledReason;
    bool buildRunning = false;
};

enum class InspectorBuildClustersStatus {
    Started,
    Busy,
    Unavailable,
    Refused,
};

// Outcome of one InspectorBuildClusters command. The report itself shows up in InspectorState later.
struct InspectorBuildClustersOutcome {
    InspectorBuildClustersStatus status = InspectorBuildClustersStatus::Unavailable;
    std::string message;
};

// Outcome of one SplitViews command.
struct SplitViewsOutcome {
    bool success = false;
    bool split = false; // Game and Scene are in separate dock nodes after the command.
    std::string message;
};

// The completed result of one EditorUiCommandRequest - `kind` mirrors the
// request's own `kind`.
struct EditorUiCommandResult {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabOutcome activateTab;
    SpawnInstanceGridOutcome spawnInstanceGrid;
    SelectAssetOutcome selectAsset;
    InspectorStateOutcome inspectorState;
    InspectorBuildClustersOutcome inspectorBuildClusters;
    SplitViewsOutcome splitViews;
};

class EditorUiCommandBridge : public SyncCommandBridge<EditorUiCommandRequest, EditorUiCommandResult> {
};

} // namespace gte
