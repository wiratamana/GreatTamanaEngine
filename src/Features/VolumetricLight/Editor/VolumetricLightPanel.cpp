#include "VolumetricLightPanel.h"

#include "../VolumetricLightTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace gte {

namespace {

constexpr std::array<const char*, 3> kDebugViews{ "Off", "In-scatter only", "Transmittance" };
constexpr std::array<const char*, 4> kShadowSampleLabels{ "1", "2", "4", "8" };
constexpr std::array<std::uint32_t, 4> kShadowSampleValues{ 1, 2, 4, 8 };

const char* StatusText(VolumetricLightFrameStatus status) noexcept
{
    switch (status) {
    case VolumetricLightFrameStatus::Ok: return "Ok";
    case VolumetricLightFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case VolumetricLightFrameStatus::Disabled: return "Disabled";
    case VolumetricLightFrameStatus::NoValidCamera: return "No valid camera (perspective only)";
    case VolumetricLightFrameStatus::ShadersNotReady: return "Shaders not ready";
    case VolumetricLightFrameStatus::TargetFailed: return "Target allocation failed";
    case VolumetricLightFrameStatus::PassDisabled: return "Pass is disabled";
    }
    return "Unknown";
}

const char* ReceiverText(VolumetricLightReceiverState state) noexcept
{
    switch (state) {
    case VolumetricLightReceiverState::NotChecked: return "Not checked";
    case VolumetricLightReceiverState::Used: return "Used (sun shadows drive the shafts)";
    case VolumetricLightReceiverState::Missing: return "Missing (no sun shadow data, fog is unshadowed)";
    case VolumetricLightReceiverState::Rejected: return "Rejected (shadow data is invalid, fog is unshadowed)";
    }
    return "Unknown";
}

void DrawStatus(const VolumetricLightViewSnapshot& game)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(
        game.status == VolumetricLightFrameStatus::Ok ? green : orange, "Game view: %s", StatusText(game.status));
    if (game.status == VolumetricLightFrameStatus::Ok) {
        ImGui::Text("Shadow receiver: %s", ReceiverText(game.receiver));
        ImGui::Text("Cascades: %u", game.cascadeCount);
        ImGui::Text("Effective max distance: %.1f", game.effectiveMaxDistance);
        ImGui::Text("Temporal history: %s", game.historyUsed ? "in use" : "not used");
        ImGui::Text("Jitter index: %u", game.jitterIndex);
        ImGui::Text("Atmosphere light: %s", game.atmosphereLight ? "used" : "not used");
    }
}

} // namespace

bool BuildVolumetricLightPanel(
    VolumetricLightSettings& edit, const VolumetricLightViewSnapshot& game, const VolumetricLightViewSnapshot& scene)
{
    bool changed = false;
    ImGui::Begin("VolumetricLight");

    DrawStatus(game);

    ImGui::SeparatorText("General");
    changed |= ImGui::Checkbox("Enabled", &edit.enabled);
    changed |= ImGui::SliderFloat("Strength", &edit.strength, 0.0f, 1.0f);

    ImGui::SeparatorText("Medium");
    changed |= ImGui::DragFloat("Density", &edit.density, 0.0005f, 0.0f, 1.0f, "%.4f");
    changed |= ImGui::SliderFloat("Albedo", &edit.albedo, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Anisotropy (g)", &edit.anisotropyG, -0.95f, 0.95f);
    changed |= ImGui::DragFloat("Height density", &edit.heightDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
    changed |= ImGui::DragFloat("Height base", &edit.heightBase, 0.1f, -100000.0f, 100000.0f);
    changed |= ImGui::SliderFloat("Height falloff", &edit.heightFalloff, 0.0f, 2.0f);
    changed |= ImGui::SliderFloat("Noise amount", &edit.noiseAmount, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Noise scale", &edit.noiseScale, 0.001f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Wind X", &edit.windX, -20.0f, 20.0f);
    changed |= ImGui::SliderFloat("Wind Z", &edit.windZ, -20.0f, 20.0f);

    ImGui::SeparatorText("Range and grid");
    changed |= ImGui::DragFloat("Max distance", &edit.maxDistance, 1.0f, 1.0f, 2000.0f);
    changed |= ImGui::DragFloat("Start distance", &edit.startDistance, 0.5f, 0.0f, std::max(edit.maxDistance, 1.0f));
    changed |= ImGui::SliderFloat("Depth exponent", &edit.depthExponent, 1.0f, 4.0f);

    ImGui::SeparatorText("Light");
    changed |= ImGui::SliderFloat("Sun intensity", &edit.sunIntensity, 0.0f, 16.0f);
    float ambient[3] = { edit.ambientR, edit.ambientG, edit.ambientB };
    if (ImGui::ColorEdit3("Ambient color", ambient, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
        edit.ambientR = ambient[0];
        edit.ambientG = ambient[1];
        edit.ambientB = ambient[2];
        changed = true;
    }
    changed |= ImGui::SliderFloat("Ambient intensity", &edit.ambientIntensity, 0.0f, 16.0f);
    changed |= ImGui::SliderFloat("Ambient in shadow", &edit.ambientShadowedFraction, 0.0f, 1.0f);
    ImGui::TextDisabled("1 = ambient fog ignores sun shadows. 0 = ambient only in sunlit air (sharp shafts).");

    ImGui::SeparatorText("Shadow sampling");
    int sampleIndex = 0;
    for (std::size_t i = 0; i < kShadowSampleValues.size(); ++i) {
        if (kShadowSampleValues[i] == edit.shadowSamples) {
            sampleIndex = static_cast<int>(i);
        }
    }
    if (ImGui::Combo("Samples", &sampleIndex, kShadowSampleLabels.data(), static_cast<int>(kShadowSampleLabels.size()))) {
        edit.shadowSamples = kShadowSampleValues[static_cast<std::size_t>(
            std::clamp(sampleIndex, 0, static_cast<int>(kShadowSampleValues.size()) - 1))];
        changed = true;
    }
    changed |= ImGui::SliderFloat("Filter (texels)", &edit.shadowFilterTexels, 0.0f, 8.0f);
    changed |= ImGui::SliderFloat("Bias (texels)", &edit.shadowBiasTexels, 0.0f, 16.0f);

    ImGui::SeparatorText("Temporal");
    changed |= ImGui::Checkbox("Temporal", &edit.temporal);
    changed |= ImGui::Checkbox("Jitter", &edit.jitter);
    changed |= ImGui::SliderFloat("History blend", &edit.historyBlend, 0.0f, 0.98f);

    ImGui::SeparatorText("Composite");
    changed |= ImGui::SliderFloat("Sky distance", &edit.skyDistanceFraction, 0.0f, 1.0f);

    ImGui::SeparatorText("Debug");
    int view = static_cast<int>(edit.debugView);
    if (ImGui::Combo("View", &view, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
        edit.debugView =
            static_cast<VolumetricLightDebugView>(std::clamp(view, 0, static_cast<int>(kDebugViews.size()) - 1));
        changed = true;
    }
    ImGui::Text("Grid: %u x %u x %u", kVolumetricLightGridWidth, kVolumetricLightGridHeight, kVolumetricLightGridDepth);
    const double megabytes = static_cast<double>(game.textureBytes + scene.textureBytes) / (1024.0 * 1024.0);
    ImGui::Text("Volume texture memory (Game + Scene): %.1f MB", megabytes);

    if (ImGui::Button("Reset to defaults")) {
        edit = VolumetricLightSettings{};
        changed = true;
    }

    ImGui::End();
    return changed;
}

} // namespace gte
