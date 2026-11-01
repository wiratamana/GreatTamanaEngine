#pragma once

// Cross-thread bridge for network-driven Editor UI commands (tabs, inspector, views, cameras).
// See SyncCommandBridge.h for the handoff mechanism.

#include "SyncCommandBridge.h"
#include "../Assets/MeshClusterOptions.h"
#include "../Core/InstanceGridLimits.h"
#include "../Editor/HierarchyActionRequest.h"
#include "../Editor/HierarchyStateReport.h"
#include "../Editor/MouseInjection.h"
#include "../Editor/RotationFieldProbe.h"
#include "../Editor/SceneCameraControl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

enum class EditorUiCommandKind {
    ActivateTab,
    SpawnInstanceGrid,
    SelectAsset,
    SelectEntity,
    InjectMouse,
    InspectorState,
    InspectorBuildClusters,
    SplitViews,
    ClusterViewer,
    SceneCamera,
    HierarchyState,
    PanelVisible,
    HierarchyAction,
    Exit, // No payload; the editor loop ends after the reply is sent.
};

// Plain request payload for one HierarchyState command: `count` rows starting at row `first`.
struct HierarchyStateCommand {
    std::uint64_t first = 0;
    std::uint32_t count = 200;
};

// Plain request payload for one PanelVisible command: `name` is a registered window name.
struct PanelVisibleCommand {
    std::string name;
    bool visible = true;
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

// Plain request payload for one SelectEntity command.
struct SelectEntityCommand {
    std::string name;
};

// Cluster options of one InspectorBuildClusters command; same fields as the Inspector's own controls.
struct InspectorBuildClustersCommand {
    std::string weld = "exact"; // "exact" or "position_only".
    std::uint32_t maxVertices = MeshClusterOptions{}.maxVerticesPerCluster;
    std::uint32_t maxTriangles = MeshClusterOptions{}.maxTrianglesPerCluster;
    float coneWeight = MeshClusterOptions{}.coneWeight;
};

// Plain request payload for one SplitViews command.
struct SplitViewsCommand {
    bool enabled = false; // true = Game beside Scene, false = Game as a tab of Scene.
};

enum class ClusterViewerOperation : std::uint8_t { Open, State, Set, Pick, Verify, CaptureHiz, Image };

// Request payload for one ClusterViewer command; only the fields of `operation` are read.
struct ClusterViewerCommand {
    ClusterViewerOperation operation = ClusterViewerOperation::State;
    std::string path;            // Open
    std::vector<KeyValue> pairs; // Set
    float x = 0.0f;              // Pick, 0..1 of the viewer image
    float y = 0.0f;
    ClusterViewerImageTarget target = ClusterViewerImageTarget::View; // Image
    bool overlay = false;                                             // Image
};

enum class SceneCameraOperation : std::uint8_t { State, Select, Pick, Image, SetPose, SetView };

// Request payload for one SceneCamera command; only the fields of `operation` are read.
struct SceneCameraCommand {
    SceneCameraOperation operation = SceneCameraOperation::State;
    std::string name;       // Select
    float x = 0.0f;         // Pick, 0..1 of the Scene image
    float y = 0.0f;
    bool overlay = true;    // Image
    float poseX = 0.0f;     // SetPose, world units
    float poseY = 0.0f;
    float poseZ = 0.0f;
    float yawDegrees = 0.0f;   // SetPose
    float pitchDegrees = 0.0f;
    SceneCameraViewRequest view; // SetView
};

// One pending Editor UI command, tagged by `kind` - only the field matching
// `kind` is meaningful. InspectorState carries no payload.
struct EditorUiCommandRequest {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabCommand activateTab;
    SpawnInstanceGridCommand spawnInstanceGrid;
    SelectAssetCommand selectAsset;
    SelectEntityCommand selectEntity;
    MouseInjection injectMouse;
    InspectorBuildClustersCommand inspectorBuildClusters;
    SplitViewsCommand splitViews;
    ClusterViewerCommand clusterViewer;
    SceneCameraCommand sceneCamera;
    HierarchyStateCommand hierarchyState;
    PanelVisibleCommand panelVisible;
    HierarchyActionCommand hierarchyAction;
};

// Outcome of one ActivateTab command. `success` is false when the editor has no window with that name.
struct ActivateTabOutcome {
    bool success = false;
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

// Outcome of one SelectEntity command. `editorAvailable` is false when the build has no Editor.
struct SelectEntityOutcome {
    bool success = false;
    bool editorAvailable = true;
    bool notFound = false; // No entity has that name.
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
    std::string cloneButtonDisabledReason; // Empty = "Create Clustered Copy" is enabled.
    std::string cloneDestination;          // File name only.
    std::string cloneWeld;                 // "auto" | "exact" | "position_only"
    bool openViewerAfterClone = false;
    RotationFieldReport rotation;
};

// Outcome of one InjectMouse command. `editorAvailable` is false when the build has no Editor.
struct InjectMouseOutcome {
    bool editorAvailable = true;
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

// Outcome of one ClusterViewer command. `available` is false when the editor has no viewer.
// `report` is filled for every operation except Image.
struct ClusterViewerOutcome {
    bool available = true;
    ClusterViewerResult result;
    bool hit = false; // Pick
    ClusterViewerReport report;
    std::optional<ClusterViewerImage> image; // Image
};

// Outcome of one PanelVisible command. `editorAvailable` is false when the build has no editor panels.
struct PanelVisibleOutcome {
    bool editorAvailable = true;
};

// Outcome of one SceneCamera command. `available` is false when the editor has no Scene view.
struct SceneCameraOutcome {
    bool available = true;
    SceneCameraResult result;
    bool hit = false;        // Pick
    std::string entityName;  // Pick
    SceneCameraReport report;
    std::optional<ClusterViewerImage> image; // Image
};

// The completed result of one EditorUiCommandRequest - `kind` mirrors the
// request's own `kind`.
struct EditorUiCommandResult {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabOutcome activateTab;
    SpawnInstanceGridOutcome spawnInstanceGrid;
    SelectAssetOutcome selectAsset;
    SelectEntityOutcome selectEntity;
    InspectorStateOutcome inspectorState;
    InjectMouseOutcome injectMouse;
    InspectorBuildClustersOutcome inspectorBuildClusters;
    SplitViewsOutcome splitViews;
    ClusterViewerOutcome clusterViewer;
    SceneCameraOutcome sceneCamera;
    HierarchyStateReport hierarchyState;
    PanelVisibleOutcome panelVisible;
    HierarchyActionReport hierarchyAction;
};

class EditorUiCommandBridge : public SyncCommandBridge<EditorUiCommandRequest, EditorUiCommandResult> {
};

} // namespace gte
