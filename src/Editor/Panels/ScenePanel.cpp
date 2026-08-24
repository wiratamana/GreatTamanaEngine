#include "ScenePanel.h"

#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../TransformGizmo.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../Game/Game.h"

#include <imgui.h>

#include <cstdint>
#include <string>

namespace gte {

void BuildScenePanel(Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera)
{
    Registry& registry = game.GetRegistry();
    // ImGui::Begin() returns false when this panel isn't actually visible
    // right now (e.g. it's an inactive tab behind "Game", or collapsed) -
    // stashed in ctx.sceneViewVisible for
    // ImGuiEditorLayer::SceneViewTarget() to read at the START of NEXT
    // frame, same "act on it next frame" pattern GamePanel.cpp uses for
    // ctx.gameViewVisible/desiredExtent - so a hidden Scene view skips
    // being rendered into at all next frame, see
    // EditorContext::sceneViewVisible.
    const bool isVisible =
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ctx.sceneViewVisible = isVisible;

    if (isVisible) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            // Own extent (ctx.desiredSceneExtent), applied by
            // ImGuiEditorLayer::SceneViewTarget() next frame - kept
            // completely separate from ctx.desiredExtent (GamePanel's),
            // so resizing/splitting "Scene" never resizes the Game-view
            // RenderTexture, and vice versa.
            ctx.desiredSceneExtent.width = static_cast<std::uint32_t>(avail.x);
            ctx.desiredSceneExtent.height = static_cast<std::uint32_t>(avail.y);

            // Its own RenderTexture now (ctx.sceneViewDescriptor) - "Scene"
            // no longer displays the same image as "Game".
            ImGui::Image(
                static_cast<ImTextureID>(reinterpret_cast<intptr_t>(ctx.sceneViewDescriptor)),
                avail);

#if GTE_ENABLE_PROJECT_PANEL
            // Same Project-panel-asset drag-and-drop target as
            // Panels/HierarchyPanel.cpp, attached to the Scene image itself
            // instead - lets a *.gta Mesh asset be dropped straight into the
            // 3D viewport to instantiate it.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
                    const std::string absolutePath(static_cast<const char*>(payload->Data));
                    const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath);
                    if (spawned.IsValid()) {
                        ctx.selection.SelectEntity(spawned);
                    }
                }
                ImGui::EndDragDropTarget();
            }
#endif

            // The Scene image's own on-screen pixel rect - both the gizmo
            // (ManipulateTransformGizmo()) and its top-left switcher overlay
            // (DrawGizmoOperationSwitcher()) below are positioned/clipped
            // against this, never against the whole "Scene" window (which
            // also includes its title bar/tab strip).
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            const ImVec2 imageSize = ImGui::GetItemRectSize();

            // Unity-style Scene camera controls (see EditorCamera.h for the
            // actual pan/rotate/dolly math, deliberately kept ImGui-free) -
            // this is the one place that reads ImGui's own mouse state and
            // feeds it in as plain values.
            //
            // Middle/right-mouse drags only ever START responding while the
            // cursor is actually hovering the Scene image itself (so
            // clicking/dragging elsewhere in the Editor never moves this
            // camera), but once started, they keep responding even if the
            // cursor drifts outside the image mid-drag - ctx.sceneCameraPanning/
            // Rotating "captures" this across frames - ending only once the
            // corresponding button is released, exactly like Unity's own
            // Scene view (and like a normal OS drag gesture in general).
            const bool hovered = ImGui::IsItemHovered();

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                ctx.sceneCameraPanning = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                ctx.sceneCameraPanning = false;
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ctx.sceneCameraRotating = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                ctx.sceneCameraRotating = false;
            }

            const ImGuiIO& io = ImGui::GetIO();
            const Vec2 mouseDelta{ io.MouseDelta.x, io.MouseDelta.y };
            // The wheel only dollies the camera while actually hovering the
            // Scene image - otherwise scrolling e.g. the Hierarchy list
            // sitting right next to it would also dolly this camera.
            const float scrollDelta = hovered ? io.MouseWheel : 0.0f;

            camera.Update(mouseDelta, scrollDelta, ctx.sceneCameraPanning, ctx.sceneCameraRotating);

            // Unity's own top-left Move/Rotate/Scale switcher, overlaid on
            // top of the Scene image - drawn AFTER the camera-control hover
            // check above (so `hovered`/IsItemHovered() above still refers
            // to the Image() item, never one of these buttons), but BEFORE
            // the gizmo below so the gizmo's own handles/lines always paint
            // over it in the rare case they visually overlap.
            ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
            DrawGizmoOperationSwitcher(ctx.gizmoOperation);

            // The Unity-style translate/rotate/scale gizmo itself, for
            // whichever entity is currently selected in the Hierarchy
            // (ctx.selection.SelectedEntity()) - only drawn/manipulated when
            // it's alive and actually has a Transform to edit (e.g. nothing
            // selected yet, or the selected entity was destroyed
            // elsewhere). Uses THIS Scene camera's own view/projection
            // (never the gameplay Camera entity's - see EditorCamera.h), so
            // the gizmo always lines up with whatever this panel is
            // currently showing.
            const Entity selectedEntity = ctx.selection.SelectedEntity();
            if (registry.IsAlive(selectedEntity)) {
                if (Transform* transform = registry.TryGetComponent<Transform>(selectedEntity)) {
                    const float aspect = imageSize.y > 0.0f ? (imageSize.x / imageSize.y) : 1.0f;
                    ManipulateTransformGizmo(ctx.gizmoOperation, camera.View(), camera.GizmoProjection(aspect),
                        imageMin.x, imageMin.y, imageSize.x, imageSize.y, *transform);
                }
            }
        }
    }
    ImGui::End();
}

} // namespace gte
