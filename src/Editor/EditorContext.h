#pragma once

#include "CameraPick.h"
#include "ClusterToolUiState.h"
#include "InstanceGridTestSpawner.h"
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

// Plain shared state passed by reference into every Editor panel and dock-layout function.
// Only ImGuiEditorLayer and the panel functions it calls touch it, in a fixed order every frame.
struct EditorContext {
    // ImGui-side texture handle of the Game-view texture; created lazily by BuildUI(), read by GamePanel.
    UiTextureHandle gameViewDescriptor = UiTextureHandle::Invalid;

    // Same for the Scene view, read by ScenePanel.
    UiTextureHandle sceneViewDescriptor = UiTextureHandle::Invalid;

    // Size in pixels of the "Game" panel content last frame; the Game-view texture is resized to it next frame.
    Extent2D desiredExtent{};

    // Same for the "Scene" panel, independent of the Game view.
    Extent2D desiredSceneExtent{};

    // True when the panel's ImGui::Begin() returned true last frame (the panel is visible).
    // A hidden view is not rendered. Starts true so the first frame renders both.
    bool gameViewVisible = true;
    bool sceneViewVisible = true;

    // Whether a Scene-view camera drag (pan/rotate - see EditorCamera.h) is
    // currently "captured": it started with the mouse actually pressed down
    // while hovering the Scene panel's image (ScenePanel.cpp), so it keeps
    // responding to mouse movement even if the cursor later drifts outside
    // the image mid-drag, ending only once the corresponding button is
    // released - exactly like Unity's own Scene view camera controls (and
    // like a normal OS drag/resize gesture in general). Written/read
    // entirely by ScenePanel.cpp; EditorCamera itself never touches ImGui
    // and has no idea this exists.
    bool sceneCameraPanning = false;
    bool sceneCameraRotating = false;

    // The single gate-keeper for every Hierarchy-entity / Project-asset
    // selection in the Editor (see Selection.h) - HierarchyPanel/
    // ProjectPanel only ever change what's selected through this object's
    // own methods (SelectEntity()/SelectAsset()/ClearAssetIfPath()), never
    // by assigning fields directly, so there is exactly one place in the
    // codebase that ever writes "the selection changed" - useful right now
    // for InspectorPanel/ScenePanel to read back, and set up specifically
    // so a future Command-pattern implementation (undo-able selection
    // changes) has one obvious choke point to route through instead of
    // reinventing its own.
    Selection selection;

    // Which gizmo manipulation ScenePanel's top-left Move/Rotate/Scale
    // switcher (TransformGizmo.h's DrawGizmoOperationSwitcher()) is
    // currently set to, and therefore what ManipulateTransformGizmo() does
    // with the current Hierarchy selection's Transform this frame (see
    // Panels/ScenePanel.cpp). Translate is Unity's own default for a
    // freshly-opened scene.
    GizmoOperation gizmoOperation = GizmoOperation::Translate;

    // Set by File > Exit (see DockLayout.cpp's BuildDockspaceAndMenuBar());
    // read once per frame by ImGuiEditorLayer::WantsExit().
    bool exitRequested = false;

    // Short-lived, colored status feedback for File > Save Scene/Open Scene
    // (DockLayout.cpp's BuildDockspaceAndMenuBar(), Ctrl+S/Ctrl+O too) -
    // mirrors ProjectPanel's own private SetStatus()/m_statusMessage
    // pattern (Panels/ProjectPanel.cpp), just surfaced through EditorContext
    // instead, since DockLayout.cpp's BuildDockspaceAndMenuBar() is a free
    // function with no persistent members of its own to keep this in.
    // Empty means "nothing to show right now".
    std::string sceneIoStatusMessage;
    bool sceneIoStatusIsError = false;
    std::chrono::steady_clock::time_point sceneIoStatusSetTime;

