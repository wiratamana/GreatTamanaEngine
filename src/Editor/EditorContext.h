#pragma once

#include "CameraPick.h"
#include "ClusterToolUiState.h"
#include "HizDemoState.h"
#include "InstanceGridTestSpawner.h"
#include "Panels/HierarchyRows.h"
#include "RotationEulerEditCache.h"
#include "RotationFieldProbe.h"
#include "Selection.h"
#include "../Core/Extent.h"
#include "../Core/UiTextureHandle.h"
#include "TransformGizmo.h"
#include "../Math/Mat4.h"
#include "../Math/Vec2.h"
#include "RenderGraphRegimeChoice.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gte {

// Forward declaration only: the full definition lives in Core/EditorCapabilities.h and is heavy.
enum class AssetScaffoldKind;

class MeshClusterBuildRunner;

// ImGui drag-and-drop payload type for a file path dragged out of the Project panel (UTF-8, NUL-terminated).
inline constexpr const char* kProjectAssetDragDropPayloadType = "GTE_PROJECT_ASSET_PATH";

// ImGui drag-and-drop payload type for an entity row dragged within the Hierarchy panel (a plain Entity value).
inline constexpr const char* kHierarchyEntityDragDropPayloadType = "GTE_HIERARCHY_ENTITY";

// Scene camera entity <-> 64-bit id used by the pick candidates and the HTTP report.
inline std::uint64_t SceneCameraEntityId(Entity entity) noexcept
{
    return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation;
}
inline Entity SceneCameraEntityFromId(std::uint64_t id) noexcept
{
    return Entity{ static_cast<std::uint32_t>(id >> 32), static_cast<std::uint32_t>(id & 0xFFFFFFFFu) };
}

// What the Scene panel drew for cameras last frame. Valid while `rendered` is true.
struct SceneCameraFrame {
    bool rendered = false;
    Vec2 rectMin;                // screen rect of the Scene image
    Vec2 rectMax;
    Mat4 viewProjection = Mat4::Identity(); // flipY = true, the matrix the Scene view renders with
    std::vector<CameraPickCandidate> candidates;
    std::uint64_t selectedId = 0; // selected Camera entity, 0 when none
};

// Plain shared state every editor window and the dock layout can reach through EditorFrame::ctx.
struct EditorContext {
    // ImGui handle of the Game view texture; created lazily by BuildUI(), read by GamePanel.
    UiTextureHandle gameViewDescriptor = UiTextureHandle::Invalid;

    // Same for the Scene view, read by ScenePanel.
    UiTextureHandle sceneViewDescriptor = UiTextureHandle::Invalid;

    // Content size in pixels of the Game panel last frame; the Game view texture is resized to it next frame.
    Extent2D desiredExtent{};

    // Same for the Scene panel.
    Extent2D desiredSceneExtent{};

    // A Scene camera drag that started over the image keeps going until the button is released.
    bool sceneCameraPanning = false;
    bool sceneCameraRotating = false;

    // The only gate for Hierarchy and Project selection changes (see Selection.h).
    Selection selection;

    // Transform gizmo mode chosen in the Scene panel switcher.
    GizmoOperation gizmoOperation = GizmoOperation::Translate;

    // Set by File > Exit; read once per frame by IEditorLayer::WantsExit().
    bool exitRequested = false;

    // Colored status line for File > Save/Open Scene. Empty means nothing to show.
    std::string sceneIoStatusMessage;
    bool sceneIoStatusIsError = false;
    std::chrono::steady_clock::time_point sceneIoStatusSetTime;

    // True once the first frame decided whether the default dock layout is needed.
    bool dockLayoutEnsured = false;

    // Height in pixels of the Inspector's bottom texture viewer; kept across selections.
    float inspectorPreviewHeight = 260.0f;

    // True while gameplay is paused from the toolbar.
    bool playbackPaused = false;

    // True for the one frame after "Step" is clicked while paused.
    bool stepOneFrameRequested = false;

    // Regime the Render Graph window shows; shared by its toolbar and HTTP.
    RenderGraphRegimeChoice renderGraphDisplayedRegime = RenderGraphRegimeChoice::Present;

    // Set when a pass toggle changed this frame; read and cleared by the Frame Debugger.
    bool renderPassToggleRegistryChangedThisFrame = false;

    // True while that dialog is open. Set by the menu or a button, cleared by the dialog.
    bool newProjectWindowOpen = false;
    bool openProjectWindowOpen = false;
    bool createAssetWindowOpen = false;

    // Kind the Create Asset dialog scaffolds. 0 is RenderPass, the first enumerator of AssetScaffoldKind.
    AssetScaffoldKind createAssetWindowPendingKind = static_cast<AssetScaffoldKind>(0);

    // Colored status line for Project menu actions. Empty means nothing to show.
    std::string projectWorkflowStatusMessage;
    bool projectWorkflowStatusIsError = false;
    std::chrono::steady_clock::time_point projectWorkflowStatusSetTime;

    // The editor's cluster build runner. Owned by the editor layer; never null in the ImGui editor.
    MeshClusterBuildRunner* clusterBuildRunner = nullptr;

    // Persistent Inspector state of the cluster and normals tools.
    ClusterToolUiState clusterTool;

    // Mesh and pipeline shared by spawned instance grids.
    InstanceGridResources instanceGrid;

    // Mesh asset path (UTF-8) the Cluster Viewer should open; consumed by ClusterViewerWindow::OnFrame().
    std::string pendingClusterViewerPath;

    // Camera overlay state of the Scene panel's last frame.
    SceneCameraFrame sceneCamera;

    // Hierarchy panel state that outlives one frame.
    HierarchyViewState hierarchy;

    // What the Hi-Z demo scene changed, and the menu action waiting for the host (run on the main thread).
    HizDemoState hizDemo;
    HizDemoMenuAction hizDemoMenuAction = HizDemoMenuAction::None;
    HizDemoMenuResult hizDemoMenuResult;

    // Euler angles being edited in the Inspector's Rotation field.
    RotationEulerEditCache rotationEdit;

    // What that field drew last frame, for GET /inspector_state.
    RotationFieldProbe rotationProbe;
};

} // namespace gte
