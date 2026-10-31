#pragma once

#include <imgui.h>

namespace gte {

// Label at the top-right of a view image. `row` stacks labels downwards. `rightInset` keeps it clear of a widget on the right.
inline void DrawViewOverlayLabel(
    const ImVec2& imageMin, const ImVec2& imageSize, const char* text, int row, float rightInset = 0.0f)
{
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    ImGui::GetWindowDrawList()->AddText(ImVec2(imageMin.x + imageSize.x - rightInset - textSize.x - 8.0f,
                                             imageMin.y + 6.0f + rowHeight * static_cast<float>(row)),
        IM_COL32(255, 255, 255, 170), text);
}

// Shown while the Hi-Z demo runs with its geometry-only profile.
inline constexpr const char* kHizDemoOverlayLabel = "Hi-Z demo: depth-only views and post effects are off";

} // namespace gte