    // Latches true the first time every one of Hierarchy/Inspector/Scene/
    // Game is confirmed to have a real dock (see DockLayout.cpp's
    // BuildDockspaceAndMenuBar() comment for the full one-shot rationale) -
    // once true, it never touches the docking system again for the rest of
    // the process, which is what lets the user freely drag/split/undock any
    // panel afterwards.
    bool dockLayoutEnsured = false;

    // Persisted (across frames AND across selection changes) pixel height
    // of the Inspector's Unity-style bottom "texture viewer" strip, when the
    // current Inspector selection is a previewable image/texture asset (see
    // Panels/InspectorPanel.cpp's BuildAssetInspector()). Deliberately lives
    // here rather than as InspectorPanel-local state, since InspectorPanel
    // is a stateless free function (see AGENTS.md, "Editor Module
    // Structure") - this is its one piece of genuinely cross-frame state,
    // the same role EditorContext already plays for every other
    // panel-persisted value above. Adjusted live by dragging the splitter
    // between the metadata list and the viewer; clamped to sane bounds
    // every frame in BuildAssetInspector() itself rather than here, since
    // the valid range depends on the Inspector panel's current on-screen
    // height, which only BuildAssetInspector() (mid-ImGui-layout) knows.
    float inspectorPreviewHeight = 260.0f;

    // frame-debugger-1 campaign (task_manager/frame-debugger-1/
    // PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - true whenever the
    // user has toggled gameplay simulation paused via the toolbar's
    // Pause/Resume button (see PlaybackControls.h). False by default - a
    // brand-new session behaves EXACTLY like before this campaign (nothing
    // paused, Game::Update() simulates normally every frame) until the user
    // explicitly pauses. Read once per frame by
    // Application::Run()/IEditorLayer::IsPlaybackPaused() - see PHASE4.
    bool playbackPaused = false;

    // True for exactly the one frame after the user clicks "Step" while
    // playbackPaused is already true - set by PlaybackControls.cpp's own
    // button handler, consumed (read-and-cleared) by
    // IEditorLayer::TryConsumeStepRequest() - see PHASE4. Clicking "Step"
    // while NOT already paused is a no-op (the button is rendered disabled
    // while playbackPaused is false - see PlaybackControls.cpp).
    bool stepOneFrameRequested = false;

    // task_manager/frame-debugger-2 campaign (PHASE2) - true whenever
    // the Editor's on-demand "Frame Debugger" floating window
    // (Panels/FrameDebuggerPanel.h) is currently open. Flipped directly
    // by DockLayout.cpp's checkable "Window > Frame Debugger" menu item
    // (ImGui::MenuItem's own 3-argument overload writes straight through
    // a bool*), and passed as ImGui::Begin()'s own p_open parameter by
    // FrameDebuggerPanel::Build() - so the window's titlebar [x] close
    // button keeps this exact same bool in sync automatically, in both
    // directions. False by default (closed on a fresh session) -
    // mirrors BoneViewerWindow's own default-closed-until-opened
    // behavior, just via a shared EditorContext bool instead of a
    // private member (see FrameDebuggerPanel.h's own class comment for
    // why).
    bool frameDebuggerWindowOpen = false;

    // True when the floating "Render Graph" window (Panels/RenderGraphPanel.h)
    // is open. Toggled by "Window > Render Graph" menu item and the window's
    // own titlebar close button. Defaults to true (always visible on launch).
    bool renderGraphWindowOpen = true;

    // Which regime the "Render Graph" panel shows. One shared field so a
    // human's toolbar click and an HTTP command always agree.
    RenderGraphRegimeChoice renderGraphDisplayedRegime = RenderGraphRegimeChoice::Present;

    // True when the floating "Render Features" window
    // (Panels/RenderFeaturesPanel.h) is open - GPU-driven-batch culling
    // readout + render-feature priority/enable authoring, split out of the
    // Render Graph panel. Toggled by "Window > Render Features" menu item
    // and the window's own titlebar close button. Defaults to false.
    bool renderFeaturesWindowOpen = false;

