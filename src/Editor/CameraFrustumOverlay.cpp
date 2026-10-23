#include "CameraFrustumOverlay.h"

#include <imgui.h>

#include <cmath>

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

void AppendPlaneArrowOverlay(const FrustumInput& camera, std::span<const Plane> planes, const Mat4& viewProjection,
    Vec2 rectMin, Vec2 rectMax, OverlayColor color, float width, std::vector<OverlaySegment2D>& out,
    std::vector<OverlayLabel2D>* labels)
{
    if (!FrustumInputIsValid(camera)) {
        return;
    }
    const FrustumCorners corners = ComputeFrustumCorners(camera);
    std::vector<FrustumSegment> segments;
    segments.reserve(planes.size() * 3);
    for (const FrustumPlaneArrow& arrow : BuildFrustumPlaneArrows(planes, corners)) {
        const Vec3 shaft = arrow.to - arrow.from;
        const float length = Length(shaft);
        if (!(length > 0.0f)) {
            continue;
        }
        const Vec3 dir = shaft * (1.0f / length);
        const Vec3 helper = std::fabs(Dot(dir, corners.up)) < 0.9f ? corners.up : corners.right;
        const Vec3 barb = Normalize(Cross(dir, helper)) * (length * 0.125f);
        const Vec3 back = arrow.to - dir * (length * 0.25f);
        segments.push_back({ arrow.from, arrow.to, FrustumEdgeKind::Arrow });
        segments.push_back({ arrow.to, back + barb, FrustumEdgeKind::Arrow });
        segments.push_back({ arrow.to, back - barb, FrustumEdgeKind::Arrow });

        Vec2 tip;
        if (labels != nullptr && ProjectToScreen(arrow.to, viewProjection, rectMin, rectMax, tip)) {
            labels->push_back({ tip, arrow.name, color });
        }
    }
    ProjectSegments(segments, viewProjection, rectMin, rectMax, color, width, out);
}

void DrawOverlayLabels(ImDrawList* drawList, std::span<const OverlayLabel2D> labels, Vec2 clipMin, Vec2 clipMax)
{
    if (drawList == nullptr || labels.empty()) {
        return;
    }
    drawList->PushClipRect(ImVec2(clipMin.x, clipMin.y), ImVec2(clipMax.x, clipMax.y), true);
    for (const OverlayLabel2D& l : labels) {
        drawList->AddText(ImVec2(l.pos.x + 4.0f, l.pos.y - 6.0f), IM_COL32(l.rgba[0], l.rgba[1], l.rgba[2], l.rgba[3]), l.text);
    }
    drawList->PopClipRect();
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
