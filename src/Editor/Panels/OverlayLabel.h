#pragma once

#include <imgui.h>

namespace gte {

// Label drawn over the top-right corner of a view image. `row` stacks several labels downwards.
inline void DrawViewOverlayLabel(const ImVec2& imageMin, const ImVec2& imageSize, const char* text, int row)
{
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(imageMin.x + imageSize.x - textSize.x - 8.0f, imageMin.y + 6.0f + rowHeight * static_cast<float>(row)),
        IM_COL32(255, 255, 255, 170), text);
}

// Shown while the Hi-Z demo runs with its geometry-only profile.
inline constexpr const char* kHizDemoOverlayLabel = "Hi-Z demo: depth-only views and post effects are off";

} // namespace gte
