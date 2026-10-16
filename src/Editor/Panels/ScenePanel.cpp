#include "ScenePanel.h"

#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../TransformGizmo.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Game/Game.h"

#include <imgui.h>

#include <cstdint>
#include <string>

namespace gte {

void BuildScenePanel(
    Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    Registry& registry = game.GetRegistry();
    // Begin() is false when the panel is hidden (inactive tab or collapsed); the flag is read next frame
    // so a hidden Scene view is not rendered into.
    const bool isVisible =
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ctx.sceneViewVisible = isVisible;

    if (isVisible) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            // Scene view's own extent, applied next frame; independent of the Game view's ctx.desiredExtent.
            ctx.desiredSceneExtent.width = static_cast<std::uint32_t>(avail.x);
            ctx.desiredSceneExtent.height = static_cast<std::uint32_t>(avail.y);

            // Scene view has its own RenderTexture (ctx.sceneViewDescriptor).
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<std::uint64_t>(ctx.sceneViewDescriptor)),
                avail);

#if GTE_ENABLE_PROJECT_PANEL
            // Drop target for Project-panel *.gta Mesh assets: instantiates the dropped asset.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
                    const std::string absolutePath(static_cast<const char*>(payload->Data));
                    const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath, sceneServicesSetLayout);
                    if (spawned.IsValid()) {
                        ctx.selection.SelectEntity(spawned);
                    }
                }
                ImGui::EndDragDropTarget();
            }
#endif

            // On-screen rect of the Scene image; the gizmo and switcher are placed against it, not the window.
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            const ImVec2 imageSize = ImGui::GetItemRectSize();

            // Camera controls: a middle/right drag starts only while hovering the image, then keeps
            // responding until the button is released, even if the cursor leaves the image.
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
            // Wheel only dollies while hovering the image, so scrolling neighbouring panels leaves the camera alone.
            const float scrollDelta = hovered ? io.MouseWheel : 0.0f;

            camera.Update(mouseDelta, scrollDelta, ctx.sceneCameraPanning, ctx.sceneCameraRotating);

            // Move/Rotate/Scale switcher overlay; drawn after the hover check (so IsItemHovered() still refers
            // to the image) and before the gizmo (so the gizmo paints on top).
            ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
            DrawGizmoOperationSwitcher(ctx.gizmoOperation);

            // Gizmo for the selected entity; needs a live entity with a Transform.
            // Uses this Scene camera's view/projection, not the gameplay camera's.
            const Entity selectedEntity = ctx.selection.SelectedEntity();
            if (registry.IsAlive(selectedEntity)) {
                if (Transform* transform = registry.TryGetComponent<Transform>(selectedEntity)) {
                    const float aspect = imageSize.y > 0.0f ? (imageSize.x / imageSize.y) : 1.0f;
                    // The gizmo works in world space, so pass the parent's world matrix (identity for roots).
                    const Mat4 parentWorld = (transform->parent.IsValid() && registry.IsAlive(transform->parent))
                        ? ComputeWorldMatrix(registry, transform->parent)
                        : Mat4::Identity();
                    ManipulateTransformGizmo(ctx.gizmoOperation, camera.View(), camera.GizmoProjection(aspect),
                        imageMin.x, imageMin.y, imageSize.x, imageSize.y, *transform, parentWorld);
                }
            }
        }
    }
    ImGui::End();
}

} // namespace gte