    // Set true in RenderGraphPanel::Build() when a before/after snapshot
    // diff (DidRenderPassToggleEnabledStatesChange) detects any pass toggle
    // checkbox flip this frame (Pass Tree, or Isolate Pass). Read-and-cleared
    // by FrameDebuggerPanel::Build() to trigger a fresh capture so a toggled
    // pass doesn't linger with stale debugger data.
    bool renderPassToggleRegistryChangedThisFrame = false;


    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - true whenever NewProjectWindow.h's floating
    // window is currently open. Flipped by the new "Project > New
    // Project..." menu item (DockLayout.cpp) and by the window's own
    // titlebar [x] close button (ImGui::Begin()'s p_open parameter keeps
    // both in sync automatically) - mirrors frameDebuggerWindowOpen's own
    // exact convention immediately above.
    bool newProjectWindowOpen = false;

    // editor-core-separation-17 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 3), PHASE4 - true whenever OpenProjectWindow.h's floating
    // window is currently open. Same open/close convention as
    // newProjectWindowOpen immediately above.
    bool openProjectWindowOpen = false;

    // editor-core-separation-18 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 4), PHASE2 - true whenever CreateAssetWindow.h's floating
    // window (PHASE3) is currently open. Set by ProjectPanel's own new
    // "[Active Project] <Name>" row's Create submenu (Panels/ProjectPanel.cpp);
    // read/cleared the same open/close way as newProjectWindowOpen/
    // openProjectWindowOpen above.
    bool createAssetWindowOpen = false;

    // Which AssetScaffoldKind CreateAssetWindow should scaffold - only
    // meaningful while createAssetWindowOpen is true; written at the same time
    // as createAssetWindowOpen, immediately before it's set true, never read
    // beforehand. Defaulted via static_cast<AssetScaffoldKind>(0) rather than
    // the enumerator name RenderPass itself, because EditorContext.h only
    // FORWARD-declares AssetScaffoldKind above (an incomplete type has no
    // visible enumerators yet) - 0 is correct PROVIDED RenderPass really is
    // declared first in Core/EditorCapabilities.h's own
    // `enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair };`
    // (confirmed - RenderPass is listed first, so its implicit value is 0). If
    // that enum's ordering is ever changed, this literal must be updated too.
    AssetScaffoldKind createAssetWindowPendingKind = static_cast<AssetScaffoldKind>(0); // RenderPass

    // Short-lived, colored status feedback shared by every "Project"-menu
    // action this whole 5-file plan adds (New Project this campaign; Open
    // Project/Compile in later campaigns) - mirrors sceneIoStatusMessage/
    // sceneIoStatusIsError/sceneIoStatusSetTime's own exact convention
    // above, just for a different family of actions, so a later campaign's
    // "Open Project" success/failure toast reuses this SAME field rather
    // than inventing a third status-toast mechanism.
    std::string projectWorkflowStatusMessage;
    bool projectWorkflowStatusIsError = false;
    std::chrono::steady_clock::time_point projectWorkflowStatusSetTime;

    // The editor's cluster build runner. Owned by the editor layer; never null in the ImGui editor.
    MeshClusterBuildRunner* clusterBuildRunner = nullptr;

    // Persistent Inspector state of the cluster and normals tools.
    ClusterToolUiState clusterTool;

    // Mesh and pipeline shared by spawned instance grids; releases them with the editor layer.
    InstanceGridResources instanceGrid;

    // Asset path (UTF-8) whose project record must be refreshed; set by a panel, consumed by the editor layer.
    std::string pendingAssetRecordRefresh;

    // Mesh asset path (UTF-8) the cluster viewer should open; set by a panel, consumed by the editor layer.
    std::string pendingClusterViewerPath;

    // Camera overlay state of the Scene panel's last frame.
    SceneCameraFrame sceneCamera;
};

} // namespace gte
