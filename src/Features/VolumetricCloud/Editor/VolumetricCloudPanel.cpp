#include "VolumetricCloudPanel.h"

#include "../VolumetricCloudMath.h"
#include "../VolumetricCloudTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gte {

namespace {

constexpr std::array<const char*, 5> kDebugViews{ "Off (the clouds)", "Shape slice", "Detail slice", "Weather map", "Blue noise" };
constexpr std::array<const char*, 7> kDivisorLabels{ "Auto (about 320 texels wide)", "1 (full)", "2 (1/4 of the pixels)",
    "3 (1/9 of the pixels)", "4 (1/16 of the pixels)", "5 (1/25 of the pixels)", "6 (1/36 of the pixels)" };
constexpr std::array<float, 7> kDivisorValues{ 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };

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

const char* SkyLightText(VolumetricCloudSkyLight light) noexcept
{
    switch (light) {
    case VolumetricCloudSkyLight::Standalone: return "standalone";
    case VolumetricCloudSkyLight::Atmosphere: return "atmosphere tables";
    case VolumetricCloudSkyLight::Unavailable: return "standalone (the atmosphere published no tables)";
    }
    return "standalone";
}

void DrawStatus(const VolumetricCloudViewSnapshot& game, const VolumetricCloudViewSnapshot& scene)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(game.status == VolumetricCloudFrameStatus::Ok ? green : orange, "Game view: %s", StatusText(game.status));
    if (game.status == VolumetricCloudFrameStatus::Ok) {
        ImGui::Text("March 1/%u, history %s", game.divisor, game.historyUsed ? "in use" : "not used");
        ImGui::Text("Sky light: %s", SkyLightText(game.skyLight));
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
    changed |= ImGui::SliderFloat("Fade start (of max distance)", &edit.fadeStart, 0.0f, 0.99f);

    ImGui::SeparatorText("Shape");
    changed |= ImGui::SliderFloat("Coverage", &edit.coverage, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Cloud type (lowest peak)", &edit.peakMin, 0.05f, 1.0f);
    changed |= ImGui::SliderFloat("Density", &edit.density, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Big lobes (mix)", &edit.largeShapeMix, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Size variation", &edit.sizeVariation, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Worley cells (popcorn)", &edit.shapeWorley, 0.0f, 1.5f);
    changed |= ImGui::SliderFloat("Vertical stretch", &edit.verticalStretch, 0.25f, 6.0f);
    changed |= ImGui::SliderFloat("Base variation", &edit.baseVariation, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Base roundness", &edit.baseRoundness, 0.01f, 0.5f);
    changed |= ImGui::SliderFloat("Body threshold floor", &edit.bodyThresholdMin, 0.0f, 0.9f);
    changed |= ImGui::SliderFloat("Body ramp width", &edit.bodyWidth, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Ragged base", &edit.baseRagged, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Far softening", &edit.farSoftening, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Erosion kept in body", &edit.bodyErosion, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Wind shear (m)", &edit.windShearM, 10.0f, 0.0f, 10000.0f);
    changed |= ImGui::SliderFloat("Extinction (1/m)", &edit.extinction, 0.0005f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Detail strength", &edit.detailStrength, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Wispiness", &edit.wispiness, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Fine detail", &edit.fineDetail, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Curl amount (m)", &edit.curlAmountM, 5.0f, 0.0f, 2000.0f);
    changed |= ImGui::DragFloat("Curl tile (m)", &edit.curlScaleM, 50.0f, 200.0f, 100000.0f);
    changed |= ImGui::DragFloat("Far field start (m)", &edit.farWeatherStartM, 100.0f, 1000.0f, 500000.0f);
    changed |= ImGui::SliderFloat("Far weather mix", &edit.farWeatherMix, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Far coverage lift", &edit.farCoverageLift, 0.0f, 0.5f);
    changed |= ImGui::SliderFloat("Far air scale", &edit.farAirScale, 0.05f, 1.0f);
    changed |= ImGui::SliderFloat("Density curve", &edit.densityCurve, 0.2f, 2.0f);
    changed |= ImGui::SliderFloat("Max step depth", &edit.maxStepDepth, 0.1f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Top falloff start", &edit.topFalloffStart, 0.0f, 0.95f);
    changed |= ImGui::SliderFloat("Wind speed (m/s)", &edit.windSpeed, 0.0f, 100.0f);
    changed |= ImGui::SliderFloat("Wind direction", &edit.windDirection, -360.0f, 360.0f);

    ImGui::SeparatorText("Light");
    changed |= ImGui::SliderFloat("Sun intensity", &edit.sunIntensity, 0.0f, 32.0f);
    changed |= ImGui::SliderFloat("Ambient intensity", &edit.ambientIntensity, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Silver lining", &edit.silverIntensity, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Powder", &edit.powder, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("In-scatter chance", &edit.inScatter, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Haze distance (m, standalone)", &edit.hazeDistanceM, 500.0f, 1000.0f, 1000000.0f);

    ImGui::SeparatorText("Atmosphere");
    bool fromAtmosphere = edit.atmosphereMode >= 0.5f;
    if (ImGui::Checkbox("Light from the atmosphere tables", &fromAtmosphere)) {
        edit.atmosphereMode = fromAtmosphere ? 1.0f : 0.0f;
        changed = true;
    }
    changed |= ImGui::SliderFloat("Sky ambient", &edit.skyAmbient, 0.0f, 10.0f);
    changed |= ImGui::SliderFloat("Air scale", &edit.airScale, 0.0f, 8.0f);
    changed |= ImGui::SliderFloat("Sky occlusion", &edit.skyOcclusion, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Sky floor", &edit.skyFloor, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Ground bounce", &edit.groundBounce, 0.0f, 8.0f);
    changed |= ImGui::SliderFloat("Ground warmth", &edit.groundWarmth, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Scatter blue shift", &edit.msBlueShift, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sky light saturation", &edit.ambientSaturation, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Scatter diffusion", &edit.msDiffuse, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Inner glow", &edit.msGlow, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Sun warmth", &edit.sunWarmth, 0.0f, 1.0f);

    ImGui::SeparatorText("Cloud shadows");
    changed |= ImGui::SliderFloat("Shadow strength", &edit.shadowStrength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Shadow range (km)", &edit.shadowRangeKm, 4.0f, 100.0f);
    changed |= ImGui::SliderFloat("Shadow softness", &edit.shadowSoftness, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Shadow update (frames)", &edit.shadowUpdate, 1.0f, 4.0f, "%.0f");
    changed |= ImGui::SliderFloat("Shadow density", &edit.shadowDensity, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Shadow steps", &edit.shadowSteps, 4.0f, 24.0f, "%.0f");
    changed |= ImGui::SliderFloat("Light shafts", &edit.shaftStrength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Shaft sharpness", &edit.shaftSharpness, 1.0f, 8.0f, "%.1f");
    changed |= ImGui::SliderFloat("Shaft haze (per km)", &edit.shaftHaze, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Shaft shadow fill", &edit.shaftFill, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Shaft mist near (per km)", &edit.shaftMist, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Shaft haze height (m)", &edit.shaftHeightM, 200.0f, 6000.0f, "%.0f");
    changed |= ImGui::SliderFloat("Shadow fill", &edit.shadowFill, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sun rays", &edit.sunRays, 0.0f, 3.0f);

    ImGui::SeparatorText("Quality");
    int divisorIndex = 0;
    for (std::size_t i = 0; i < kDivisorValues.size(); ++i) {
        if (std::lround(edit.resolutionDivisor) == std::lround(kDivisorValues[i])) {
            divisorIndex = static_cast<int>(i);
        }
    }
    if (ImGui::Combo("March resolution", &divisorIndex, kDivisorLabels.data(), static_cast<int>(kDivisorLabels.size()))) {
        edit.resolutionDivisor = kDivisorValues[static_cast<std::size_t>(std::clamp(divisorIndex, 0, 6))];
        changed = true;
    }
    bool temporal = edit.temporal >= 0.5f;
    if (ImGui::Checkbox("Temporal accumulation", &temporal)) {
        edit.temporal = temporal ? 1.0f : 0.0f;
        changed = true;
    }
    changed |= ImGui::SliderFloat("Max steps", &edit.maxSteps, 8.0f, 256.0f, "%.0f");
    changed |= ImGui::SliderFloat("Sun steps", &edit.sunSteps, 1.0f, 8.0f, "%.0f");
    changed |= ImGui::SliderFloat("Fresh blend (floor)", &edit.freshBlend, 0.01f, 1.0f);
    changed |= ImGui::SliderFloat("Motion blend", &edit.motionBlend, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sub-pixel jitter (px)", &edit.subpixelJitter, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Sharp history", &edit.historySharp, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Adaptive blend", &edit.adaptiveBlend, 0.0f, 1.0f);

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
