#pragma once

#include "Selection.h"
#include "TransformGizmo.h"

#include <string>

#include <volk.h>

namespace gte {

// Plain shared state passed by reference into every Editor panel/dock-layout
// function (see DockLayout.h, Panels/*.h) - the free-function equivalent of
// what used to be ImGuiEditorLayer's own private member variables before the
// panel split (see AGENTS.md, "Editor Module Structure"). Deliberately plain
// data with no behavior of its own, the same "plain data, no virtual
// behavior" philosophy AGENTS.md already applies to ECS components (see
// ECS/Components/Transform.h) - only ImGuiEditorLayer (the composition root)
// and the panel/dock-layout functions it calls ever touch this, in a fixed,
// explicit order every frame.
//
// Holding raw Vulkan types here (VkDescriptorSet, VkExtent2D) is deliberate,
// not an architectural leak: Renderer's own public API
// (RenderTexture::Extent(), Renderer::GetVulkanContextInfo()) already hands
// out plain Vulkan handles on purpose, specifically so "an external
// Vulkan-based rendering backend... owned by the Editor module" (see
// Renderer.h) - i.e. Dear ImGui's own Vulkan backend - can use them
// directly. See AGENTS.md ("Editor Module Structure") for the full
// rationale.
struct EditorContext {
    // The ImGui-side descriptor for the Game-view RenderTexture, created
    // lazily by ImGuiEditorLayer::BuildUI() - read by GamePanel only. Never
    // interpreted by engine code, just handed straight to ImGui::Image() as
    // an opaque texture id.
    VkDescriptorSet gameViewDescriptor = VK_NULL_HANDLE;

    // The Scene-view equivalent of gameViewDescriptor above, for its own,
    // separate RenderTexture (see ImGuiEditorLayer's class comment and
    // EditorLayer.h's SceneViewTarget()) - read by ScenePanel only. "Game"
    // and "Scene" each display their own texture now; they no longer share
    // one.
    VkDescriptorSet sceneViewDescriptor = VK_NULL_HANDLE;

    // Size (in pixels) the "Game" panel's content region actually was as of
    // last frame's GamePanel::Build() (Panels/GamePanel.cpp) - what
    // ImGuiEditorLayer::GameViewTarget() resizes the Game-view RenderTexture
    // to at the start of the next frame. Initialized by ImGuiEditorLayer's
    // constructor to the OS window's startup size, so the very first frame
    // (before BuildUI() has ever run) doesn't see a spurious mismatch
    // against the texture's own initial size.
    VkExtent2D desiredExtent{};

    // The Scene-view equivalent of desiredExtent above, written by
    // ScenePanel::Build() and applied by
    // ImGuiEditorLayer::SceneViewTarget() - kept completely independent of
    // desiredExtent so "Game" and "Scene" can each be resized/split to a
    // different size without affecting the other's RenderTexture.
    VkExtent2D desiredSceneExtent{};

    // True if the "Game"/"Scene" panel's ImGui::Begin() call actually
    // returned true last frame (Panels/GamePanel.cpp/ScenePanel.cpp) - i.e.
    // the panel is genuinely visible this frame (an active, undocked, or
    // split-open tab), as opposed to an inactive tab hidden behind the
    // other one. Read by ImGuiEditorLayer::GameViewTarget()/
    // SceneViewTarget() to decide whether to bother resizing/returning that
    // RenderTexture at all this frame - Application then skips the matching
    // Renderer::RenderOffscreen() call entirely when the target comes back
    // nullptr, so a hidden panel costs nothing to render. Initialized to
    // true so the very first frame (before BuildUI() has ever run) still
    // renders both views, same "safe default, corrected next frame" pattern
    // as desiredExtent above. If "Scene" and "Game" are tabbed together,
    // exactly one of these two is ever true at a time; if the user splits
    // them apart, both are true simultaneously.
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
};

} // namespace gte
