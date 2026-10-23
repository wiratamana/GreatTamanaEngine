#include "ViewCullOverlay.h"

namespace gte {

namespace {

constexpr float kBodyWidth = 1.5f;
constexpr float kHoverWidth = 2.5f;
constexpr float kOutlineWidth = 2.0f;
constexpr OverlayColor kHoverColor{ 255, 255, 255, 255 };

} // namespace

void AppendCullOutlineOverlay(const fx::ViewCullReport& report, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax,
    std::vector<OverlaySegment2D>& segments)
{
    if (!report.valid || !report.rendered || !report.frozen || !report.cullCameraValid) {
        return;
    }
    const std::optional<FrustumCorners> corners = FrustumCornersFromViewProjection(report.cullViewProjection);
    if (!corners) {
        return;
    }
    std::vector<FrustumSegment> edges = BuildFrustumSegments(*corners);
    std::erase_if(edges, [](const FrustumSegment& s) {
        return s.kind == FrustumEdgeKind::Apex || s.kind == FrustumEdgeKind::Arrow;
    });
    ProjectSegments(edges, viewProjection, rectMin, rectMax, kSceneCameraViewerColor, kOutlineWidth, segments);
}

void AppendSceneOverlay(const SceneOverlayInput& input, Vec2 rectMin, Vec2 rectMax, std::vector<OverlaySegment2D>& segments,
    std::vector<OverlayQuad2D>& quads)
{
    for (const CameraPickCandidate& candidate : input.candidates) {
        const bool isSelected = candidate.id == input.selectedId;
        const bool isHover = input.hoverId && *input.hoverId == candidate.id;
        const OverlayColor color = isSelected ? kSceneCameraSelectedColor : (isHover ? kHoverColor : kSceneCameraBodyColor);
        AppendCameraOverlay(candidate.frustum, isSelected, input.viewProjection, rectMin, rectMax, color,
            isHover ? kHoverWidth : kBodyWidth, segments, isSelected ? &quads : nullptr);
    }
    if (input.cull != nullptr) {
        AppendCullOutlineOverlay(*input.cull, input.viewProjection, rectMin, rectMax, segments);
    }
}

} // namespace gte
