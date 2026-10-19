#include "GtaoPanel.h"

#include "../../../Editor/EditorContext.h"
#include "../GtaoTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace gte {

namespace {

constexpr std::array<const char*, 2> kDebugViews{ "Off", "AO only" };
constexpr std::array<const char*, 2> kResolutions{ "Full", "Half" };

const char* StatusText(GtaoFrameStatus status) noexcept
{
    switch (status) {
    case GtaoFrameStatus::Ok: return "Ok";
    case GtaoFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case GtaoFrameStatus::NoValidCamera: return "No valid camera (perspective only)";
    case GtaoFrameStatus::ShadersNotReady: return "Shaders not ready";
    case GtaoFrameStatus::TargetFailed: return "Target allocation failed";
    case GtaoFrameStatus::PassDisabled: return "Pass is disabled";
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

void DrawStatus(const GtaoViewSnapshot& game)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(game.status == GtaoFrameStatus::Ok ? green : orange, "Game view: %s", StatusText(game.status));
    if (game.status == GtaoFrameStatus::Ok) {
        ImGui::Text("AO size: %u x %u (divisor %u)", game.aoExtent.width, game.aoExtent.height, game.divisor);
        ImGui::Text("Temporal history: %s", game.historyUsed ? "in use" : "not used");
        ImGui::Text("Sample radius at 5 m: %.1f px", game.screenRadiusAt5m);
    }
}

} // namespace

bool BuildGtaoPanel(EditorContext& /*ctx*/, GtaoSettings& edit, const GtaoViewSnapshot& game, const GtaoViewSnapshot& scene)
{
    bool changed = false;
    ImGui::Begin("Gtao");

    DrawStatus(game);

    ImGui::SeparatorText("Sampling");
    changed |= ImGui::DragFloat("Radius", &edit.radius, 0.01f, 0.05f, 10.0f);
    changed |= ImGui::SliderFloat("Falloff", &edit.falloffFraction, 0.05f, 1.0f);
    changed |= SliderCount("Slices", edit.sliceCount, 1, 8);
    changed |= SliderCount("Steps", edit.stepCount, 1, 12);
    changed |= ImGui::SliderFloat("Distribution power", &edit.sampleDistributionPower, 1.0f, 4.0f);
    changed |= ImGui::SliderFloat("Thin occluder", &edit.thinOccluderCompensation, 0.0f, 0.7f);

    ImGui::SeparatorText("Look");
    changed |= ImGui::SliderFloat("Strength", &edit.strength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Final power", &edit.finalPower, 0.5f, 4.0f);
    changed |= ImGui::SliderFloat("Min visibility", &edit.minVisibility, 0.0f, 0.5f);
    changed |= ImGui::DragFloat("Max distance", &edit.maxDistance, 1.0f, 1.0f, 100000.0f);
    changed |= ImGui::SliderFloat("Fade fraction", &edit.distanceFadeFraction, 0.0f, 0.9f);
    changed |= ImGui::Checkbox("Apply in linear space", &edit.applyInLinearSpace);

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

    ImGui::SeparatorText("Debug");
    int view = static_cast<int>(edit.debugView);
    if (ImGui::Combo("View", &view, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
        edit.debugView = static_cast<GtaoDebugView>(std::clamp(view, 0, static_cast<int>(kDebugViews.size()) - 1));
        changed = true;
    }
    const double megabytes = static_cast<double>(game.textureBytes + scene.textureBytes) / (1024.0 * 1024.0);
    ImGui::Text("AO texture memory (Game + Scene): %.1f MB", megabytes);

    if (ImGui::Button("Reset to defaults")) {
        edit = GtaoSettings{};
        changed = true;
    }

    ImGui::End();
    return changed;
}

} // namespace gte
