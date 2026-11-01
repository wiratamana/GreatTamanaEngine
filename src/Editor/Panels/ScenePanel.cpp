#include "ScenePanel.h"

#include "OverlayLabel.h"
#include "SceneViewGizmo.h"
#include "../CameraFrustumOverlay.h"
#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../EditorFrame.h"
#include "../SceneCameraFrustum.h"
#include "../SceneOverlay.h"
#include "../TransformGizmo.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Game/Game.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

namespace {

constexpr std::size_t kMaxSceneCameras = 256;

// True when an enabled feature is switched off for the secondary (Scene) view.
bool AnyFeatureOffInSceneView(const std::vector<RenderFeatureDebugEntry>& entries)
{
    return std::any_of(entries.begin(), entries.end(),
        [](const RenderFeatureDebugEntry& e) { return e.enabled && !e.enabledSecondary; });
}

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

    std::vector<OverlaySegment2D> segments;
    std::vector<OverlayQuad2D> quads;
    AppendSceneOverlay(overlay, frame.rectMin, frame.rectMax, segments, quads);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawOverlayQuads(drawList, quads, frame.rectMin, frame.rectMax);
    DrawOverlaySegments(drawList, segments, frame.rectMin, frame.rectMax);
}

} // namespace

int ScenePanel::ExtraFlags() const
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void ScenePanel::OnFrame(EditorFrame& frame)
{
    frame.ctx.sceneCamera.rendered = false;
}

void ScenePanel::OnGui(EditorFrame& frame)
{
    EditorContext& ctx = frame.ctx;
    Game& game = frame.game;
    EditorCamera& camera = m_camera;
    Registry& registry = game.GetRegistry();
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries = frame.renderFeatureEntries;
#if GTE_ENABLE_PROJECT_PANEL
    Renderer& renderer = frame.renderer;
    const DescriptorSetLayoutHandle sceneServicesSetLayout = frame.sceneServicesSetLayout;
#endif

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f || avail.y < 1.0f) {
        return;
    }

    // Scene view's own extent, applied next frame; independent of the Game view's ctx.desiredExtent.
    ctx.desiredSceneExtent.width = static_cast<std::uint32_t>(avail.x);
    ctx.desiredSceneExtent.height = static_cast<std::uint32_t>(avail.y);

    // Scene view has its own RenderTexture (ctx.sceneViewDescriptor).
    ImGui::Image(static_cast<ImTextureID>(static_cast<std::uint64_t>(ctx.sceneViewDescriptor)), avail);

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

    // On-screen rect of the Scene image; the gizmos and switcher are placed against it, not the window.
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageSize = ImGui::GetItemRectSize();

    // The labels show only while a feature is really off in this view.
    const bool previewOff = AnyFeatureOffInSceneView(renderFeatureEntries);
    if (previewOff) {
        DrawViewOverlayLabel(imageMin, imageSize, "Scene view: fast preview", 0, kSceneViewGizmoReservedWidth);
    }
    if (ctx.hizDemo.active && ctx.hizDemo.geometryOnly) {
        DrawViewOverlayLabel(imageMin, imageSize, kHizDemoOverlayLabel, previewOff ? 1 : 0, kSceneViewGizmoReservedWidth);
    }

    // A middle or right drag starts only over the image, then follows the cursor until the button is released.
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
    EditorCameraInput cameraInput;
    cameraInput.mouseDelta = Vec2{ io.MouseDelta.x, io.MouseDelta.y };
    cameraInput.scrollNotches = hovered ? io.MouseWheel : 0.0f; // The wheel only zooms while the cursor is on the image.
    cameraInput.deltaSeconds = io.DeltaTime;
    cameraInput.viewportHeightPixels = imageSize.y;
    cameraInput.panning = ctx.sceneCameraPanning;
    cameraInput.looking = ctx.sceneCameraRotating;
    camera.Update(cameraInput);

    ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
    DrawGizmoOperationSwitcher(ctx.gizmoOperation);

    // The view gizmo comes before the camera pick: its item blocks picking under it.
    const SceneViewGizmoResult viewGizmo = DrawSceneViewGizmo(imageMin, imageSize, camera);
    if (viewGizmo.alignTo.has_value()) {
        camera.AlignTo(*viewGizmo.alignTo);
    }
    if (viewGizmo.toggleProjection) {
        camera.ToggleProjectionMode();
    }
    BuildCameraOverlay(registry, ctx, camera, imageMin, imageSize, hovered);

    // Transform gizmo of the selected entity, drawn with the Scene camera.
    const Entity selectedEntity = ctx.selection.SelectedEntity();
    if (registry.IsAlive(selectedEntity)) {
        if (Transform* transform = registry.TryGetComponent<Transform>(selectedEntity)) {
            const float aspect = imageSize.y > 0.0f ? (imageSize.x / imageSize.y) : 1.0f;
            const Mat4 parentWorld = (transform->parent.IsValid() && registry.IsAlive(transform->parent))
                ? ComputeWorldMatrix(registry, transform->parent)
                : Mat4::Identity();
            ManipulateTransformGizmo(ctx.gizmoOperation, camera.View(), camera.GizmoProjection(aspect), imageMin.x,
                imageMin.y, imageSize.x, imageSize.y, *transform, parentWorld, camera.IsOrthographic());
        }
    }
}

} // namespace gte
