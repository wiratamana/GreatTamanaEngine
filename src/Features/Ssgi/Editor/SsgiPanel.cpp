#include "SsgiPanel.h"

#include "../SsgiTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace gte {

namespace {

constexpr std::array<const char*, 3> kDebugViews{ "Off", "GI only", "Normals" };
constexpr std::array<const char*, 2> kResolutions{ "Full", "Half" };

const char* StatusText(SsgiFrameStatus status) noexcept
{
    switch (status) {
    case SsgiFrameStatus::Ok: return "Ok";
    case SsgiFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case SsgiFrameStatus::NoValidCamera: return "No valid camera (perspective only)";
    case SsgiFrameStatus::ShadersNotReady: return "Shaders not ready";
    case SsgiFrameStatus::TargetFailed: return "Target allocation failed";
    case SsgiFrameStatus::PassDisabled: return "Pass is disabled";
    }
    return "Unknown";
}

bool SliderCount(const char* label, std::uint32_t& value, int lo, int hi)
{
    int v = static_cast<int>(value);
    const bool changed = ImGui::SliderInt(label, &v, lo, hi);
    if (changed) {
        value = static_cast<std::uint32_t>(std::clamp(v, lo, hi));
    }
    return changed;
}

void DrawStatus(const SsgiViewSnapshot& game)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(game.status == SsgiFrameStatus::Ok ? green : orange, "Game view: %s", StatusText(game.status));
    if (game.status == SsgiFrameStatus::Ok) {
        ImGui::Text("GI size: %u x %u (divisor %u)", game.giExtent.width, game.giExtent.height, game.divisor);
        ImGui::Text("Temporal history: %s", game.historyUsed ? "in use" : "not used");
    }
}

} // namespace

bool BuildSsgiPanel(SsgiSettings& edit, const SsgiViewSnapshot& game, const SsgiViewSnapshot& scene)
{
    bool changed = false;
    ImGui::Begin("Ssgi");

    DrawStatus(game);

    ImGui::SeparatorText("Lighting");
    changed |= ImGui::SliderFloat("Intensity", &edit.intensity, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Albedo luma", &edit.albedoLuma, 0.0f, 0.95f);
    changed |= ImGui::SliderFloat("Albedo saturation", &edit.albedoSaturation, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Max radiance", &edit.maxRadiance, 0.1f, 0.5f, 64.0f);

    ImGui::SeparatorText("Rays");
    changed |= SliderCount("Rays", edit.rayCount, 1, 8);
    changed |= SliderCount("Steps", edit.stepCount, 4, 48);
    changed |= ImGui::DragFloat("Max ray distance", &edit.maxRayDistance, 0.05f, 0.1f, 100.0f);
    changed |= ImGui::DragFloat("Thickness", &edit.thickness, 0.01f, 0.01f, 5.0f);
    changed |= ImGui::SliderFloat("Depth bias", &edit.depthBias, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Distribution power", &edit.stepDistributionPower, 1.0f, 4.0f);
    changed |= ImGui::SliderFloat("End fade", &edit.endFadeFraction, 0.0f, 0.9f);
    changed |= ImGui::DragFloat("Max distance", &edit.maxDistance, 1.0f, 1.0f, 100000.0f);
    changed |= ImGui::SliderFloat("Distance fade", &edit.distanceFadeFraction, 0.0f, 0.9f);

    ImGui::SeparatorText("Quality");
    int resolution = static_cast<int>(edit.resolutionDivisor) - 1;
    if (ImGui::Combo("Resolution", &resolution, kResolutions.data(), static_cast<int>(kResolutions.size()))) {
        edit.resolutionDivisor = static_cast<std::uint32_t>(std::clamp(resolution, 0, 1)) + 1u;
        changed = true;
    }
    changed |= ImGui::Checkbox("Spatial denoise", &edit.spatialDenoise);
    changed |= ImGui::Checkbox("Temporal", &edit.temporal);
    changed |= ImGui::SliderFloat("History weight", &edit.historyWeight, 0.0f, 0.98f);
    changed |= ImGui::SliderFloat("Depth sigma", &edit.depthSigma, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::Checkbox("Apply in Scene view", &edit.applyInSceneView);

    ImGui::SeparatorText("Debug");
    int view = static_cast<int>(edit.debugView);
    if (ImGui::Combo("View", &view, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
        edit.debugView = static_cast<SsgiDebugView>(std::clamp(view, 0, static_cast<int>(kDebugViews.size()) - 1));
        changed = true;
    }
    const double megabytes = static_cast<double>(game.textureBytes + scene.textureBytes) / (1024.0 * 1024.0);
    ImGui::Text("GI texture memory (Game + Scene): %.1f MB", megabytes);

    if (ImGui::Button("Reset to defaults")) {
        edit = SsgiSettings{};
        changed = true;
    }

    ImGui::End();
    return changed;
}

} // namespace gte
