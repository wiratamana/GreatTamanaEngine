#include "WaterGizmoOverlay.h"

#include "WaterGizmoMath.h"
#include "../../../Editor/TransformGizmo.h"
#include "../../../Editor/ViewportProjection.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr float kPickRadius = 11.0f;
constexpr float kInsertOutlineRadius = 12.0f;

ImU32 Color(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

void Report(WaterUiBridge& bridge, const water::WaterResult& r)
{
    bridge.status = r.message;
    bridge.statusIsError = !r.ok;
}

void DrawBody(ImDrawList* list, const water::WaterBodySet& set, std::size_t bodyIndex, bool selected, const Mat4& vp, Vec2 rectMin, Vec2 rectMax)
{
    const water::WaterBody& body = set.Bodies()[bodyIndex];
    const ImU32 line = selected ? Color(255, 235, 90) : Color(150, 215, 255, 200);
    const float width = selected ? 2.5f : 1.5f;
    auto project = [&](double x, double y, double z, Vec2& out) {
        return ProjectToScreen(Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)), vp, rectMin, rectMax, out);
    };
    if (body.kind == water::WaterBodyKind::Lake) {
        const std::size_t n = body.polygon.size();
        for (std::size_t i = 0; i < n; ++i) {
            Vec2 a;
            Vec2 b;
            const water::Point2& p = body.polygon[i];
            const water::Point2& q = body.polygon[(i + 1) % n];
            if (project(p.x, body.level + 0.4, p.z, a) && project(q.x, body.level + 0.4, q.z, b)) {
                list->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), line, width);
            }
        }
    } else if (body.kind == water::WaterBodyKind::River) {
        for (const water::RiverSegment& s : set.Segments(bodyIndex)) {
            Vec2 a;
            Vec2 b;
            if (project(s.ax, s.ya + 0.4, s.az, a) && project(s.bx, s.yb + 0.4, s.bz, b)) {
                list->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), line, width);
            }
        }
    }
}

} // namespace

