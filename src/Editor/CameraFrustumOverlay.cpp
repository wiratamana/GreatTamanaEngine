#include "CameraFrustumOverlay.h"

#include <imgui.h>

namespace gte {

void AppendCameraOverlay(const FrustumInput& camera, bool fullFrustum, const Mat4& viewProjection, Vec2 rectMin,
    Vec2 rectMax, OverlayColor color, float width, std::vector<OverlaySegment2D>& out)
{
    if (!FrustumInputIsValid(camera)) {
        return;
    }
    std::vector<FrustumSegment> segments = BuildFrustumSegments(ComputeFrustumCorners(camera));
    if (!fullFrustum) {
        std::erase_if(segments, [](const FrustumSegment& s) {
            return s.kind != FrustumEdgeKind::Apex && s.kind != FrustumEdgeKind::Arrow;
        });
    }
    ProjectSegments(segments, viewProjection, rectMin, rectMax, color, width, out);
}

void DrawOverlaySegments(ImDrawList* drawList, std::span<const OverlaySegment2D> segments, Vec2 clipMin, Vec2 clipMax)
{
    if (drawList == nullptr || segments.empty()) {
        return;
    }
    drawList->PushClipRect(ImVec2(clipMin.x, clipMin.y), ImVec2(clipMax.x, clipMax.y), true);
    for (const OverlaySegment2D& s : segments) {
        drawList->AddLine(ImVec2(s.a.x, s.a.y), ImVec2(s.b.x, s.b.y), IM_COL32(s.rgba[0], s.rgba[1], s.rgba[2], s.rgba[3]), s.width);
    }
    drawList->PopClipRect();
}

} // namespace gte
