#include "ScenePanel.h"

#include "../CameraFrustumOverlay.h"
#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../SceneCameraFrustum.h"
#include "../TransformGizmo.h"
#include "../ViewCullOverlay.h"
#include "ScenePanelCullView.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Core/FeatureKit/ViewCullLink.h"
#include "../../Game/Game.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

namespace {

constexpr std::size_t kMaxSceneCameras = 256;

// Valid Camera entities as pick candidates, with the lens aspect of the Game view.
void GatherCameraCandidates(Registry& registry, float aspect, std::vector<CameraPickCandidate>& out)
{
    ComponentStorage<Camera>& cameras = registry.Storage<Camera>();
    for (std::size_t i = 0; i < cameras.Size() && out.size() < kMaxSceneCameras; ++i) {
        const Entity entity = cameras.EntityAt(i);
        const FrustumInput input = MakeSceneCameraFrustum(registry, entity, aspect);
        if (FrustumInputIsValid(input)) {
            out.push_back({ SceneCameraEntityId(entity), input });
        }
    }
}

// Picks a camera with the left button and draws every camera body plus the selected frustum over the Scene image.
void BuildCameraOverlay(
    Registry& registry, EditorContext& ctx, const EditorCamera& camera, ImVec2 imageMin, ImVec2 imageSize, bool hovered)
{
    SceneCameraFrame& frame = ctx.sceneCamera;
    frame.rendered = false;
    if (imageSize.x < 1.0f || imageSize.y < 1.0f) {
        return;
    }

    const float gameAspect = GameViewAspect(ctx);
    frame.rectMin = Vec2(imageMin.x, imageMin.y);
    frame.rectMax = Vec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
    frame.viewProjection = camera.ViewProjection(imageSize.x / imageSize.y);
    frame.candidates.clear();
    GatherCameraCandidates(registry, gameAspect, frame.candidates);

    const Entity selected = ctx.selection.SelectedEntity();
    const bool cameraSelected = ctx.selection.Kind() == InspectorSelectionKind::Entity && registry.IsAlive(selected)
        && registry.HasComponent<Camera>(selected);
    frame.selectedId = cameraSelected ? SceneCameraEntityId(selected) : 0;
    frame.rendered = true;

    // Hover and click need a free cursor: no widget under it and no gizmo hot.
    std::optional<CameraPickHit> hover;
    if (hovered && !ImGui::IsAnyItemHovered() && !IsGizmoOver() && !IsGizmoUsing()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        hover = PickCamera(frame.candidates, frame.viewProjection, frame.rectMin, frame.rectMax, Vec2(mouse.x, mouse.y));
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const Entity picked = SceneCameraEntityFromId(hover->id);
            if (registry.IsAlive(picked)) {
                ctx.selection.SelectEntity(picked);
                frame.selectedId = hover->id;
            }
        }
    }

    SceneOverlayInput overlay;
    overlay.candidates = frame.candidates;
    overlay.selectedId = frame.selectedId;
    if (hover) {
        overlay.hoverId = hover->id;
    }
    overlay.viewProjection = frame.viewProjection;
    overlay.cull = ctx.viewCull ? &ctx.viewCull->report : nullptr;

    std::vector<OverlaySegment2D> segments;
    std::vector<OverlayQuad2D> quads;
    AppendSceneOverlay(overlay, frame.rectMin, frame.rectMax, segments, quads);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawOverlayQuads(drawList, quads, frame.rectMin, frame.rectMax);
    DrawOverlaySegments(drawList, segments, frame.rectMin, frame.rectMax);
}

} // namespace

void BuildScenePanel(
    Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    Registry& registry = game.GetRegistry();
    // Begin() is false when the panel is hidden (inactive tab or collapsed); the flag is read next frame
    // so a hidden Scene view is not rendered into.
    const bool isVisible =
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ctx.sceneViewVisible = isVisible;
    ctx.sceneCamera.rendered = false;

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

            // Camera bodies, the selected frustum and left-click picking; before the gizmo so the gizmo paints on top.
            // Cull view widgets come first so they block camera picking under them.
            const bool overCullView = BuildScenePanelCullView(ctx, imageMin, imageSize);
            BuildCameraOverlay(registry, ctx, camera, imageMin, imageSize, hovered && !overCullView);

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
