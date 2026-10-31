#include "SceneViewGizmo.h"

#include "../EditorCamera.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace gte {

namespace {

constexpr float kGizmoSize = 96.0f;      // Square box of the six heads.
constexpr float kMargin = 8.0f;          // Gap to the image corner.
constexpr float kArmLength = 33.0f;      // Center to head center.
constexpr float kPositiveHeadRadius = 9.0f;
constexpr float kNegativeHeadRadius = 6.0f;
constexpr float kHitRadius = 10.0f;
constexpr float kLabelGap = 4.0f;
constexpr float kLabelPadding = 4.0f;
constexpr float kMinImageWidth = 220.0f; // Smaller Scene images get no gizmo.
constexpr float kMinImageHeight = 190.0f;

// Positive heads are the even enum values.
bool IsPositive(SceneViewAxis axis) noexcept
{
    return (static_cast<std::size_t>(axis) % 2) == 0;
}

// X red, Y green, Z blue.
ImU32 AxisColor(SceneViewAxis axis, int alpha) noexcept
{
    switch (static_cast<std::size_t>(axis) / 2) {
        case 0:
            return IM_COL32(222, 64, 64, alpha);
        case 1:
            return IM_COL32(128, 204, 64, alpha);
        default:
            return IM_COL32(64, 128, 255, alpha);
    }
}

char AxisLetter(SceneViewAxis axis) noexcept
{
    constexpr char kLetters[] = { 'X', 'Y', 'Z' };
    return kLetters[static_cast<std::size_t>(axis) / 2];
}

bool Contains(const ImVec2& min, const ImVec2& max, const ImVec2& p) noexcept
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

} // namespace

SceneViewGizmoResult DrawSceneViewGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const EditorCamera& camera)
{
    SceneViewGizmoResult result;
    if (imageSize.x < kMinImageWidth || imageSize.y < kMinImageHeight) {
        return result;
    }

    const float imageRight = imageMin.x + imageSize.x;
    const ImVec2 boxMin(imageRight - kMargin - kGizmoSize, imageMin.y + kMargin);
    const ImVec2 center(boxMin.x + kGizmoSize * 0.5f, boxMin.y + kGizmoSize * 0.5f);

    const std::string text = SceneViewLabel(AlignedSceneViewAxis(camera.Forward()), camera.IsOrthographic());
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const float labelWidth = textSize.x + kLabelPadding * 2.0f;
    const float labelHeight = textSize.y + kLabelPadding * 2.0f;
    const float labelLeft = std::min(center.x - labelWidth * 0.5f, imageRight - kMargin - labelWidth);
    const ImVec2 labelMin(labelLeft, boxMin.y + kGizmoSize + kLabelGap);
    const ImVec2 labelMax(labelMin.x + labelWidth, labelMin.y + labelHeight);

    // One item covers the heads and the text. It also blocks camera picking under the gizmo.
    const ImVec2 areaMin(std::min(boxMin.x, labelMin.x), boxMin.y);
    const ImVec2 areaMax(std::max(boxMin.x + kGizmoSize, labelMax.x), labelMax.y);
    ImGui::SetCursorScreenPos(areaMin);
    ImGui::InvisibleButton("##SceneViewGizmo", ImVec2(areaMax.x - areaMin.x, areaMax.y - areaMin.y));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    const SceneViewGizmoLayout layout =
        LayoutSceneViewGizmo(camera.Right(), camera.Up(), camera.Forward(), kArmLength);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    std::optional<SceneViewAxis> hoveredAxis;
    if (hovered) {
        hoveredAxis = HitTestSceneViewGizmo(layout, Vec2(mouse.x - center.x, mouse.y - center.y), kHitRadius);
    }
    const bool overLabel = hovered && !hoveredAxis.has_value() && Contains(labelMin, labelMax, mouse);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (hovered) {
        drawList->AddCircleFilled(center, kGizmoSize * 0.5f - 2.0f, IM_COL32(255, 255, 255, 30));
    }

    // Far heads first, so near heads draw over them.
    for (std::size_t i = layout.heads.size(); i-- > 0;) {
        const SceneViewGizmoHead& head = layout.heads[i];
        const ImVec2 headCenter(center.x + head.offset.x, center.y + head.offset.y);
        const bool isHot = hoveredAxis.has_value() && *hoveredAxis == head.axis;
        float radius = kNegativeHeadRadius;
        if (IsPositive(head.axis)) {
            radius = kPositiveHeadRadius;
            drawList->AddLine(center, headCenter, AxisColor(head.axis, 255), 2.0f);
            drawList->AddCircleFilled(headCenter, radius, AxisColor(head.axis, 255));
            const char letter[2] = { AxisLetter(head.axis), '\0' };
            const ImVec2 letterSize = ImGui::CalcTextSize(letter);
            drawList->AddText(ImVec2(headCenter.x - letterSize.x * 0.5f, headCenter.y - letterSize.y * 0.5f),
                IM_COL32(20, 20, 20, 255), letter);
        } else {
            drawList->AddCircleFilled(headCenter, radius, AxisColor(head.axis, 130));
        }
        if (isHot) {
            drawList->AddCircle(headCenter, radius + 1.5f, IM_COL32(255, 255, 255, 255), 0, 1.5f);
        }
    }

    drawList->AddRectFilled(
        labelMin, labelMax, overLabel ? IM_COL32(255, 255, 255, 60) : IM_COL32(0, 0, 0, 90), 3.0f);
    drawList->AddText(
        ImVec2(labelMin.x + kLabelPadding, labelMin.y + kLabelPadding), IM_COL32(255, 255, 255, 230), text.c_str());

    if (hoveredAxis.has_value() || overLabel) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (overLabel) {
        ImGui::SetTooltip("Click to switch between Perspective and Isometric");
    }

    if (clicked && hoveredAxis.has_value()) {
        result.alignTo = *hoveredAxis;
    } else if (clicked && overLabel) {
        result.toggleProjection = true;
    }
    return result;
}

} // namespace gte