void DrawWaterGizmos(WaterUiBridge& bridge, const Mat4& vp, Vec2 rectMin, Vec2 rectMax, bool hovered)
{
    if (bridge.world == nullptr || !bridge.gizmos) {
        bridge.dragging = false;
        bridge.handleHot = false;
        return;
    }
    const auto set = bridge.world->Bodies();
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushClipRect(ImVec2(rectMin.x, rectMin.y), ImVec2(rectMax.x, rectMax.y), true);

    std::vector<WaterHandle> handles;
    for (std::size_t i = 0; i < set->Bodies().size(); ++i) {
        const water::WaterBody& body = set->Bodies()[i];
        if (body.kind == water::WaterBodyKind::Ocean) {
            continue;
        }
        const bool selected = body.id == bridge.selectedId;
        DrawBody(list, *set, i, selected, vp, rectMin, rectMax);
        const std::size_t before = handles.size();
        BuildWaterHandles(body, vp, rectMin, rectMax, handles);
        if (!selected) {
            // Other bodies only offer their corners: click one to select the body.
            handles.erase(std::remove_if(handles.begin() + static_cast<std::ptrdiff_t>(before), handles.end(),
                              [](const WaterHandle& h) { return h.midpoint; }),
                handles.end());
        }
    }

    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 mouse(io.MousePos.x, io.MousePos.y);
    const bool ownsDrag = bridge.dragging && bridge.dragRect.x == rectMin.x && bridge.dragRect.y == rectMin.y;
    const bool free = hovered && !ImGui::IsAnyItemHovered() && !IsGizmoOver() && !IsGizmoUsing();
    const int pick = (free || ownsDrag) ? PickWaterHandle(handles, mouse, kPickRadius) : -1;
    bool hot = pick >= 0 || ownsDrag;
    bridge.debugHandles = static_cast<int>(handles.size());
    bridge.debugPick = pick;
    bridge.debugFree = free;
    bridge.debugHovered = hovered;
    bridge.debugMouse = mouse;

    if (ownsDrag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            WaterRay ray;
            double x = 0.0;
            double z = 0.0;
            if (ScreenToRay(vp, rectMin, rectMax, mouse, ray) && RayHitPlaneY(ray, bridge.dragPlaneY, x, z)) {
                const water::WaterResult r = bridge.world->MovePoint(bridge.dragBody, bridge.dragPoint, x, z, std::nullopt, std::nullopt);
                if (!r.ok) {
                    Report(bridge, r);
                }
            }
        } else {
            bridge.dragging = false;
            bridge.world->FlushCarve(); // Rebuild the terrain at once on release.
            hot = false;
        }
    } else if (free && !bridge.dragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        auto startDrag = [&](std::uint32_t id, int index) {
            const auto now = bridge.world->Bodies();
            const water::WaterBody* b = now->Find(id);
            bridge.selectedId = id;
            bridge.selectedPoint = index;
            if (b != nullptr) {
                bridge.dragging = true;
                bridge.dragBody = id;
                bridge.dragPoint = index;
                bridge.dragPlaneY = HandlePlaneY(*b, index);
                bridge.dragRect = rectMin;
            }
        };
        if (pick >= 0) {
            const WaterHandle h = handles[static_cast<std::size_t>(pick)];
            if (h.midpoint) {
                const water::WaterResult r = bridge.world->InsertPoint(h.bodyId, h.index, h.world.x, h.world.z);
                Report(bridge, r);
                if (r.ok) {
                    startDrag(h.bodyId, h.index);
                }
            } else {
                startDrag(h.bodyId, h.index);
            }
            hot = true;
        } else if (io.KeyCtrl && bridge.selectedId != 0) {
            const water::WaterBody* b = set->Find(bridge.selectedId);
            float distance = 0.0f;
            const int index = b != nullptr ? NearestOutlineInsertIndex(*b, vp, rectMin, rectMax, mouse, distance) : -1;
            WaterRay ray;
            double x = 0.0;
            double z = 0.0;
            if (index >= 0 && distance < kInsertOutlineRadius && ScreenToRay(vp, rectMin, rectMax, mouse, ray)
                && RayHitPlaneY(ray, HandlePlaneY(*b, std::max(0, index - 1)), x, z)) {
                const water::WaterResult r = bridge.world->InsertPoint(bridge.selectedId, index, x, z);
                Report(bridge, r);
                if (r.ok) {
                    startDrag(bridge.selectedId, index);
                    hot = true;
                }
            }
        }
    }

    if (free && !bridge.dragging && bridge.selectedId != 0 && bridge.selectedPoint >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        const water::WaterResult r = bridge.world->RemovePoint(bridge.selectedId, bridge.selectedPoint);
        Report(bridge, r);
        if (r.ok) {
            bridge.selectedPoint = -1;
            bridge.world->FlushCarve();
        }
    }
    bridge.handleHot = hot;

    // Handles on top of the outlines.
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const WaterHandle& h = handles[i];
        const bool selectedBody = h.bodyId == bridge.selectedId;
        const bool selectedPoint = selectedBody && !h.midpoint && h.index == bridge.selectedPoint;
        const bool hovering = static_cast<int>(i) == pick;
        const ImVec2 c(h.screen.x, h.screen.y);
        if (h.midpoint) {
            const float r = hovering ? 5.0f : 3.5f;
            list->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), Color(255, 255, 255, hovering ? 255 : 150));
        } else {
            const float r = selectedPoint ? 8.0f : (hovering ? 7.5f : 6.0f);
            list->AddCircleFilled(c, r, selectedPoint ? Color(255, 140, 30) : (selectedBody ? Color(255, 235, 90) : Color(120, 200, 255)));
            list->AddCircle(c, r, Color(20, 20, 20), 0, 1.5f);
        }
    }
    list->PopClipRect();
}

} // namespace gte
