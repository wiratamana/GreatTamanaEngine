#include "ToneMappingPanel.h"

#include "../ToneMappingTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace gte {

namespace {

constexpr std::array<const char*, 3> kModeLabels{ "Clamp", "ACES", "PBR Neutral" };

const char* StatusText(ToneMappingFrameStatus status) noexcept
{
    switch (status) {
    case ToneMappingFrameStatus::Ok: return "Ok";
    case ToneMappingFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case ToneMappingFrameStatus::ShadersNotReady: return "Shaders not ready";
    case ToneMappingFrameStatus::TargetFailed: return "Target allocation failed";
    case ToneMappingFrameStatus::PassDisabled: return "Pass is disabled (the picture stays linear)";
    }
    return "Unknown";
}

void DrawStatus(const char* label, const ToneMappingViewSnapshot& view)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(view.status == ToneMappingFrameStatus::Ok ? green : orange, "%s: %s", label, StatusText(view.status));
}

} // namespace

bool BuildToneMappingPanel(
    ToneMappingSettings& edit, const ToneMappingViewSnapshot& game, const ToneMappingViewSnapshot& scene)
{
    bool changed = false;

    DrawStatus("Game view", game);
    DrawStatus("Scene view", scene);

    ImGui::SeparatorText("Display transform");
    changed |= ImGui::DragFloat("Exposure", &edit.exposure, 0.01f, 0.01f, 16.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    int mode = static_cast<int>(edit.mode);
    if (ImGui::Combo("Mode", &mode, kModeLabels.data(), static_cast<int>(kModeLabels.size()))) {
        edit.mode = static_cast<tonemap::ToneMapMode>(std::clamp(mode, 0, static_cast<int>(kModeLabels.size()) - 1));
        changed = true;
    }
    changed |= ImGui::SliderFloat("Saturation", &edit.saturation, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Dither", &edit.dither, 0.0f, 2.0f);

    if (ImGui::Button("Reset to defaults")) {
        edit = ToneMappingSettings{};
        changed = true;
    }

    return changed;
}

} // namespace gte
