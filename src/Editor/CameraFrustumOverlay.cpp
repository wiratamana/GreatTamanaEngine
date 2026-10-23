#include "CameraFrustumOverlay.h"

#include <imgui.h>

namespace gte {

void AppendCameraOverlay(const FrustumInput& camera, bool fullFrustum, const Mat4& viewProjection, Vec2 rectMin,
    Vec2 rectMax, OverlayColor color, float width, std::vector<OverlaySegment2D>& out,
    std::vector<OverlayQuad2D>* farFill)
{
    if (!FrustumInputIsValid(camera)) {
        return;
    }
    const FrustumCorners corners = ComputeFrustumCorners(camera);
    std::vector<FrustumSegment> segments = BuildFrustumSegments(corners);
    if (!fullFrustum) {
        std::erase_if(segments, [](const FrustumSegment& s) {
            return s.kind != FrustumEdgeKind::Apex && s.kind != FrustumEdgeKind::Arrow;
        });
    }
    ProjectSegments(segments, viewProjection, rectMin, rectMax, color, width, out);
    if (fullFrustum && farFill != nullptr) {
        ProjectQuad(std::span<const Vec3, 4>(corners.farC), viewProjection, rectMin, rectMax,
            { color[0], color[1], color[2], kFarFillAlpha }, *farFill);
    }
}

void DrawOverlayQuads(ImDrawList* drawList, std::span<const OverlayQuad2D> quads, Vec2 clipMin, Vec2 clipMax)
{
    if (drawList == nullptr || quads.empty()) {
        return;
    }
    drawList->PushClipRect(ImVec2(clipMin.x, clipMin.y), ImVec2(clipMax.x, clipMax.y), true);
    for (const OverlayQuad2D& q : quads) {
        const ImVec2 points[4] = { ImVec2(q.p[0].x, q.p[0].y), ImVec2(q.p[1].x, q.p[1].y), ImVec2(q.p[2].x, q.p[2].y),
            ImVec2(q.p[3].x, q.p[3].y) };
        drawList->AddConvexPolyFilled(points, 4, IM_COL32(q.rgba[0], q.rgba[1], q.rgba[2], q.rgba[3]));
    }
    drawList->PopClipRect();
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
