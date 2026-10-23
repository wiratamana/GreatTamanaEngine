#include "SceneCameraAdapter.h"

#include "CameraFrustumOverlay.h"
#include "EditorContext.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Registry.h"
#include "../Renderer/Renderer.h"

#include <cmath>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr float kOverlayLineWidth = 1.5f;

// Name component value, else "Entity <index>" like the Hierarchy panel.
std::string CameraName(const Registry& registry, Entity entity)
{
    if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr && !name->value.empty()) {
        return name->value;
    }
    return "Entity " + std::to_string(entity.index);
}

bool InUnitRange(float v) noexcept
{
    return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
}

} // namespace

void SceneCameraAdapter::BeginFrame(
    Renderer& renderer, EditorContext& ctx, Registry& registry, RenderTexture& sceneView) noexcept
{
    m_renderer = &renderer;
    m_ctx = &ctx;
    m_registry = &registry;
    m_sceneView = &sceneView;
}

SceneCameraReport SceneCameraAdapter::Report() const
{
    SceneCameraReport report;
    if (m_ctx == nullptr || m_registry == nullptr || !m_ctx->sceneCamera.rendered) {
        return report;
    }
    const SceneCameraFrame& frame = m_ctx->sceneCamera;
    const Vec2 size = frame.rectMax - frame.rectMin;
    report.rendered = true;
    report.imageWidth = static_cast<int>(size.x);
    report.imageHeight = static_cast<int>(size.y);
    report.selectedEntity = frame.selectedId;

    for (const CameraPickCandidate& candidate : frame.candidates) {
        const FrustumInput& lens = candidate.frustum;
        SceneCameraInfo info;
        info.entityId = candidate.id;
        info.name = CameraName(*m_registry, SceneCameraEntityFromId(candidate.id));
        info.position[0] = lens.position.x;
        info.position[1] = lens.position.y;
        info.position[2] = lens.position.z;
        const Vec3 euler = lens.rotation.ToEulerDegrees();
        info.rotationEuler[0] = euler.x;
        info.rotationEuler[1] = euler.y;
        info.rotationEuler[2] = euler.z;
        info.fovYDegrees = lens.fovYDegrees;
        info.nearZ = lens.nearZ;
        info.farZ = lens.farZ;
        info.selected = candidate.id == frame.selectedId;

        Vec2 apex;
        if (ProjectToScreen(lens.position, frame.viewProjection, frame.rectMin, frame.rectMax, apex)) {
            info.apexScreenX = (apex.x - frame.rectMin.x) / size.x;
            info.apexScreenY = (apex.y - frame.rectMin.y) / size.y;
            info.apexVisible = InUnitRange(info.apexScreenX) && InUnitRange(info.apexScreenY);
        }
        if (info.selected) {
            AppendCameraOverlay(lens, true, frame.viewProjection, Vec2::Zero(), size, kSceneCameraSelectedColor,
                kOverlayLineWidth, report.selectedFrustum);
        }
        report.cameras.push_back(std::move(info));
    }
    return report;
}

SceneCameraResult SceneCameraAdapter::Select(const std::string& entityName)
{
    if (m_ctx == nullptr || m_registry == nullptr) {
        return { false, SceneCameraError::Unavailable, "the Scene panel has not run yet" };
    }
    ComponentStorage<Camera>& cameras = m_registry->Storage<Camera>();
    if (cameras.Size() == 0) {
        return { false, SceneCameraError::NoCamera, "the scene has no Camera entity" };
    }
    for (std::size_t i = 0; i < cameras.Size(); ++i) {
        const Entity entity = cameras.EntityAt(i);
        if (CameraName(*m_registry, entity) == entityName) {
            m_ctx->selection.SelectEntity(entity);
            return { true, SceneCameraError::None, "selected " + entityName };
        }
    }
    return { false, SceneCameraError::NotFound, "no Camera entity is named '" + entityName + "'" };
}

SceneCameraPickResult SceneCameraAdapter::Pick(float x, float y)
{
    SceneCameraPickResult result;
    if (m_ctx == nullptr || m_registry == nullptr) {
        result.error = SceneCameraError::Unavailable;
        result.message = "the Scene panel has not run yet";
        return result;
    }
    if (!InUnitRange(x) || !InUnitRange(y)) {
        result.error = SceneCameraError::BadInput;
        result.message = "x and y must be in 0..1";
        return result;
    }
    const SceneCameraFrame& frame = m_ctx->sceneCamera;
    if (!frame.rendered) {
        result.error = SceneCameraError::NotRendered;
        result.message = "the Scene view was not rendered";
        return result;
    }

    const Vec2 size = frame.rectMax - frame.rectMin;
    const Vec2 cursor = frame.rectMin + Vec2(x * size.x, y * size.y);
    result.ok = true;
    if (const std::optional<CameraPickHit> hit =
            PickCamera(frame.candidates, frame.viewProjection, frame.rectMin, frame.rectMax, cursor)) {
        const Entity entity = SceneCameraEntityFromId(hit->id);
        if (m_registry->IsAlive(entity)) {
            m_ctx->selection.SelectEntity(entity);
            result.hit = true;
            result.entityName = CameraName(*m_registry, entity);
        }
    }
    return result;
}

std::optional<ClusterViewerImage> SceneCameraAdapter::CaptureImage(bool overlay)
{
    if (m_renderer == nullptr || m_ctx == nullptr || m_sceneView == nullptr || !m_ctx->sceneCamera.rendered) {
        return std::nullopt;
    }
    // The previous frame may still be in flight; a debug capture can afford the stall.
    m_renderer->WaitForGpuIdle();
    ClusterViewerImage image;
    image.pixels = m_renderer->CaptureRenderTexturePixels(*m_sceneView);
    if (image.pixels.pixels.empty() || image.pixels.width < 1 || image.pixels.height < 1) {
        return std::nullopt;
    }
    if (overlay) {
        const SceneCameraFrame& frame = m_ctx->sceneCamera;
        const Vec2 imageMax(static_cast<float>(image.pixels.width), static_cast<float>(image.pixels.height));
        for (const CameraPickCandidate& candidate : frame.candidates) {
            const bool isSelected = candidate.id == frame.selectedId;
            AppendCameraOverlay(candidate.frustum, isSelected, frame.viewProjection, Vec2::Zero(), imageMax,
                isSelected ? kSceneCameraSelectedColor : kSceneCameraBodyColor, kOverlayLineWidth, image.overlay);
        }
    }
    return image;
}

} // namespace gte
