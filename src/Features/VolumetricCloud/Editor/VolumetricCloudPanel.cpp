#include "VolumetricCloudPanel.h"

#include "../VolumetricCloudMath.h"
#include "../VolumetricCloudTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace gte {

namespace {

constexpr std::array<const char*, 5> kDebugViews{ "Off (the clouds)", "Shape slice", "Detail slice", "Weather map", "Blue noise" };
constexpr std::array<const char*, 3> kDivisorLabels{ "1 (full)", "2 (1/4 of the pixels)", "4 (1/16 of the pixels)" };
constexpr std::array<float, 3> kDivisorValues{ 1.0f, 2.0f, 4.0f };

const char* StatusText(VolumetricCloudFrameStatus status) noexcept
{
    switch (status) {
    case VolumetricCloudFrameStatus::Ok: return "Ok";
    case VolumetricCloudFrameStatus::NotRendered: return "Not rendered (the view is not drawn or the effect is off for it)";
    case VolumetricCloudFrameStatus::Disabled: return "Disabled";
    case VolumetricCloudFrameStatus::AssetMissing: return "Noise files missing (assets/clouds)";
    case VolumetricCloudFrameStatus::ShadersNotReady: return "Shaders not ready";
    case VolumetricCloudFrameStatus::TargetFailed: return "Target allocation failed";
    case VolumetricCloudFrameStatus::PassDisabled: return "Pass is disabled";
    case VolumetricCloudFrameStatus::NoValidCamera: return "No valid camera (perspective only)";
    }
    return "Unknown";
}

void DrawStatus(const VolumetricCloudViewSnapshot& game, const VolumetricCloudViewSnapshot& scene)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(game.status == VolumetricCloudFrameStatus::Ok ? green : orange, "Game view: %s", StatusText(game.status));
    if (game.status == VolumetricCloudFrameStatus::Ok) {
        ImGui::Text("March 1/%u, history %s", game.divisor, game.historyUsed ? "in use" : "not used");
    }
    ImGui::Text("Scene view: %s", StatusText(scene.status));
}

} // namespace

bool BuildVolumetricCloudPanel(
    VolumetricCloudSettings& edit, const VolumetricCloudViewSnapshot& game, const VolumetricCloudViewSnapshot& scene)
{
    bool changed = false;
    DrawStatus(game, scene);

    ImGui::SeparatorText("General");
    changed |= ImGui::Checkbox("Enabled", &edit.enabled);
    ImGui::TextDisabled("The sun is the active Directional Light. Off by default: the effect is heavy.");
    for (std::size_t i = 0; i < kVolumetricCloudPresetNames.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (ImGui::Button(kVolumetricCloudPresetNames[i])) {
            edit = ApplyVolumetricCloudPreset(edit, static_cast<VolumetricCloudPreset>(i));
            changed = true;
        }
    }

    ImGui::SeparatorText("Layer");
    changed |= ImGui::DragFloat("Base altitude (m)", &edit.layerBaseM, 10.0f, 0.0f, 20000.0f);
    changed |= ImGui::DragFloat("Top altitude (m)", &edit.layerTopM, 10.0f, 10.0f, 30000.0f);
    changed |= ImGui::DragFloat("Max distance (m)", &edit.maxDistanceM, 100.0f, 1000.0f, 500000.0f);

    ImGui::SeparatorText("Shape");
    changed |= ImGui::SliderFloat("Coverage", &edit.coverage, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Density", &edit.density, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Extinction (1/m)", &edit.extinction, 0.0005f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Detail strength", &edit.detailStrength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Wind speed (m/s)", &edit.windSpeed, 0.0f, 100.0f);
    changed |= ImGui::SliderFloat("Wind direction", &edit.windDirection, -360.0f, 360.0f);

    ImGui::SeparatorText("Light");
    changed |= ImGui::SliderFloat("Sun intensity", &edit.sunIntensity, 0.0f, 32.0f);
    changed |= ImGui::SliderFloat("Ambient intensity", &edit.ambientIntensity, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Silver lining", &edit.silverIntensity, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Powder", &edit.powder, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Haze distance (m)", &edit.hazeDistanceM, 500.0f, 1000.0f, 1000000.0f);

    ImGui::SeparatorText("Quality");
    int divisorIndex = 0;
    for (std::size_t i = 0; i < kDivisorValues.size(); ++i) {
        if (static_cast<float>(CloudResolutionDivisor(edit)) == kDivisorValues[i]) {
            divisorIndex = static_cast<int>(i);
        }
    }
    if (ImGui::Combo("March resolution", &divisorIndex, kDivisorLabels.data(), static_cast<int>(kDivisorLabels.size()))) {
        edit.resolutionDivisor = kDivisorValues[static_cast<std::size_t>(std::clamp(divisorIndex, 0, 2))];
        changed = true;
    }
    bool temporal = edit.temporal >= 0.5f;
    if (ImGui::Checkbox("Temporal accumulation", &temporal)) {
        edit.temporal = temporal ? 1.0f : 0.0f;
        changed = true;
    }
    changed |= ImGui::SliderFloat("Max steps", &edit.maxSteps, 8.0f, 256.0f, "%.0f");
    changed |= ImGui::SliderFloat("Sun steps", &edit.sunSteps, 1.0f, 8.0f, "%.0f");
    changed |= ImGui::SliderFloat("Fresh blend", &edit.freshBlend, 0.01f, 1.0f);

    ImGui::SeparatorText("Debug");
    int view = static_cast<int>(edit.debugView);
    if (ImGui::Combo("View", &view, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
        edit.debugView = static_cast<VolumetricCloudDebugView>(std::clamp(view, 0, static_cast<int>(kDebugViews.size()) - 1));
        changed = true;
    }

    if (ImGui::Button("Reset to defaults")) {
        edit = VolumetricCloudSettings{};
        changed = true;
    }
    return changed;
}

} // namespace gte
